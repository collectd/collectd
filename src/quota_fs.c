/**
 * collectd - src/quota_fs.c
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
#include "quota_mnt.h"
#include "quota_fs.h"

#if HAVE_SYS_QUOTA_H
# include <sys/quota.h>
#endif

/* *** *** ***   prototypes of local functions   *** *** *** */

static int qft(const char *type);
static quota_t *getquota_ext3(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ext3_v1(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ext3_v2(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ext2(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ufs(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_vxfs(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_zfs(quota_t **quota, quota_mnt_t *m);

/* *** *** ***   local functions   *** *** *** */

static int
qft(const char *type)
{
	if(strcmp(type, "ext3") == 0) return QFT_EXT3;
	if(strcmp(type, "ext2") == 0) return QFT_EXT2;
	if(strcmp(type, "ufs")  == 0) return QFT_UFS;
	if(strcmp(type, "vxfs") == 0) return QFT_VXFS;
	if(strcmp(type, "zfs")  == 0) return QFT_ZFS;
	return QFT_NONE;
} /* static int qft(const char *type) */

static quota_t *
getquota_ext3(quota_t **quota, quota_mnt_t *m)
{
#if HAVE_QUOTACTL
	uint32_t fmt;
#endif
#if HAVE_QUOTACTL
	if(quotactl(QCMD(Q_GETFMT, USRQUOTA), m->device,
		0, (void *)&fmt) == -1)
	{
		DBG("quotactl returned -1: %s", strerror(errno));
		return NULL;
	}
	if(fmt == 1) {
		return getquota_ext3_v1(quota, m);
	} else if(fmt == 2) {
		return getquota_ext3_v2(quota, m);
	} else {
		DBG("unknown quota format: 0x%08x", fmt);
		return NULL;
	}
#endif /* HAVE_QUOTACTL */

	return NULL;
} /* static quota_t *getquota_ext3(quota_t **quota, quota_mnt_t *m) */

static quota_t *
getquota_ext3_v1(quota_t **quota, quota_mnt_t *m)
{
#if HAVE_QUOTACTL
	struct dqinfo dqi_usr, dqi_grp;
#endif
	quota_t *q;

	DBG("quota v1:");
#if HAVE_QUOTACTL
	if(quotactl(QCMD(Q_GETINFO, USRQUOTA), m->device,
		0, (void *)&dqi_usr) == -1)
	{
		DBG("quotactl (Q_GETINFO, USRQUOTA) returned -1: %s",
			strerror(errno));
		*quota = NULL;
		return NULL;
	}
	if(quotactl(QCMD(Q_GETINFO, GRPQUOTA), m->device,
		0, (void *)&dqi_grp) == -1)
	{
		DBG("quotactl (Q_GETINFO, GRPQUOTA) returned -1: %s",
			strerror(errno));
		*quota = NULL;
		return NULL;
	}
#endif /* HAVE_QUOTACTL */

	q = *quota = (quota_t *)smalloc(sizeof(quota_t));

	q->type = (char *)sstrdup("usrquota");
#if HAVE_GRP_H
/* struct group *getgrid((gid_t)500) */
	q->name = (char *)sstrdup("niki");
#else
	q->name = (char *)sstrdup("");
#endif
	q->id = (char *)sstrdup("500");
	q->dir = (char *)sstrdup(m->dir);
	q->blocks = 5;
	q->bquota = 100;
	q->blimit = 180;
	q->bgrace = dqi_usr.dqi_bgrace;
	q->btimeleft = 0;
	q->inodes = 5;
	q->iquota = 100;
	q->ilimit = 180;
	q->igrace = dqi_usr.dqi_igrace;
	q->itimeleft = 0;
	q->next = NULL;

	q->next = (quota_t *)smalloc(sizeof(quota_t));
	q = q->next;
	q->type = (char *)sstrdup("grpquota");
#if HAVE_GRP_H
/* struct group *getgrid((gid_t)500) */
	q->name = (char *)sstrdup("users");
#else
	q->name = (char *)sstrdup("");
#endif
	q->id = (char *)sstrdup("100");
	q->dir = (char *)sstrdup(m->dir);
	q->blocks = 5;
	q->bquota = 100;
	q->blimit = 180;
	q->bgrace = dqi_grp.dqi_bgrace;
	q->btimeleft = 0;
	q->inodes = 5;
	q->iquota = 100;
	q->ilimit = 180;
	q->igrace = dqi_grp.dqi_igrace;
	q->itimeleft = 0;
	q->next = NULL;

	return *quota;
}

static quota_t *
getquota_ext3_v2(quota_t **quota, quota_mnt_t *m)
{
	DBG("quota v2:");
	return getquota_ext3_v1(quota, m);
}

static quota_t *
getquota_ext2(quota_t **quota, quota_mnt_t *m)
{
	return NULL;
}

static quota_t *
getquota_ufs(quota_t **quota, quota_mnt_t *m)
{
	return NULL;
}

static quota_t *
getquota_vxfs(quota_t **quota, quota_mnt_t *m)
{
	return NULL;
}

static quota_t *
getquota_zfs(quota_t **quota, quota_mnt_t *m)
{
	return NULL;
}

/* *** *** ***   global functions   *** *** *** */

int
quota_fs_issupported(const char *fsname)
{
	if(!strcmp(fsname, "ext2")
	|| !strcmp(fsname, "ext3")
	|| !strcmp(fsname, "ufs")
	|| !strcmp(fsname, "vxfs")
	|| !strcmp(fsname, "zfs"))
	{
		return EXIT_SUCCESS;
	} else {
#if 0
		DBG("%s filesystem not supported", fsname);
#endif
		return EXIT_FAILURE;
	}
} /* int quota_fs_issupported(const char *fsname) */

int
quota_fs_isnfs(const char *fsname)
{
	if(!strcmp(fsname, "nfs") || !strcmp(fsname, "nfs4")) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
} /* int quota_fs_isnfs(const char *fsname) */

void
quota_fs_freequota(quota_t *quota)
{
	quota_t *q = quota, *prev = NULL;

DBG("x");
	while(q != NULL) {
		while(q->next != NULL) {
			prev = q;
			q = q->next;
		}
		if(prev != NULL) {
			prev->next = NULL;
		}
		free(q->type);
		free(q->name);
		free(q->id);
		free(q->dir);
		free(q);
		prev = NULL;
		if(q != quota) {
			q = quota;
		} else {
			q = NULL;
		}
	} /* while(q != NULL) */
} /* void quota_fs_freequota(quota_t *quota) */

quota_t *
quota_fs_getquota(quota_t **quota, quota_mnt_t *mnt)
{
	quota_mnt_t *m = mnt;
	quota_t *q = NULL;

	*quota = NULL;
	while(m != NULL) {
		switch(qft(m->type)) {
		  case QFT_EXT3: 
			q = getquota_ext3(&q, m);
			break;
		  case QFT_EXT2:
			q = getquota_ext2(&q, m);
			break;
		  case QFT_UFS:
			q = getquota_ufs(&q, m);
			break;
		  case QFT_VXFS:
			q = getquota_vxfs(&q, m);
			break;
		  case QFT_ZFS:
			q = getquota_zfs(&q, m);
			break;
		}
		if(q != NULL) {   /* found some quotas */
			DBG("\ttype: %s", (*quota)->type);
			DBG("\tname: %s", (*quota)->name);
			DBG("\tid: %s", (*quota)->id);
			DBG("\tdir: %s", (*quota)->dir);
			DBG("\tblocks: %llu (%lld/%lld) %llu %llu",
				(*quota)->blocks, (*quota)->bquota, (*quota)->blimit,
				(*quota)->bgrace, (*quota)->btimeleft);
			DBG("\tinodes: %llu (%lld/%lld) %llu %llu",
				(*quota)->inodes, (*quota)->iquota, (*quota)->ilimit,
				(*quota)->igrace, (*quota)->itimeleft);

			if(*quota == NULL) {   /* not init yet */
DBG("a");
				*quota = q;    /* init */
			} else {   /* we have some quotas already */
DBG("b");
				quota_t *t = *quota;
				/* goto last entry */
				while(t->next != NULL) {
					t = t->next;
DBG("c");
				}
				t->next = q;   /* set next pointer */
			}
		}
DBG("z");
		m = m->next;
	} /* while(m != NULL) */

	return(*quota);
} /* quota_t *quota_fs_getquota(quota_t **quota, quota_mnt_t *mnt) */

