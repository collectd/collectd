/**
 * collectd - src/fscache.c
 * Copyright (C) 2009 Edward "Koko" Konetzko
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Edward "Koko" Konetzko <konetzed at quixoticagony.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include <stdio.h>  /* a header needed for FILE */
#include <string.h> /* a header needed for scanf function */
#include <stdlib.h> /* used for atoi */


#if !KERNEL_LINUX
# error "This module only supports the Linux implementation of fscache"
#endif

#define BUFSIZE 1024

/*
see /proc/fs/fscache/stats
see Documentation/filesystems/caching/fscache.txt in linux kernel >= 2.6.30

This shows counts of a number of events that can happen in FS-Cache:

CLASS   EVENT   MEANING
======= ======= =======================================================
Cookies idx=N   Number of index cookies allocated
        dat=N   Number of data storage cookies allocated
        spc=N   Number of special cookies allocated
Objects alc=N   Number of objects allocated
        nal=N   Number of object allocation failures
        avl=N   Number of objects that reached the available state
        ded=N   Number of objects that reached the dead state
ChkAux  non=N   Number of objects that didn't have a coherency check
        ok=N    Number of objects that passed a coherency check
        upd=N   Number of objects that needed a coherency data update
        obs=N   Number of objects that were declared obsolete
Pages   mrk=N   Number of pages marked as being cached
        unc=N   Number of uncache page requests seen
Acquire n=N Number of acquire cookie requests seen
        nul=N   Number of acq reqs given a NULL parent
        noc=N   Number of acq reqs rejected due to no cache available
        ok=N    Number of acq reqs succeeded
        nbf=N   Number of acq reqs rejected due to error
        oom=N   Number of acq reqs failed on ENOMEM
Lookups n=N Number of lookup calls made on cache backends
        neg=N   Number of negative lookups made
        pos=N   Number of positive lookups made
        crt=N   Number of objects created by lookup
Updates n=N Number of update cookie requests seen
        nul=N   Number of upd reqs given a NULL parent
        run=N   Number of upd reqs granted CPU time
Relinqs n=N Number of relinquish cookie requests seen
        nul=N   Number of rlq reqs given a NULL parent
        wcr=N   Number of rlq reqs waited on completion of creation
AttrChg n=N Number of attribute changed requests seen
        ok=N    Number of attr changed requests queued
        nbf=N   Number of attr changed rejected -ENOBUFS
        oom=N   Number of attr changed failed -ENOMEM
        run=N   Number of attr changed ops given CPU time
Allocs  n=N Number of allocation requests seen
        ok=N    Number of successful alloc reqs
        wt=N    Number of alloc reqs that waited on lookup completion
        nbf=N   Number of alloc reqs rejected -ENOBUFS
        ops=N   Number of alloc reqs submitted
        owt=N   Number of alloc reqs waited for CPU time
Retrvls n=N Number of retrieval (read) requests seen
        ok=N    Number of successful retr reqs
        wt=N    Number of retr reqs that waited on lookup completion
        nod=N   Number of retr reqs returned -ENODATA
        nbf=N   Number of retr reqs rejected -ENOBUFS
        int=N   Number of retr reqs aborted -ERESTARTSYS
        oom=N   Number of retr reqs failed -ENOMEM
        ops=N   Number of retr reqs submitted
        owt=N   Number of retr reqs waited for CPU time
Stores  n=N Number of storage (write) requests seen
        ok=N    Number of successful store reqs
        agn=N   Number of store reqs on a page already pending storage
        nbf=N   Number of store reqs rejected -ENOBUFS
        oom=N   Number of store reqs failed -ENOMEM
        ops=N   Number of store reqs submitted
        run=N   Number of store reqs granted CPU time
Ops pend=N  Number of times async ops added to pending queues
        run=N   Number of times async ops given CPU time
        enq=N   Number of times async ops queued for processing
        dfr=N   Number of async ops queued for deferred release
        rel=N   Number of async ops released
        gc=N    Number of deferred-release async ops garbage collected

63 events to collect in 13 groups
*/
static void fscache_submit (const char *section, const char *name,
        value_t value)
{
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &value;
    vl.values_len = 1;

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "fscache", sizeof (vl.plugin));
    sstrncpy(vl.plugin_instance, section, sizeof (vl.plugin_instance));
    sstrncpy(vl.type, "fscache_stat", sizeof(vl.type));
    sstrncpy(vl.type_instance, name, sizeof(vl.type_instance));

    plugin_dispatch_values (&vl);
}

static void fscache_read_stats_file (FILE *fh)
{
    char section[DATA_MAX_NAME_LEN];
    size_t section_len;

    char linebuffer[BUFSIZE];

/*
 *  cat /proc/fs/fscache/stats
 *      FS-Cache statistics
 *      Cookies: idx=2 dat=0 spc=0
 *      Objects: alc=0 nal=0 avl=0 ded=0
 *      ChkAux : non=0 ok=0 upd=0 obs=0
 *      Pages  : mrk=0 unc=0
 *      Acquire: n=2 nul=0 noc=0 ok=2 nbf=0 oom=0
 *      Lookups: n=0 neg=0 pos=0 crt=0
 *      Updates: n=0 nul=0 run=0
 *      Relinqs: n=0 nul=0 wcr=0
 *      AttrChg: n=0 ok=0 nbf=0 oom=0 run=0
 *      Allocs : n=0 ok=0 wt=0 nbf=0
 *      Allocs : ops=0 owt=0
 *      Retrvls: n=0 ok=0 wt=0 nod=0 nbf=0 int=0 oom=0
 *      Retrvls: ops=0 owt=0
 *      Stores : n=0 ok=0 agn=0 nbf=0 oom=0
 *      Stores : ops=0 run=0
 *      Ops    : pend=0 run=0 enq=0
 *      Ops    : dfr=0 rel=0 gc=0
 */

    /* Read file line by line */
    while (fgets (linebuffer, sizeof (linebuffer), fh) != NULL)
    {
        char *lineptr;
        char *fields[32];
        int fields_num;
        int i;

        /* Find the colon and replace it with a null byte */
        lineptr = strchr (linebuffer, ':');
        if (lineptr == NULL)
            continue;
        *lineptr = 0;
        lineptr++;

        /* Copy and clean up the section name */
        sstrncpy (section, linebuffer, sizeof (section));
        section_len = strlen (section);
        while ((section_len > 0) && isspace ((int) section[section_len - 1]))
        {
            section_len--;
            section[section_len] = 0;
        }
        if (section_len <= 0)
            continue;

        fields_num = strsplit (lineptr, fields, STATIC_ARRAY_SIZE (fields));
        if (fields_num <= 0)
            continue;

        for (i = 0; i < fields_num; i++)
        {
            char *field_name;
            char *field_value_str;
            value_t field_value_cnt;
            int status;

            field_name = fields[i];
            assert (field_name != NULL);

            field_value_str = strchr (field_name, '=');
            if (field_value_str == NULL)
                continue;
            *field_value_str = 0;
            field_value_str++;

            status = parse_value (field_value_str, &field_value_cnt,
                    DS_TYPE_DERIVE);
            if (status != 0)
                continue;

            fscache_submit (section, field_name, field_value_cnt);
        }
    } /* while (fgets) */
} /* void fscache_read_stats_file */

static int fscache_read (void){
    FILE *fh;
    fh = fopen("/proc/fs/fscache/stats", "r");
    if (fh != NULL){
        fscache_read_stats_file(fh);
        fclose(fh);

    }else{
        printf("cant open file\n");
        return (-1);
    }
    return (0);
}

void module_register (void)
{
    plugin_register_read ("fscache", fscache_read);
} /* void module_register */

/* vim: set sw=4 sts=4 et : */
