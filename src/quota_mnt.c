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
#include "utils_debug.h"
#include "quota_fs.h"
#include "quota_mnt.h"


/* if sthg is changed here also change it in configure.in!!! */
#if HAVE_MNTENT_H   /* 4.3BSD, SunOS, HP-UX, Dynix, Irix. */
# include <mntent.h>
#endif
#if HAVE_MNTTAB_H   /* SVR2, SVR3. */
# include <mnttab.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#if HAVE_SYS_FS_TYPES_H   /* Ultrix. */
# include <sys/fs_types.h>
#endif
#if HAVE_SYS_MNTENT_H
# include <sys/mntent.h>
#endif
#if HAVE_SYS_MNTTAB_H   /* SVR4. */
# include <sys/mnttab.h>
#endif
#if HAVE_SYS_MOUNT_H   /* 4.4BSD, Ultrix. */
# include <sys/mount.h>
#endif
#if HAVE_SYS_VFSTAB_H
# include <sys/vfstab.h>
#endif
#if HAVE_SYS_QUOTA_H
# include <sys/quota.h>
#endif
#if HAVE_SYS_VFS_H
# include <sys/vfs.h>
#endif
/* END if sthg is changed here also change it in configure.in!!! */
#if HAVE_XFS_XQM_H
# include <xfs/xqm.h>
#define xfs_mem_dqinfo  fs_quota_stat
#define Q_XFS_GETQSTAT  Q_XGETQSTAT
#define XFS_SUPER_MAGIC_STR "XFSB"
#define XFS_SUPER_MAGIC2_STR "BSFX"
#endif



/* *** *** ***   local functions   *** *** *** */



#if 0
#if HAVE_GETMNTENT
static quota_mnt_t *
quota_mnt_getmntent(FILE *mntf, quota_mnt_t **list)
{
	quota_mnt_t *last = *list;
	struct mntent *mnt;

#if HAVE_GETMNTENT1
	while((mnt = getmntent(mntf)) != NULL) {
#endif /* HAVE_GETMNTENT1 */
		char *loop = NULL, *device = NULL;
		char *usrjquota = NULL;
		char *grpjquota = NULL;
		char *jqfmt = NULL;
		int opts = QMO_NONE;

#if 0
		DBG("------------------");
		DBG("mnt->mnt_fsname %s", mnt->mnt_fsname);
		DBG("mnt->mnt_dir    %s", mnt->mnt_dir);
		DBG("mnt->mnt_type   %s", mnt->mnt_type);
		DBG("mnt->mnt_opts   %s", mnt->mnt_opts);
		DBG("mnt->mnt_freq   %d", mnt->mnt_freq);
		DBG("mnt->mnt_passno %d", mnt->mnt_passno);
#endif

		if(quota_fs_issupported(mnt->mnt_type) == EXIT_FAILURE)
		{
			DBG("unsupportet fs (%s) %s (%s): ignored",
				mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
			continue;
		}

		if(quota_mnt_checkmountopt(mnt->mnt_opts, "noquota", 1) != NULL) {
			DBG("noquota option on fs (%s) %s (%s): ignored",
				mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
			continue;
		}

		if(quota_mnt_checkmountopt(mnt->mnt_opts, "bind", 1) != NULL) {
			DBG("bind mount on fs (%s) %s (%s): ignored",
				mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
			continue;
		}

		loop = quota_mnt_getmountopt(mnt->mnt_opts, "loop=");
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

		if(quota_mnt_checkmountopt(mnt->mnt_opts, "quota", 1) != NULL) {
			opts |= QMO_USRQUOTA;
		}
		if(quota_mnt_checkmountopt(mnt->mnt_opts, "usrquota", 1) != NULL) {
			opts |= QMO_USRQUOTA;
		}
		usrjquota = quota_mnt_getmountopt(mnt->mnt_opts, "usrjquota=");
		if(usrjquota != NULL) {
			opts |= QMO_USRQUOTA;
		}
		if(quota_mnt_checkmountopt(mnt->mnt_opts, "grpquota", 1) != NULL) {
			opts |= QMO_GRPQUOTA;
		}
		grpjquota = quota_mnt_getmountopt(mnt->mnt_opts, "grpjquota=");
		if(grpjquota != NULL) {
			opts |= QMO_GRPQUOTA;
		}
		jqfmt = quota_mnt_getmountopt(mnt->mnt_opts, "jqfmt=");

#if HAVE_XFS_XQM_H
		if(!strcmp(mnt->mnt_type, "xfs")) {
			if(hasxfsquota(mnt, USRQUOTA) == 0
			&& hasxfsquota(mnt, GRPQUOTA) == 0)
			{
				DBG("no quota on fs (%s) %s (%s): ignored",
					mnt->mnt_type, mnt->mnt_dir,
					mnt->mnt_fsname);
				sfree(loop);
				sfree(usrjquota);
				sfree(grpjquota);
				sfree(jqfmt);
				continue;
			}
		} else {
#endif /* HAVE_XFS_XQM_H */
			if((opts == QMO_NONE) && (quota_fs_isnfs(mnt->mnt_type) == EXIT_FAILURE))
			{
				DBG("neither quota/usrquota/grpquota/usrjquota/grpjquota"
					" option nor nfs fs (%s) %s (%s): ignored",
					mnt->mnt_type, mnt->mnt_dir, mnt->mnt_fsname);
				sfree(loop);
				sfree(usrjquota);
				sfree(grpjquota);
				sfree(jqfmt);
				continue;
			}
#if HAVE_XFS_XQM_H
		}
#endif /* HAVE_XFS_XQM_H */
#if 0
		DBG("------------------ OK");
#endif
		if(*list == NULL) {
			*list = (quota_mnt_t *)smalloc(sizeof(quota_mnt_t));
			last = *list;
		} else {
			last->next = (quota_mnt_t *)smalloc(sizeof(quota_mnt_t));
			last = last->next;
		}
		last->m->dir = sstrdup(mnt->mnt_dir);
		last->m->device = device;
		last->m->type = sstrdup(mnt->mnt_type);
		last->m->options = sstrdup(mnt->mnt_opts);
		last->usrjquota = usrjquota;
		last->grpjquota = grpjquota;
		last->jqfmt = jqfmt;
		last->opts = opts;
		last->next = NULL;
	} /* while((mnt = getmntent(mntf)) != NULL) */

	return last;
} /* static quota_mnt_t *quota_mnt_getmntent(FILE *mntf, quota_mnt_t **list) */
#endif /* HAVE_GETMNTENT */
#endif


quota_mnt_t *
quota_mnt_getlist(quota_mnt_t **list)
{
	cu_mount_t *fulllist = NULL, *fl;
	quota_mnt_t *last = NULL;

	(void)cu_mount_getlist(&fulllist);
	fl = fulllist;
	while(fl != NULL) {

		fl = fl->next;
	} /* while(fl != NULL) */
	cu_mount_freelist(fulllist);

	return(last);
} /* quota_mnt_t *quota_mnt_getlist(quota_mnt_t **list) */

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
		sfree(l->m->dir);
		sfree(l->m->device);
		sfree(l->m->type);
		sfree(l->m->options);
		sfree(l->usrjquota);
		sfree(l->grpjquota);
		sfree(l->jqfmt);
		sfree(l);
		p = NULL;
		if(l != list) {
			l = list;
		} else {
			l = NULL;
		}
	} /* while(l != NULL) */
} /* void quota_mnt_freelist(quota_mnt_t *list) */


