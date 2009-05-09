/**
 * collectd - src/utils_mount.c
 * Copyright (C) 2005,2006  Niki W. Waibel
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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "common.h"
#if HAVE_XFS_XQM_H
# include <xfs/xqm.h>
#define XFS_SUPER_MAGIC_STR "XFSB"
#define XFS_SUPER_MAGIC2_STR "BSFX"
#endif

#include "plugin.h"
#include "utils_mount.h"

#if HAVE_GETVFSSTAT
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif
#  if HAVE_SYS_STATVFS_H
#    include <sys/statvfs.h>
#  endif
/* #endif HAVE_GETVFSSTAT */

#elif HAVE_GETFSSTAT
#  if HAVE_SYS_PARAM_H
#    include <sys/param.h>
#  endif
#  if HAVE_SYS_UCRED_H
#    include <sys/ucred.h>
#  endif
#  if HAVE_SYS_MOUNT_H
#    include <sys/mount.h>
#  endif
#endif /* HAVE_GETFSSTAT */

#if HAVE_MNTENT_H
#  include <mntent.h>
#endif
#if HAVE_SYS_MNTTAB_H
#  include <sys/mnttab.h>
#endif

#if HAVE_PATHS_H
#  include <paths.h>
#endif

#ifdef COLLECTD_MNTTAB
#  undef COLLECTD_MNTTAB
#endif

#if defined(_PATH_MOUNTED) /* glibc */
#  define COLLECTD_MNTTAB _PATH_MOUNTED
#elif defined(MNTTAB) /* Solaris */
#  define COLLECTD_MNTTAB MNTTAB
#elif defined(MNT_MNTTAB)
#  define COLLECTD_MNTTAB MNT_MNTTAB
#elif defined(MNTTABNAME)
#  define COLLECTD_MNTTAB MNTTABNAME
#elif defined(KMTAB)
#  define COLLECTD_MNTTAB KMTAB
#else
#  define COLLECTD_MNTTAB "/etc/mnttab"
#endif

/* *** *** *** ********************************************* *** *** *** */
/* *** *** *** *** *** ***   private functions   *** *** *** *** *** *** */
/* *** *** *** ********************************************* *** *** *** */

/* stolen from quota-3.13 (quota-tools) */

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR     "/dev"
#define UUID   1
#define VOL    2

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
				ssnprintf(device, sizeof(device), "%s/%s",
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
		DEBUG("utils_mount: Found an invalid UUID: %s", s);
	return NULL;
}

static char *get_spec_by_volume_label(const char *s)
{
        return get_spec_by_x (VOL, s);
}

static char *get_device_name(const char *optstr)
{
	char *rc;

	if (optstr == NULL)
	{
		return (NULL);
	}
	else if (strncmp (optstr, "UUID=", 5) == 0)
	{
		DEBUG ("utils_mount: TODO: check UUID= code!");
		rc = get_spec_by_uuid (optstr + 5);
	}
	else if (strncmp (optstr, "LABEL=", 6) == 0)
	{
		DEBUG ("utils_mount: TODO: check LABEL= code!");
		rc = get_spec_by_volume_label (optstr + 6);
	}
	else
	{
		rc = sstrdup (optstr);
	}

	if(!rc)
	{
		DEBUG ("utils_mount: Error checking device name: optstr = %s", optstr);
	}
	return rc;
}

/* What weird OS is this..? I can't find any info with google :/ -octo */
#if HAVE_LISTMNTENT && 0
static cu_mount_t *cu_mount_listmntent (void)
{
	cu_mount_t *last = *list;
	struct tabmntent *p;
	struct mntent *mnt;

	struct tabmntent *mntlist;
	if(listmntent(&mntlist, COLLECTD_MNTTAB, NULL, NULL) < 0) {
#if COLLECT_DEBUG
		char errbuf[1024];
		DEBUG("utils_mount: calling listmntent() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
#endif /* COLLECT_DEBUG */
	}

	for(p = mntlist; p; p = p->next) {
		char *loop = NULL, *device = NULL;

		mnt = p->ment;
		loop = cu_mount_getoptionvalue(mnt->mnt_opts, "loop=");
		if(loop == NULL) {   /* no loop= mount */
			device = get_device_name(mnt->mnt_fsname);
			if(device == NULL) {
				DEBUG("utils_mount: can't get devicename for fs (%s) %s (%s)"
					": ignored", mnt->mnt_type,
					mnt->mnt_dir, mnt->mnt_fsname);
				continue;
			}
		} else {
			device = loop;
		}
		if(*list == NULL) {
			*list = (cu_mount_t *)smalloc(sizeof(cu_mount_t));
			last = *list;
		} else {
			while(last->next != NULL) { /* is last really last? */
				last = last->next;
			}
			last->next = (cu_mount_t *)smalloc(sizeof(cu_mount_t));
			last = last->next;
		}
		last->dir = sstrdup(mnt->mnt_dir);
		last->spec_device = sstrdup(mnt->mnt_fsname);
		last->device = device;
		last->type = sstrdup(mnt->mnt_type);
		last->options = sstrdup(mnt->mnt_opts);
		last->next = NULL;
	} /* for(p = mntlist; p; p = p->next) */

	return(last);
} /* cu_mount_t *cu_mount_listmntent(void) */
/* #endif HAVE_LISTMNTENT */

/* 4.4BSD and Mac OS X (getfsstat) or NetBSD (getvfsstat) */
#elif HAVE_GETVFSSTAT || HAVE_GETFSSTAT
static cu_mount_t *cu_mount_getfsstat (void)
{
#if HAVE_GETVFSSTAT
#  define STRUCT_STATFS struct statvfs
#  define CMD_STATFS    getvfsstat
#  define FLAGS_STATFS  ST_NOWAIT
/* #endif HAVE_GETVFSSTAT */
#elif HAVE_GETFSSTAT
#  define STRUCT_STATFS struct statfs
#  define CMD_STATFS    getfsstat
#  define FLAGS_STATFS  MNT_NOWAIT
#endif /* HAVE_GETFSSTAT */

	int bufsize;
	STRUCT_STATFS *buf;

	int num;
	int i;

	cu_mount_t *first = NULL;
	cu_mount_t *last  = NULL;
	cu_mount_t *new   = NULL;

	/* Get the number of mounted file systems */
	if ((bufsize = CMD_STATFS (NULL, 0, FLAGS_STATFS)) < 1)
	{
#if COLLECT_DEBUG
		char errbuf[1024];
		DEBUG ("utils_mount: getv?fsstat failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
#endif /* COLLECT_DEBUG */
		return (NULL);
	}

	if ((buf = (STRUCT_STATFS *) malloc (bufsize * sizeof (STRUCT_STATFS)))
			== NULL)
		return (NULL);
	memset (buf, '\0', bufsize * sizeof (STRUCT_STATFS));

	/* The bufsize needs to be passed in bytes. Really. This is not in the
	 * manpage.. -octo */
	if ((num = CMD_STATFS (buf, bufsize * sizeof (STRUCT_STATFS), FLAGS_STATFS)) < 1)
	{
#if COLLECT_DEBUG
		char errbuf[1024];
		DEBUG ("utils_mount: getv?fsstat failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
#endif /* COLLECT_DEBUG */
		free (buf);
		return (NULL);
	}

	for (i = 0; i < num; i++)
	{
		if ((new = malloc (sizeof (cu_mount_t))) == NULL)
			break;
		memset (new, '\0', sizeof (cu_mount_t));
		
		/* Copy values from `struct mnttab' */
		new->dir         = sstrdup (buf[i].f_mntonname);
		new->spec_device = sstrdup (buf[i].f_mntfromname);
		new->type        = sstrdup (buf[i].f_fstypename);
		new->options     = NULL;
		new->device      = get_device_name (new->options);
		new->next = NULL;

		/* Append to list */
		if (first == NULL)
		{
			first = new;
			last  = new;
		}
		else
		{
			last->next = new;
			last       = new;
		}
	}

	free (buf);

	return (first);
}
/* #endif HAVE_GETVFSSTAT || HAVE_GETFSSTAT */

/* Solaris (SunOS 10): int getmntent(FILE *fp, struct mnttab *mp); */
#elif HAVE_TWO_GETMNTENT || HAVE_GEN_GETMNTENT || HAVE_SUN_GETMNTENT
static cu_mount_t *cu_mount_gen_getmntent (void)
{
	struct mnttab mt;
	FILE *fp;

	cu_mount_t *first = NULL;
	cu_mount_t *last  = NULL;
	cu_mount_t *new   = NULL;

	DEBUG ("utils_mount: (void); COLLECTD_MNTTAB = %s", COLLECTD_MNTTAB);

	if ((fp = fopen (COLLECTD_MNTTAB, "r")) == NULL)
	{
		char errbuf[1024];
		ERROR ("fopen (%s): %s", COLLECTD_MNTTAB,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (NULL);
	}

	while (getmntent (fp, &mt) == 0)
	{
		if ((new = malloc (sizeof (cu_mount_t))) == NULL)
			break;
		memset (new, '\0', sizeof (cu_mount_t));
		
		/* Copy values from `struct mnttab' */
		new->dir         = sstrdup (mt.mnt_mountp);
		new->spec_device = sstrdup (mt.mnt_special);
		new->type        = sstrdup (mt.mnt_fstype);
		new->options     = sstrdup (mt.mnt_mntopts);
		new->device      = get_device_name (new->options);
		new->next = NULL;

		/* Append to list */
		if (first == NULL)
		{
			first = new;
			last  = new;
		}
		else
		{
			last->next = new;
			last       = new;
		}
	}

	fclose (fp);

	return (first);
} /* static cu_mount_t *cu_mount_gen_getmntent (void) */
/* #endif HAVE_TWO_GETMNTENT || HAVE_GEN_GETMNTENT || HAVE_SUN_GETMNTENT */

#elif HAVE_SEQ_GETMNTENT
#warn "This version of `getmntent' hat not yet been implemented!"
/* #endif HAVE_SEQ_GETMNTENT */

#elif HAVE_ONE_GETMNTENT
static cu_mount_t *cu_mount_getmntent (void)
{
	FILE *fp;
	struct mntent *me;

	cu_mount_t *first = NULL;
	cu_mount_t *last  = NULL;
	cu_mount_t *new   = NULL;

	DEBUG ("utils_mount: (void); COLLECTD_MNTTAB = %s", COLLECTD_MNTTAB);

	if ((fp = setmntent (COLLECTD_MNTTAB, "r")) == NULL)
	{
		char errbuf[1024];
		ERROR ("setmntent (%s): %s", COLLECTD_MNTTAB,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (NULL);
	}

	while ((me = getmntent (fp)) != NULL)
	{
		if ((new = malloc (sizeof (cu_mount_t))) == NULL)
			break;
		memset (new, '\0', sizeof (cu_mount_t));
		
		/* Copy values from `struct mntent *' */
		new->dir         = sstrdup (me->mnt_dir);
		new->spec_device = sstrdup (me->mnt_fsname);
		new->type        = sstrdup (me->mnt_type);
		new->options     = sstrdup (me->mnt_opts);
		new->device      = get_device_name (new->options);
		new->next        = NULL;

		DEBUG ("utils_mount: new = {dir = %s, spec_device = %s, type = %s, options = %s, device = %s}",
				new->dir, new->spec_device, new->type, new->options, new->device);

		/* Append to list */
		if (first == NULL)
		{
			first = new;
			last  = new;
		}
		else
		{
			last->next = new;
			last       = new;
		}
	}

	endmntent (fp);

	DEBUG ("utils_mount: return (0x%p)", (void *) first);

	return (first);
}
#endif /* HAVE_ONE_GETMNTENT */

/* *** *** *** ******************************************** *** *** *** */
/* *** *** *** *** *** ***   public functions   *** *** *** *** *** *** */
/* *** *** *** ******************************************** *** *** *** */

cu_mount_t *cu_mount_getlist(cu_mount_t **list)
{
	cu_mount_t *new;
	cu_mount_t *first = NULL;
	cu_mount_t *last  = NULL;

	if (list == NULL)
		return (NULL);

	if (*list != NULL)
	{
		first = *list;
		last  =  first;
		while (last->next != NULL)
			last = last->next;
	}

#if HAVE_LISTMNTENT && 0
	new = cu_mount_listmntent ();
#elif HAVE_GETVFSSTAT || HAVE_GETFSSTAT
	new = cu_mount_getfsstat ();
#elif HAVE_TWO_GETMNTENT || HAVE_GEN_GETMNTENT || HAVE_SUN_GETMNTENT
	new = cu_mount_gen_getmntent ();
#elif HAVE_SEQ_GETMNTENT
# error "This version of `getmntent' hat not yet been implemented!"
#elif HAVE_ONE_GETMNTENT
	new = cu_mount_getmntent ();
#else
# error "Could not determine how to find mountpoints."
#endif

	if (first != NULL)
	{
		last->next = new;
	}
	else
	{
		first = new;
		last  = new;
		*list = first;
	}

	while ((last != NULL) && (last->next != NULL))
		last = last->next;

	return (last);
} /* cu_mount_t *cu_mount_getlist(cu_mount_t **list) */

void cu_mount_freelist (cu_mount_t *list)
{
	cu_mount_t *this;
	cu_mount_t *next;

	for (this = list; this != NULL; this = next)
	{
		next = this->next;

		sfree (this->dir);
		sfree (this->spec_device);
		sfree (this->device);
		sfree (this->type);
		sfree (this->options);
		sfree (this);
	}
} /* void cu_mount_freelist(cu_mount_t *list) */

char *
cu_mount_checkoption(char *line, char *keyword, int full)
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
} /* char *cu_mount_checkoption(char *line, char *keyword, int full) */

char *
cu_mount_getoptionvalue(char *line, char *keyword)
{
	char *r;

	r = cu_mount_checkoption(line, keyword, 0);
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
} /* char *cu_mount_getoptionvalue(char *line, char *keyword) */

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

