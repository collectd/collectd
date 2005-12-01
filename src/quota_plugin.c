/**
 * collectd - src/quota_plugin.c
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
#include "plugin.h"

#include "quota_debug.h"
#include "quota_common.h"
#include "quota_mnt.h"
#include "quota_fs.h"
#include "quota_plugin.h"

#define MODULE_NAME "quota"

/* *** *** ***   local constants   *** *** *** */

static const char *quota_filename_template = MODULE_NAME "-%s.rrd";

static char *quota_ds_def[] =
{
	"DS:blocks:GAUGE:25:0:U",
	"DS:block_quota:GAUGE:25:-1:U",
	"DS:block_limit:GAUGE:25:-1:U",
	"DS:block_grace:GAUGE:25:-1:U",
	"DS:block_timeleft:GAUGE:25:-1:U",
	"DS:inodes:GAUGE:25:0:U",
	"DS:inode_quota:GAUGE:25:-1:U",
	"DS:inode_limit:GAUGE:25:-1:U",
	"DS:inode_grace:GAUGE:25:-1:U",
	"DS:inode_timeleft:GAUGE:25:-1:U",
	NULL
};
static const int quota_ds_num = 10;

/* *** *** ***   local functions   *** *** *** */

#define BUFSIZE 1024
static void
quota_submit(quota_t *q)
{
	char buf[BUFSIZE];
	int r;
	char *name, *n;

	r = snprintf(buf, BUFSIZE,
		"%u:%llu:%lld:%lld:%llu:%llu:%llu:%lld:%lld:%llu:%llu",
                (unsigned int)curtime,
                q->blocks, q->bquota, q->blimit, q->bgrace, q->btimeleft,
                q->inodes, q->iquota, q->ilimit, q->igrace, q->itimeleft);
	if(r < 1 || r >= BUFSIZE) {
		DBG("failed");
		return;
	}
	n = name = (char *)smalloc(strlen(q->type) + 1 + strlen(q->dir)
	+ 1 + strlen(q->name) + 1 + strlen(q->id));
	sstrncpy(n, q->type, strlen(q->type)+1);
	n += strlen(q->type);
	sstrncpy(n, "-", 1+1);
	n += 1;
	sstrncpy(n, q->name, strlen(q->name)+1);
	n += strlen(q->name);
	sstrncpy(n, "-", 1+1);
	n += 1;
	sstrncpy(n, q->id, strlen(q->id)+1);
	n += strlen(q->id);
	sstrncpy(n, "-", 1+1);
	n += 1;
	sstrncpy(n, q->dir, strlen(q->dir)+1);
	n += strlen(q->dir);
	n = name;
	/* translate '/' -> '_' */
	while(*n != '\0') {
		if(*n == '/') {
			*n = '_';
		}
		n++;
	}

	DBG("rrd file: %s-%s", MODULE_NAME, name);
	plugin_submit(MODULE_NAME, name, buf);
}
#undef BUFSIZE

/* *** *** ***   local plugin functions   *** *** *** */

static void
quota_init(void)
{
	DBG_INIT("quota debug file opened.");
}

static void
quota_read(void)
{
	quota_mnt_t *list = NULL, *l = NULL;
	quota_t *quota = NULL, *q = NULL;

	l = quota_mnt_getlist(&list);
	DBG("local mountpoints:");
	while(l != NULL) {
		DBG("\tdir: %s", l->dir);
		DBG("\tdevice: %s", l->device);
		DBG("\topts: %s (0x%04x)",
			(l->opts == QMO_NONE) ? "-"
			: (l->opts == QMO_USRQUOTA) ? "USRQUOTA"
			: (l->opts == QMO_GRPQUOTA) ? "GRPQUOTA"
			: (l->opts == (QMO_USRQUOTA|QMO_GRPQUOTA)) ?
				"USRQUOTA GRPQUOTA" : " ??? ",
			l->opts);
		l = l->next;
		if(l != NULL) {
			DBG("\t-- ");
		}
	}
	DBG("\t== ");

	q = quota_fs_getquota(&quota, list);
	DBG("quotas:");
	while(q != NULL) {
		DBG("\ttype: %s", q->type);
		DBG("\tname: %s", q->name);
		DBG("\tid: %s", q->id);
		DBG("\tdir: %s", q->dir);
		DBG("\tblocks: %llu (%lld/%lld) %llu %llu",
			q->blocks, q->bquota, q->blimit,
			q->bgrace, q->btimeleft);
		DBG("\tinodes: %llu (%lld/%lld) %llu %llu",
			q->inodes, q->iquota, q->ilimit,
			q->igrace, q->itimeleft);
		quota_submit(q);
		q = q->next;
		if(q != NULL) {
			DBG("\t-- ");
		}
	}
	DBG("\t== ");

	quota_fs_freequota(quota);
	quota_mnt_freelist(list);
}

static void
quota_write(char *host, char *inst, char *val)
{
	char file[512];
	int r;

	r = snprintf(file, 512, quota_filename_template, inst);
	if(r < 1 || r >= 512) {
		DBG("failed");
		return;
	}

	DBG("host: %s", host);
	DBG("file: %s", file);
	DBG("val: %s", val);
	DBG("quota_ds_def: %s", *quota_ds_def);
	DBG("quota_ds_num: %d", quota_ds_num);
	rrd_update_file(host, file, val, quota_ds_def, quota_ds_num);
}

/* *** *** ***   global functions   *** *** *** */

void
module_register(void)
{
	plugin_register(MODULE_NAME, quota_init, quota_read, quota_write);
}

