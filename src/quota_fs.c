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

#if HAVE_PWD_H
# include <pwd.h>
#endif
#if HAVE_GRP_H
# include <grp.h>
#endif
#if HAVE_SYS_QUOTA_H
# include <sys/quota.h>
#endif



#define MY_BLOCKSIZE 1024
/* *** *** ***   prototypes of local functions   *** *** *** */



static quota_t *getquota_ext3(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ext3_v1(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ext3_v2(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_ufs(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_vxfs(quota_t **quota, quota_mnt_t *m);
static quota_t *getquota_zfs(quota_t **quota, quota_mnt_t *m);



/* *** *** ***   local functions   *** *** *** */



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
		DBG("quotactl (Q_GETFMT, USRQUOTA) returned -1 on"
			" %s: %s", m->device, strerror(errno));
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
	quota_t *q = *quota;
	int i;
	char buf[100];
#if HAVE_GETPWUID
	struct passwd *passwd;
#endif
#if HAVE_GETGRGID
	struct group *group;
#endif
#if HAVE_QUOTACTL
	struct dqinfo dqiusr, dqigrp;
#endif

DBG("start");

#if HAVE_QUOTACTL
	if(m->opts & QMO_USRQUOTA) {
		if(quotactl(QCMD(Q_GETINFO, USRQUOTA), m->device,
			0, (void *)&dqiusr) == -1)
		{
			DBG("quotactl (Q_GETINFO, USRQUOTA) returned -1 on"
				" %s: %s", m->device, strerror(errno));
			m->opts &= ~QMO_USRQUOTA;
			DBG("\tusrquota switched off");
		}
	}
	if(m->opts & QMO_USRQUOTA) {
		if(quotactl(QCMD(Q_SYNC, USRQUOTA), m->device, 0, NULL) == -1)
		{
			DBG("quotactl (Q_SYNC, USRQUOTA) returned -1 on"
				" %s: %s", m->device, strerror(errno));
			m->opts &= ~QMO_USRQUOTA;
			DBG("\tusrquota switched off");
		}
	}
	if(m->opts & QMO_GRPQUOTA) {
		if(quotactl(QCMD(Q_GETINFO, GRPQUOTA), m->device,
			0, (void *)&dqigrp) == -1)
		{
			DBG("quotactl (Q_GETINFO, GRPQUOTA) returned -1 on"
				" %s: %s", m->device, strerror(errno));
			m->opts &= ~QMO_GRPQUOTA;
			DBG("\tgrpquota switched off");
		}
	}
	if(m->opts & QMO_GRPQUOTA) {
		if(quotactl(QCMD(Q_SYNC, GRPQUOTA), m->device, 0, NULL) == -1)
		{
			DBG("quotactl (Q_SYNC, GRPQUOTA) returned -1 on"
				" %s: %s", m->device, strerror(errno));
			m->opts &= ~QMO_GRPQUOTA;
			DBG("\tgrpquota switched off");
		}
	}
#endif /* HAVE_QUOTACTL */

	if(m->opts == QMO_NONE) {
		return NULL;
	}

#if HAVE_QUOTACTL
	if(m->opts & QMO_USRQUOTA) {
		for(i=0; i<1000; i++) {
			struct dqblk dqb;
			if(quotactl(QCMD(Q_GETQUOTA, USRQUOTA),
				m->device, i, (void *)&dqb) == -1)
			{
#if 0
				DBG("quotactl (Q_GETQUOTA, USRQUOTA)"
					" returned -1 on %d %s: %s",
					i, m->device, strerror(errno));
#endif
				continue;
			}
			DBG("quotactl (Q_GETQUOTA, USRQUOTA)"
				" returned ok on %d %s",
				i, m->device);
			if(*quota == NULL) {
				*quota = (quota_t *)smalloc(sizeof(quota_t));
				q = *quota;
			} else {
				q->next = (quota_t *)smalloc(sizeof(quota_t));
				q = q->next;
			}
			q->type = sstrdup(QFT_USRQUOTA);
			(void)snprintf(buf, 100, "%ld", (long)i);
#if HAVE_GETPWUID
			passwd = getpwuid((uid_t)i);
			q->name = sstrdup(passwd->pw_name);
#else
			q->name = sstrdup(buf);
#endif
			q->id = sstrdup(buf);
			q->dir = sstrdup(m->dir);
			q->blocks = dqb.dqb_curspace;
			q->bquota = dqb.dqb_bsoftlimit << 10;
			q->blimit = dqb.dqb_bhardlimit << 10;
			q->bgrace = dqiusr.dqi_bgrace;
			q->btimeleft = dqb.dqb_btime;
			q->inodes = dqb.dqb_curinodes;
			q->iquota = dqb.dqb_isoftlimit;
			q->ilimit = dqb.dqb_ihardlimit;
			q->igrace = dqiusr.dqi_igrace;
			q->itimeleft = dqb.dqb_itime;
			q->next = NULL;
		} /* for(i=0; i<1000; i++) */
	} /* if(m->opts & QMO_USRQUOTA) */

	if(m->opts & QMO_GRPQUOTA) {
		for(i=0; i<1000; i++) {
			struct dqblk dqb;
			if(quotactl(QCMD(Q_GETQUOTA, GRPQUOTA),
				m->device, i, (void *)&dqb) == -1)
			{
#if 0
				DBG("quotactl (Q_GETQUOTA, GRPQUOTA)"
					" returned -1 on %d %s: %s",
					i, m->device, strerror(errno));
#endif
				continue;
			}
			DBG("quotactl (Q_GETQUOTA, GRPQUOTA)"
				" returned ok on %d %s",
				i, m->device);
			if(*quota == NULL) {
				*quota = (quota_t *)smalloc(sizeof(quota_t));
				q = *quota;
			} else {
				q->next = (quota_t *)smalloc(sizeof(quota_t));
				q = q->next;
			}
			q->type = sstrdup(QFT_GRPQUOTA);
			(void)snprintf(buf, 100, "%ld", (long)i);
#if HAVE_GETGRGID
			group = getgrgid((gid_t)i);
			q->name = sstrdup(group->gr_name);
#else
			q->name = sstrdup(buf);
#endif
			q->id = sstrdup(buf);
			q->dir = sstrdup(m->dir);
			q->blocks = dqb.dqb_curspace;
			q->bquota = dqb.dqb_bsoftlimit << 10;
			q->blimit = dqb.dqb_bhardlimit << 10;
			q->bgrace = dqigrp.dqi_bgrace;
			q->btimeleft = dqb.dqb_btime;
			q->inodes = dqb.dqb_curinodes;
			q->iquota = dqb.dqb_isoftlimit;
			q->ilimit = dqb.dqb_ihardlimit;
			q->igrace = dqigrp.dqi_igrace;
			q->itimeleft = dqb.dqb_itime;
			q->next = NULL;
		} /* for(i=0; i<1000; i++) */
	} /* if(m->opts & QMO_GRPQUOTA) */
#endif /* HAVE_QUOTACTL */

DBG("end");
	return q;
}



static quota_t *
getquota_ext3_v2(quota_t **quota, quota_mnt_t *m)
{
	return getquota_ext3_v1(quota, m);
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



#if QUOTA_PLUGIN_DEBUG
void
quota_fs_printquota_dbg(quota_t *q)
{
	DBG("start");
	while(q != NULL) {
		DBG("\ttype: %s", q->type);
		DBG("\tname: %s", q->name);
		DBG("\tid: %s", q->id);
		DBG("\tdir: %s", q->dir);
		DBG("\tblocks: %llu (%lld/%lld) %lld %lld",
			q->blocks, q->bquota, q->blimit,
			q->bgrace, q->btimeleft);
		DBG("\tinodes: %llu (%lld/%lld) %lld %lld",
			q->inodes, q->iquota, q->ilimit,
			q->igrace, q->itimeleft);
		q = q->next;
	} /* while(q != NULL) */
	DBG("end");
} /* void quota_fs_printquota_dbg(quota_t *quota) */
#else
void
quota_fs_printquota_dbg(quota_t *q)
{
}
#endif /* QUOTA_PLUGIN_DEBUG */



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

	while(q != NULL) {
		while(q->next != NULL) {
			prev = q;
			q = q->next;
		}
		if(prev != NULL) {
			prev->next = NULL;
		}
		sfree(q->type);
		sfree(q->name);
		sfree(q->id);
		sfree(q->dir);
		sfree(q);
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
	quota_t *q = NULL, *qlast = NULL;

	while(m != NULL) {
		switch(quota_mnt_type(m->type)) {
		  case QMT_EXT2:
		  case QMT_EXT3: 
			qlast = getquota_ext3(&q, m);
			break;
		  case QMT_UFS:
			qlast = getquota_ufs(&q, m);
			break;
		  case QMT_VXFS:
			qlast = getquota_vxfs(&q, m);
			break;
		  case QMT_ZFS:
			qlast = getquota_zfs(&q, m);
			break;
		}
		if(qlast != NULL) {   /* found some quotas */
			if(*quota == NULL) {   /* not init yet */
				*quota = q;    /* init */
			}
			q = qlast;
		} /* if(qlast != NULL) */
		m = m->next;
	} /* while(m != NULL) */

	return(*quota);
} /* quota_t *quota_fs_getquota(quota_t **quota, quota_mnt_t *mnt) */

