/**
 * collectd - src/utils_mount.c
 * Copyright (C) 2005  Niki W. Waibel
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * Author:
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/



#include "common.h"
#if HAVE_XFS_XQM_H
# include <xfs/xqm.h>
#define xfs_mem_dqinfo  fs_quota_stat
#define Q_XFS_GETQSTAT  Q_XGETQSTAT
#define XFS_SUPER_MAGIC_STR "XFSB"
#define XFS_SUPER_MAGIC2_STR "BSFX"
#endif
#include "utils_debug.h"
#include "utils_mount.h"



/* *** *** ***   local functions   *** *** *** */



/* stolen from quota-3.13 (quota-tools) */

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR     "/dev"
#define UUID   1
#define VOL    2

#define AUTOFS_DIR_MAX 64       /* Maximum number of autofs directories */

static struct uuidCache_s {
	struct uuidCache_s *next;
	char uuid[16];
	char *label;
	char *device;
} *uuidCache = NULL;

#define EXT2_SUPER_MAGIC 0xEF53
struct ext2_super_block {
	unsigned char s_dummy1[56];
	unsigned char s_magic[2];
	unsigned char s_dummy2[46];
	unsigned char s_uuid[16];
	char s_volume_name[16];
};
#define ext2magic(s) ((unsigned int)s.s_magic[0] \
	+ (((unsigned int)s.s_magic[1]) << 8))

#if HAVE_XFS_XQM_H
struct xfs_super_block {
	unsigned char s_magic[4];
	unsigned char s_dummy[28];
	unsigned char s_uuid[16];
	unsigned char s_dummy2[60];
	char s_fsname[12];
};
#endif /* HAVE_XFS_XQM_H */

#define REISER_SUPER_MAGIC "ReIsEr2Fs"
struct reiserfs_super_block {
	unsigned char s_dummy1[52];
	unsigned char s_magic[10];
	unsigned char s_dummy2[22];
	unsigned char s_uuid[16];
	char s_volume_name[16];
};

/* for now, only ext2 and xfs are supported */
static int
get_label_uuid(const char *device, char **label, char *uuid)
{
	/* start with ext2 and xfs tests, taken from mount_guess_fstype */
	/* should merge these later */
	int fd, rv = 1;
	size_t namesize;
	struct ext2_super_block e2sb;
#if HAVE_XFS_XQM_H
	struct xfs_super_block xfsb;
#endif
	struct reiserfs_super_block reisersb;

	fd = open(device, O_RDONLY);
	if(fd == -1) {
		return rv;
	}

	if(lseek(fd, 1024, SEEK_SET) == 1024
	&& read(fd, (char *)&e2sb, sizeof(e2sb)) == sizeof(e2sb)
	&& ext2magic(e2sb) == EXT2_SUPER_MAGIC) {
		memcpy(uuid, e2sb.s_uuid, sizeof(e2sb.s_uuid));
		namesize = sizeof(e2sb.s_volume_name);
		*label = smalloc(namesize + 1);
		sstrncpy(*label, e2sb.s_volume_name, namesize);
		rv = 0;
#if HAVE_XFS_XQM_H
	} else if(lseek(fd, 0, SEEK_SET) == 0
	&& read(fd, (char *)&xfsb, sizeof(xfsb)) == sizeof(xfsb)
	&& (strncmp((char *)&xfsb.s_magic, XFS_SUPER_MAGIC_STR, 4) == 0 ||
	strncmp((char *)&xfsb.s_magic, XFS_SUPER_MAGIC2_STR, 4) == 0)) {
		memcpy(uuid, xfsb.s_uuid, sizeof(xfsb.s_uuid));
		namesize = sizeof(xfsb.s_fsname);
		*label = smalloc(namesize + 1);
		sstrncpy(*label, xfsb.s_fsname, namesize);
		rv = 0;
#endif /* HAVE_XFS_XQM_H */
	} else if(lseek(fd, 65536, SEEK_SET) == 65536
	&& read(fd, (char *)&reisersb, sizeof(reisersb)) == sizeof(reisersb)
	&& !strncmp((char *)&reisersb.s_magic, REISER_SUPER_MAGIC, 9)) {
		memcpy(uuid, reisersb.s_uuid, sizeof(reisersb.s_uuid));
		namesize = sizeof(reisersb.s_volume_name);
		*label = smalloc(namesize + 1);
		sstrncpy(*label, reisersb.s_volume_name, namesize);
		rv = 0;
	}
	close(fd);
	return rv;
}

static void
uuidcache_addentry(char *device, char *label, char *uuid)
{
	struct uuidCache_s *last;

	if(!uuidCache) {
		last = uuidCache = smalloc(sizeof(*uuidCache));
	} else {
		for(last = uuidCache; last->next; last = last->next);
		last->next = smalloc(sizeof(*uuidCache));
		last = last->next;
	}
	last->next = NULL;
	last->device = device;
	last->label = label;
	memcpy(last->uuid, uuid, sizeof(last->uuid));
}

static void
uuidcache_init(void)
{
	char line[100];
	char *s;
	int ma, mi, sz;
	static char ptname[100];
	FILE *procpt;
	char uuid[16], *label = NULL;
	char device[110];
	int firstPass;
	int handleOnFirst;

	if(uuidCache) {
		return;
	}

	procpt = fopen(PROC_PARTITIONS, "r");
	if(procpt == NULL) {
		return;
	}

	for(firstPass = 1; firstPass >= 0; firstPass--) {
		fseek(procpt, 0, SEEK_SET);
		while(fgets(line, sizeof(line), procpt)) {
			if(sscanf(line, " %d %d %d %[^\n ]",
				&ma, &mi, &sz, ptname) != 4)
			{
				continue;
			}

			/* skip extended partitions (heuristic: size 1) */
			if(sz == 1) {
				continue;
			}

			/* look only at md devices on first pass */
			handleOnFirst = !strncmp(ptname, "md", 2);
			if(firstPass != handleOnFirst) {
				continue;
			}

			/* skip entire disk (minor 0, 64, ... on ide;
			0, 16, ... on sd) */
			/* heuristic: partition name ends in a digit */

			for(s = ptname; *s; s++);

			if(isdigit((int)s[-1])) {
			/*
			* Note: this is a heuristic only - there is no reason
			* why these devices should live in /dev.
			* Perhaps this directory should be specifiable by option.
			* One might for example have /devlabel with links to /dev
			* for the devices that may be accessed in this way.
			* (This is useful, if the cdrom on /dev/hdc must not
			* be accessed.)
			*/
				snprintf(device, sizeof(device), "%s/%s",
					DEVLABELDIR, ptname);
				if(!get_label_uuid(device, &label, uuid)) {
					uuidcache_addentry(sstrdup(device),
						label, uuid);
				}
			}
		}
	}
	fclose(procpt);
}

static unsigned char
fromhex(char c)
{
	if(isdigit((int)c)) {
		return (c - '0');
	} else if(islower((int)c)) {
		return (c - 'a' + 10);
	} else {
		return (c - 'A' + 10);
	}
}

static char *
get_spec_by_x(int n, const char *t)
{
	struct uuidCache_s *uc;

	uuidcache_init();
	uc = uuidCache;

	while(uc) {
		switch(n) {
		case UUID:
			if(!memcmp(t, uc->uuid, sizeof(uc->uuid))) {
				return sstrdup(uc->device);
			}
			break;
		case VOL:
			if(!strcmp(t, uc->label)) {
				return sstrdup(uc->device);
			}
			break;
		}
		uc = uc->next;
	}
	return NULL;
}

static char *
get_spec_by_uuid(const char *s)
{
	char uuid[16];
	int i;

	if(strlen(s) != 36
	|| s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
		goto bad_uuid;
	}

	for(i=0; i<16; i++) {
		if(*s == '-') {
			s++;
		}
		if(!isxdigit((int)s[0]) || !isxdigit((int)s[1])) {
			goto bad_uuid;
		}
		uuid[i] = ((fromhex(s[0]) << 4) | fromhex(s[1]));
		s += 2;
	}
	return get_spec_by_x(UUID, uuid);

	bad_uuid:
		DBG("Found an invalid UUID: %s", s);
	return NULL;
}

static char *
get_spec_by_volume_label(const char *s)
{
        return get_spec_by_x(VOL, s);
}

static char *
get_device_name(const char *item)
{
	char *rc;

	if(!strncmp(item, "UUID=", 5)) {
		DBG("TODO: check UUID= code!");
		rc = get_spec_by_uuid(item + 5);
	} else if(!strncmp(item, "LABEL=", 6)) {
		DBG("TODO: check LABEL= code!");
		rc = get_spec_by_volume_label(item + 6);
	} else {
		rc = sstrdup(item);
	}
	if(!rc) {
		DBG("Error checking device name: %s", item);
	}
	return rc;
}

#if HAVE_LISTMNTENT
static void
cu_mount_listmntent(struct tabmntent *mntlist, cu_mount_t **list)
{
	struct *p;
	struct mntent *mnt;

	for(p = mntlist; p; p = p->next) {
		mnt = p->ment;
		*list = smalloc(sizeof(cu_mount_t));
		list->device = strdup(mnt->mnt_fsname);
		list->name = strdup(mnt->mnt_dir);
		list->type = strdup(mnt->mnt_type);
		list->next = NULL;
		list = &(ist->next);
	}
	freemntlist(mntlist);
}
#endif /* HAVE_LISTMNTENT */



#if HAVE_GETVFSENT
static void
cu_mount_getvfsmnt(FILE *mntf, cu_mount_t **list)
{
	DBG("TODO: getvfsmnt");
	*list = NULL;
}
#endif /* HAVE_GETVFSENT */

char *
cu_mount_checkmountopt(char *line, char *keyword, int full)
{
	char *line2, *l2;
	int l = strlen(keyword);
	char *p1, *p2;

	if(line == NULL || keyword == NULL) {
		return NULL;
	}
	if(full != 0) {
		full = 1;
	}

	line2 = sstrdup(line);
	l2 = line2;
	while(*l2 != '\0') {
		if(*l2 == ',') {
			*l2 = '\0';
		}
		l2++;
	}

	p1 = line - 1;
	p2 = strchr(line, ',');
	do {
		if(strncmp(line2+(p1-line)+1, keyword, l+full) == 0) {
			free(line2);
			return p1+1;
		}
		p1 = p2;
		if(p1 != NULL) {
			p2 = strchr(p1+1, ',');
		}
	} while(p1 != NULL);

	free(line2);
	return NULL;
} /* char *cu_mount_checkmountopt(char *line, char *keyword, int full) */

char *
cu_mount_getmountopt(char *line, char *keyword)
{
	char *r;

	r = cu_mount_checkmountopt(line, keyword, 0);
	if(r != NULL) {
		char *p;
		r += strlen(keyword);
		p = strchr(r, ',');
		if(p == NULL) {
			if(strlen(r) == 0) {
				return NULL;
			}
			return sstrdup(r);
		} else {
			char *m;
			if((p-r) == 1) {
				return NULL;
			}
			m = (char *)smalloc(p-r+1);
			sstrncpy(m, r, p-r+1);
			return m;
		}
	}
	return r;
} /* char *cu_mount_getmountopt(char *line, char *keyword) */

#if HAVE_GETMNTENT
static cu_mount_t *
cu_mount_getmntent(FILE *mntf, cu_mount_t **list)
{
	cu_mount_t *last = *list;
	struct mntent *mnt;

#if HAVE_GETMNTENT1
	while((mnt = getmntent(mntf)) != NULL) {
#endif /* HAVE_GETMNTENT1 */
		char *loop = NULL, *device = NULL;

#if 1
		DBG("------------------ BEGIN");
		DBG("mnt->mnt_fsname %s", mnt->mnt_fsname);
		DBG("mnt->mnt_dir    %s", mnt->mnt_dir);
		DBG("mnt->mnt_type   %s", mnt->mnt_type);
		DBG("mnt->mnt_opts   %s", mnt->mnt_opts);
		DBG("mnt->mnt_freq   %d", mnt->mnt_freq);
		DBG("mnt->mnt_passno %d", mnt->mnt_passno);
#endif

		loop = cu_mount_getmountopt(mnt->mnt_opts, "loop=");
		if(loop == NULL) {   /* no loop= mount */
			device = get_device_name(mnt->mnt_fsname);
			if(device == NULL) {
				DBG("can't get devicename for fs (%s) %s (%s)"
					": ignored", mnt->mnt_type,
					mnt->mnt_dir, mnt->mnt_fsname);
				continue;
			}
		} else {
			device = loop;
		}

#if 1
		DBG("device          %s", device);
		DBG("------------------ END");
#endif
		if(*list == NULL) {
			*list = (cu_mount_t *)smalloc(sizeof(cu_mount_t));
			last = *list;
		} else {
			last->next = (cu_mount_t *)smalloc(sizeof(cu_mount_t));
			last = last->next;
		}
		last->dir = sstrdup(mnt->mnt_dir);
		last->spec_device = sstrdup(mnt->mnt_fsname);
		last->device = device;
		last->type = sstrdup(mnt->mnt_type);
		last->options = sstrdup(mnt->mnt_opts);
		last->next = NULL;
	} /* while((mnt = getmntent(mntf)) != NULL) */

	return last;
} /* static cu_mount_t *cu_mount_getmntent(FILE *mntf, cu_mount_t **list) */
#endif /* HAVE_GETMNTENT */



cu_mount_t *
cu_mount_getlist(cu_mount_t **list)
{
	cu_mount_t *last = NULL;

	/* yes, i know that the indentation is wrong.
	   but show me a better way to do this... */
	/* see lib/mountlist.c of coreutils for all
	   gory details! */
#if HAVE_GETMNTENT && defined(_PATH_MOUNTED)
	{
	FILE *mntf = NULL;
	if((mntf = setmntent(_PATH_MOUNTED, "r")) == NULL) {
		DBG("opening %s failed: %s", _PATH_MOUNTED, strerror(errno));
#endif
#if HAVE_GETMNTENT && defined(MNT_MNTTAB)
	{
	FILE *mntf = NULL;
	if((mntf = setmntent(MNT_MNTTAB, "r")) == NULL) {
		DBG("opening %s failed: %s", MNT_MNTTAB, strerror(errno));
#endif
#if HAVE_GETMNTENT && defined(MNTTABNAME)
	{
	FILE *mntf = NULL;
	if((mntf = setmntent(MNTTABNAME, "r")) == NULL) {
		DBG("opening %s failed: %s", MNTTABNAME, strerror(errno));
#endif
#if HAVE_GETMNTENT && defined(_PATH_MNTTAB)
	{
	FILE *mntf = NULL;
	if((mntf = setmntent(_PATH_MNTTAB, "r")) == NULL) {
		DBG("opening %s failed: %s", _PATH_MNTTAB, strerror(errno));
#endif
#if HAVE_GETVFSENT && defined(VFSTAB)
	{
	FILE *mntf = NULL;
	if((mntf = fopen(VFSTAB, "r")) == NULL) {
		DBG("opening %s failed: %s", VFSTAB, strerror(errno));
#endif
#if HAVE_LISTMNTENT
	{
	struct tabmntent *mntlist;

	if(listmntent(&mntlist, KMTAB, NULL, NULL) < 0) {
		DBG("calling listmntent() failed: %s", strerror(errno));
#endif
		/* give up */
		DBG("failed get local mountpoints");
		return(NULL);

#if HAVE_LISTMNTENT
	} else { last = cu_mount_listmntent(mntlist, list); }
	freemntlist(mntlist);
	}
#endif
#if HAVE_GETVFSENT && defined(VFSTAB)
	} else { last = cu_mount_getvfsmnt(mntf, list); }
	(void)fclose(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(_PATH_MNTTAB)
	} else { last = cu_mount_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(MNTTABNAME)
	} else { last = cu_mount_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(MNT_MNTTAB)
	} else { last = cu_mount_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(_PATH_MOUNTED)
	} else { last = cu_mount_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
	return(last);
} /* cu_mount_t *cu_mount_getlist(cu_mount_t **list) */

void
cu_mount_freelist(cu_mount_t *list)
{
	cu_mount_t *l = list, *p = NULL;

	while(l != NULL) {
		while(l->next != NULL) {
			p = l;
			l = l->next;
		}
		if(p != NULL) {
			p->next = NULL;
		}
		sfree(l->dir);
		sfree(l->spec_device);
		sfree(l->device);
		sfree(l->type);
		sfree(l->options);
		sfree(l);
		p = NULL;
		if(l != list) {
			l = list;
		} else {
			l = NULL;
		}
	} /* while(l != NULL) */
} /* void cu_mount_freelist(cu_mount_t *list) */

int
cu_mount_type(const char *type)
{
	if(strcmp(type, "ext3") == 0) return CUMT_EXT3;
	if(strcmp(type, "ext2") == 0) return CUMT_EXT2;
	if(strcmp(type, "ufs")  == 0) return CUMT_UFS;
	if(strcmp(type, "vxfs") == 0) return CUMT_VXFS;
	if(strcmp(type, "zfs")  == 0) return CUMT_ZFS;
	return CUMT_UNKNOWN;
} /* int cu_mount_type(const char *type) */



