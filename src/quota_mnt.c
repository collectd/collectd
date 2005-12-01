/**
 * collectd - src/quota_mnt.c
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
#include "quota_debug.h"
#include "quota_common.h"
#include "quota_fs.h"
#include "quota_mnt.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#if HAVE_MNTENT_H   /* 4.3BSD, SunOS, HP-UX, Dynix, Irix. */
#include <mntent.h>
#endif
#if HAVE_MNTTAB_H   /* SVR2, SVR3. */
#include <mnttab.h>
#endif
#if HAVE_PATHS_H
#include <paths.h>
#endif
#include <stdlib.h>
#include <string.h>
#if HAVE_SYS_FS_TYPES_H   /* Ultrix. */
#include <sys/fs_types.h>
#endif
#if HAVE_SYS_MNTENT_H
#include <sys/mntent.h>
#endif
#if HAVE_SYS_MNTTAB_H   /* SVR4. */
#include <sys/mnttab.h>
#endif
#if HAVE_SYS_MOUNT_H   /* 4.4BSD, Ultrix. */
#include <sys/mount.h>
#endif
#if HAVE_SYS_VFSTAB_H
#include <sys/vfstab.h>
#endif
#if HAVE_SYS_QUOTA_H
#include <sys/quota.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#if HAVE_XFS_XQM_H
#include <xfs/xqm.h>
#define xfs_mem_dqinfo  fs_quota_stat
#define Q_XFS_GETQSTAT  Q_XGETQSTAT
#define XFS_SUPER_MAGIC_STR "XFSB"
#define XFS_SUPER_MAGIC2_STR "BSFX"
#endif

#include "quota_mntopt.h"

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

const char *
get_device_name(const char *item)
{
	const char *rc;

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

/*
 *      Check for various kinds of NFS filesystem
 */
int
nfs_fstype(char *type)
{
	return !strcmp(type, MNTTYPE_NFS) || !strcmp(type, MNTTYPE_NFS4);
}

#if HAVE_XFS_XQM_H
/*
 *      Check for XFS filesystem with quota accounting enabled
 */
static int
hasxfsquota(struct mntent *mnt, int type)
{
	int ret = 0;
	u_int16_t sbflags;
	struct xfs_mem_dqinfo info;
	const char *dev = get_device_name(mnt->mnt_fsname);

	if(!dev) {
		return ret;
	}

	memset(&info, 0, sizeof(struct xfs_mem_dqinfo));
	if(!quotactl(QCMD(Q_XFS_GETQSTAT, type), dev, 0, (void *)&info)) {
		sbflags = (info.qs_flags & 0xff00) >> 8;
		if(type == USRQUOTA && (info.qs_flags & XFS_QUOTA_UDQ_ACCT)) {
			ret = 1;
		} else if(type == GRPQUOTA && (info.qs_flags & XFS_QUOTA_GDQ_ACCT)) {
			ret = 1;
		}
		#ifdef XFS_ROOTHACK
		/*
		* Old XFS filesystems (up to XFS 1.2 / Linux 2.5.47) had a
		* hack to allow enabling quota on the root filesystem without
		* having to specify it at mount time.
		*/
		else if(strcmp(mnt->mnt_dir, "/")) {
			ret = 0;
		} else if(type == USRQUOTA && (sbflags & XFS_QUOTA_UDQ_ACCT)) {
			ret = 1;
		} else if(type == GRPQUOTA && (sbflags & XFS_QUOTA_GDQ_ACCT)) {
			ret = 1;
		#endif /* XFS_ROOTHACK */
	}
	free((char *)dev);
	return ret;
}
#endif /* HAVE_XFS_XQM_H */


#if HAVE_LISTMNTENT
static void
quota_mnt_listmntent(struct tabmntent *mntlist, quota_mnt_t **list)
{
	struct *p;
	struct mntent *mnt;

	for(p = mntlist; p; p = p->next) {
		mnt = p->ment;
		*list = smalloc(sizeof(quota_mnt_t));
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
quota_mnt_getvfsmnt(FILE *mntf, quota_mnt_t **list)
{
	DBG("TODO: getvfsmnt");
	*list = NULL;
}
#endif /* HAVE_GETVFSENT */



#if HAVE_GETMNTENT
static void
quota_mnt_getmntent(FILE *mntf, quota_mnt_t **list)
{
	struct mntent *mnt;

	while((mnt = getmntent(mntf)) != NULL) {
		const char *devname;

#if 0
		DBG("------------------");
		DBG("mnt->mnt_fsname %s", mnt->mnt_fsname);
		DBG("mnt->mnt_dir    %s", mnt->mnt_dir);
		DBG("mnt->mnt_type   %s", mnt->mnt_type);
		DBG("mnt->mnt_opts   %s", mnt->mnt_opts);
		DBG("mnt->mnt_freq   %d", mnt->mnt_freq);
		DBG("mnt->mnt_passno %d", mnt->mnt_passno);
#endif
		if(!(devname = get_device_name(mnt->mnt_fsname))) {
			DBG("can't get devicename for fs (%s) %s (%s): ignored",
				mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
			continue;
		}
		if(hasmntopt(mnt, MNTOPT_NOQUOTA) != NULL) {
			DBG("noquota option on fs (%s) %s (%s): ignored",
				mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
			free((char *)devname);
			continue;
		}
#if HAVE_XFS_XQM_H
		if(!strcmp(mnt->mnt_type, MNTTYPE_XFS)) {
			if(hasxfsquota(mnt, USRQUOTA) == 0
			&& hasxfsquota(mnt, GRPQUOTA) == 0)
			{
				DBG("no quota on fs (%s) %s (%s): ignored",
					mnt->mnt_type, mnt->mnt_dir,
					mnt->mnt_fsname);
				free((char *)devname);
				continue;
			}
		} else {
#endif /* HAVE_XFS_XQM_H */
			if(hasmntopt(mnt, MNTOPT_QUOTA) == NULL
			&& hasmntopt(mnt, MNTOPT_USRQUOTA) == NULL
			&& hasmntopt(mnt, MNTOPT_GRPQUOTA) == NULL
			&& quota_fs_isnfs(mnt->mnt_type) == EXIT_FAILURE)
			{
				DBG("neither quota/usrquota/grpquota option"
					" nor nfs fs (%s) %s (%s): ignored",
					mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
				free((char *)devname);
				continue;
			}
#if HAVE_XFS_XQM_H
		}
#endif /* HAVE_XFS_XQM_H */
		if(quota_fs_issupported(mnt->mnt_type) == EXIT_FAILURE)
		{
			DBG("unsupportet fs (%s) %s (%s): ignored",
				mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
			free((char *)devname);
			continue;
		}
#if 0
		DBG("------------------ OK");
#endif
		*list = (quota_mnt_t *)smalloc(sizeof(quota_mnt_t));
		(*list)->dir = sstrdup(mnt->mnt_dir);
		(*list)->device = sstrdup(mnt->mnt_fsname);
		(*list)->type = sstrdup(mnt->mnt_type);
		(*list)->opts = QMO_NONE;
/* TODO: this is not sufficient for XFS! */
/* TODO: maybe we should anyway NOT rely on the option in the mountfile...
   ... maybe the fs should be asked direktly all time! */
		if(hasmntopt(mnt, MNTOPT_QUOTA) != NULL
		|| hasmntopt(mnt, MNTOPT_USRQUOTA) != NULL) {
			(*list)->opts |= QMO_USRQUOTA;
		}
		if(hasmntopt(mnt, MNTOPT_GRPQUOTA) != NULL) {
			(*list)->opts |= QMO_GRPQUOTA;
		}
		(*list)->next = NULL;
		list = &((*list)->next);
	} /* while((mnt = getmntent(mntf)) != NULL) */
}
#endif /* HAVE_GETMNTENT */



quota_mnt_t *
quota_mnt_getlist(quota_mnt_t **list)
{
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
		*list = NULL;
		return(NULL);

#if HAVE_LISTMNTENT
	} else { quota_mnt_listmntent(mntlist, list); }
	freemntlist(mntlist);
	}
#endif
#if HAVE_GETVFSENT && defined(VFSTAB)
	} else { quota_mnt_getvfsmnt(mntf, list); }
	(void)fclose(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(_PATH_MNTTAB)
	} else { quota_mnt_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(MNTTABNAME)
	} else { quota_mnt_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(MNT_MNTTAB)
	} else { quota_mnt_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
#if HAVE_GETMNTENT && defined(_PATH_MOUNTED)
	} else { quota_mnt_getmntent(mntf, list); }
	(void)endmntent(mntf);
	}
#endif
	return(*list);
}

void
quota_mnt_freelist(quota_mnt_t *list)
{
	quota_mnt_t *l = list, *p = NULL;

	while(l != NULL) {
		while(l->next != NULL) {
			p = l;
			l = l->next;
		}
		if(p != NULL) {
			p->next = NULL;
		}
		free(l->dir);
		free(l->device);
		free(l);
		p = NULL;
		if(l != list) {
			l = list;
		} else {
			l = NULL;
		}
	} /* while(l != NULL) */
} /* void quota_mnt_freelist(quota_mnt_t *list) */

