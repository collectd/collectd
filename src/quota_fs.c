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
#include "quota_plugin.h"
#include "quota_mnt.h"
#include "quota_fs.h"

/* *** *** ***   prototypes of local functions   *** *** *** */

static int qft(const char *type);
static void getquota_ext3(quota_t **quota, quota_mnt_t *m);
static void getquota_ext2(quota_t **quota, quota_mnt_t *m);
static void getquota_ufs(quota_t **quota, quota_mnt_t *m);
static void getquota_vxfs(quota_t **quota, quota_mnt_t *m);
static void getquota_zfs(quota_t **quota, quota_mnt_t *m);

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

static void
getquota_ext3(quota_t **quota, quota_mnt_t *m)
{
#if HAVE_QUOTACTL
	int fmt;
	if(quotactl(QCMD(Q_GETFMT, type), dev, 0, (void *)&fmt) == -1) {
		DBG("quotactl returned -1: %s", strerror(errno));
		*quota = NULL;
	}
#endif
#if 0
	int kern_quota_on(const char *dev, int type, int fmt)
{
        /* Check whether quota is turned on... */
        if (kernel_iface == IFACE_GENERIC) {
                int actfmt;

                if (quotactl(QCMD(Q_GETFMT, type), dev, 0, (void *)&actfmt) < 0)
                        return -1;
                actfmt = kern2utilfmt(actfmt);
                if (actfmt >= 0 && (fmt == -1 || (1 << actfmt) & fmt))
                        return actfmt;
                return -1;
        }
        if ((fmt & (1 << QF_VFSV0)) && v2_kern_quota_on(dev, type))     /* New quota format */
                return QF_VFSV0;
        if ((fmt & (1 << QF_XFS)) && xfs_kern_quota_on(dev, type))      /* XFS quota format */
                return QF_XFS;
        if ((fmt & (1 << QF_VFSOLD)) && v1_kern_quota_on(dev, type))    /* Old quota format */
                return QF_VFSOLD;
        return -1;
}
#endif


#if 0
	quotaio.c
	kernfmt = kern_quota_on(h->qh_quotadev, type, fmt == -1 ? kernel_formats : (1 << fmt));
	                if (kernfmt >= 0) {
                        h->qh_io_flags |= IOFL_QUOTAON;
                        fmt = kernfmt;  /* Default is kernel used format */
                }
if ((fmt = get_qf_name(mnt, type, (fmt == -1) ? ((1 << QF_VFSOLD) | (1 << QF_VFSV0)) : (1 << fmt),
            (!QIO_ENABLED(h) || flags & IOI_OPENFILE) ? NF_FORMAT : 0, &qfname)) < 0) {
errstr(_("Quota file not found or has wrong format.\n"));
                goto out_handle;
       if (!QIO_ENABLED(h) || flags & IOI_OPENFILE) {  /* Need to open file? */
                /* We still need to open file for operations like 'repquota' */
                if ((fd = open(qfname, QIO_RO(h) ? O_RDONLY : O_RDWR)) < 0) {
                        errstr(_("Can't open quotafile %s: %s\n"),
                                qfname, strerror(errno));
                        goto out_handle;
                }
                flock(fd, QIO_RO(h) ? LOCK_SH : LOCK_EX);
                /* Init handle */
                h->qh_fd = fd;
                h->qh_fmt = fmt;
        }
        else {
                h->qh_fd = -1;
                h->qh_fmt = fmt;
        }
        free(qfname);   /* We don't need it anymore */
        qfname = NULL;
       if (h->qh_fmt == QF_VFSOLD)
                h->qh_ops = &quotafile_ops_1;
        else if (h->qh_fmt == QF_VFSV0)
                h->qh_ops = &quotafile_ops_2;
        memset(&h->qh_info, 0, sizeof(h->qh_info));

        if (h->qh_ops->init_io && h->qh_ops->init_io(h) < 0) {
                errstr(_("Can't initialize quota on %s: %s\n"), h->qh_quotadev, strerror(errno));
                goto out_lock;
        }
        return h;
out_lock:
        if (fd != -1)
                flock(fd, LOCK_UN);
out_handle:
        if (qfname)
                free(qfname);
        free(h);
	return NULL;
#endif
	*quota = NULL;
}

static void
getquota_ext2(quota_t **quota, quota_mnt_t *m)
{
	*quota = NULL;
}

static void
getquota_ufs(quota_t **quota, quota_mnt_t *m)
{
	*quota = NULL;
}

static void
getquota_vxfs(quota_t **quota, quota_mnt_t *m)
{
	*quota = NULL;
}

static void
getquota_zfs(quota_t **quota, quota_mnt_t *m)
{
	*quota = NULL;
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
}

int
quota_fs_isnfs(const char *fsname)
{
	if(!strcmp(fsname, "nfs") || !strcmp(fsname, "nfs4")) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}

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
		free(q->type);
		free(q->name);
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

	while(m != NULL) {
		switch(qft(m->type)) {
		  case QFT_EXT3:
			getquota_ext3(quota, m);
			break;
		  case QFT_EXT2:
			getquota_ext2(quota, m);
			break;
		  case QFT_UFS:
			getquota_ufs(quota, m);
			break;
		  case QFT_VXFS:
			getquota_vxfs(quota, m);
			break;
		  case QFT_ZFS:
			getquota_zfs(quota, m);
			break;
		}
		m = m->next;
	} /* while(l != NULL) */

	return(*quota);
}

