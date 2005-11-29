/**
 * collectd - src/quota_mntopt.h
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

#if !COLLECTD_QUOTA_MNTOPT_H
#define COLLECTD_QUOTA_MNTOPT_H 1

#include "common.h"

/* filesystem type */
#ifndef MNTTYPE_AUTOFS
#define MNTTYPE_AUTOFS          "autofs"   /* Automount mountpoint */
#endif
#ifndef MNTTYPE_CAPIFS
#define MNTTYPE_CAPIFS          "capifs"   /* */
#endif
#ifndef MNTTYPE_CRAMFS
#define MNTTYPE_CRAMFS          "cramfs"   /* */
#endif
#ifndef MNTTYPE_DEVPTS
#define MNTTYPE_DEVPTS          "devpts"   /* */
#endif
#ifndef MNTTYPE_EXT2
#define MNTTYPE_EXT2            "ext2"     /* 2nd Extended file system */
#endif
#ifndef MNTTYPE_EXT3
#define MNTTYPE_EXT3            "ext3"     /* ext2 + journaling */
#endif
#ifndef MNTTYPE_FUSE
#define MNTTYPE_FUSE            "fuse"     /* */
#endif
#ifndef MNTTYPE_HSFS
#define MNTTYPE_HSFS            "hsfs"     /* */
#endif
#ifndef MNTTYPE_ISO9660
#define MNTTYPE_ISO9660         "iso9660"  /* */
#endif
#ifndef MNTTYPE_JFS
#define MNTTYPE_JFS             "jfs"      /* JFS file system */
#endif
#ifndef MNTTYPE_MINIX
#define MNTTYPE_MINIX           "minix"    /* MINIX file system */
#endif
#ifndef MNTTYPE_NFS
#define MNTTYPE_NFS             "nfs"      /* */
#endif
#ifndef MNTTYPE_NFS4
#define MNTTYPE_NFS4            "nfs4"     /* NFSv4 filesystem */
#endif
#ifndef MNTTYPE_NTFS
#define MNTTYPE_NTFS            "ntfs"     /* */
#endif
#ifndef MNTTYPE_PROC
#define MNTTYPE_PROC            "proc"     /* */
#endif
#ifndef MNTTYPE_RAMFS
#define MNTTYPE_RAMFS           "ramfs"    /* */
#endif
#ifndef MNTTYPE_ROMFS
#define MNTTYPE_ROMFS           "romfs"    /* */
#endif
#ifndef MNTTYPE_RELAYFS
#define MNTTYPE_RELAYFS         "relayfs"  /* */
#endif
#ifndef MNTTYPE_REISER
#define MNTTYPE_REISER          "reiserfs" /* Reiser file system */
#endif
#ifndef MNTTYPE_SYSFS
#define MNTTYPE_SYSFS           "sysfs"    /* */
#endif
#ifndef MNTTYPE_TMPFS
#define MNTTYPE_TMPFS           "tmpfs"    /* */
#endif
#ifndef MNTTYPE_USBFS
#define MNTTYPE_USBFS           "usbfs"    /* */
#endif
#ifndef MNTTYPE_UDF
#define MNTTYPE_UDF             "udf"      /* OSTA UDF file system */
#endif
#ifndef MNTTYPE_UFS
#define MNTTYPE_UFS             "ufs"      /* UNIX file system */
#endif
#ifndef MNTTYPE_XFS
#define MNTTYPE_XFS             "xfs"      /* SGI XFS file system */
#endif
#ifndef MNTTYPE_VFAT
#define MNTTYPE_VFAT            "vfat"     /* */
#endif
#ifndef MNTTYPE_ZFS
#define MNTTYPE_ZFS             "zfs"      /* */
#endif

/* mount options */
#ifndef MNTOPT_RO
#define MNTOPT_RO               "ro"            /* */
#endif
#ifndef MNTOPT_RQ
#define MNTOPT_RQ               "rq"            /* */
#endif
#ifndef MNTOPT_PUBLIC
#define MNTOPT_PUBLIC           "public"        /* */
#endif
#ifndef MNTOPT_NOQUOTA
#define MNTOPT_NOQUOTA          "noquota"       /* don't enforce quota */
#endif
#ifndef MNTOPT_QUOTA
#define MNTOPT_QUOTA            "quota"         /* enforce user quota */
#endif
#ifndef MNTOPT_USRQUOTA
#define MNTOPT_USRQUOTA         "usrquota"      /* enforce user quota */
#endif
#ifndef MNTOPT_USRJQUOTA
#define MNTOPT_USRJQUOTA        "usrjquota"     /* enforce user quota */
#endif
#ifndef MNTOPT_GRPQUOTA
#define MNTOPT_GRPQUOTA         "grpquota"      /* enforce group quota */
#endif
#ifndef MNTOPT_GRPJQUOTA
#define MNTOPT_GRPJQUOTA        "grpjquota"     /* enforce group quota */
#endif
#ifndef MNTOPT_RSQUASH
#define MNTOPT_RSQUASH          "rsquash"       /* root as ordinary user */
#endif
#ifndef MNTOPT_BIND
#define MNTOPT_BIND             "bind"          /* binded mount */
#endif
#ifndef MNTOPT_LOOP
#define MNTOPT_LOOP             "loop"          /* loopback mount */
#endif
#ifndef MNTOPT_JQFMT
#define MNTOPT_JQFMT            "jqfmt"         /* journaled quota format */
#endif

#endif /* !COLLECTD_QUOTA_MNTOPT_H */

