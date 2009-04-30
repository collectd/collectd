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

static int fscache_number_of_values = 62;

struct fscache_values {
    char name[20];
    unsigned long long value;
};  /*end struct fscache_values */

static void fscache_submit(struct fscache_values submit_values[]){
/*submit logic goes here!!!!!*/

    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;
    int i;

    vl.values = values;
    vl.values_len = 1;
    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, "fscache", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, "fscache",
            sizeof(vl.plugin_instance));
    sstrncpy(vl.type, "fscache_stat", sizeof(vl.type));

    for (i = 0; i < fscache_number_of_values; i++){
        values[0].counter = submit_values[i].value;
        sstrncpy(vl.type_instance, submit_values[i].name,
                sizeof(vl.type_instance));
        DEBUG("%s-%s/fscache-%s = %llu",
                vl.plugin, vl.plugin_instance,
                vl.type_instance, submit_values[i].value);
        plugin_dispatch_values(&vl);
    }
}

static void fscache_read_stats_file(FILE *fh){
    int forcount = 0;
    int count = 0;
    int start = 0;
    int numfields = 0;
    int valuecounter = 0;
    char filebuffer[BUFSIZE];
    char valuebuffer[BUFSIZE];
    char valuename[BUFSIZE];
    char valuevalue[BUFSIZE];
    char sectionbuffer[BUFSIZE];
    char tempstring[BUFSIZE];
    char *ptrfilebuffer = filebuffer;
    char *position = NULL;
    char *fields[fscache_number_of_values];
    struct fscache_values values[fscache_number_of_values];

    /* Read file line by line */
    while( fgets( filebuffer, BUFSIZE, fh)!= NULL){
        /*skip first line and split file on : */
        if (count != 0){
            position = strchr(filebuffer,':');
            start = strlen(filebuffer) - 1;
            filebuffer[start] = '\0';
            /* : is located at value 8, +1 to remove space */
            strncpy(valuebuffer, ptrfilebuffer+9, start);
            /*store section info for later */
            strncpy(sectionbuffer, ptrfilebuffer, position - filebuffer);
            position = strchr(sectionbuffer, ' ');
            if (position != NULL){
                sectionbuffer[position-sectionbuffer] = '\0';
            }

            /*split rest of line by space and then split that via =*/
            numfields = strsplit ( valuebuffer, fields,
                    fscache_number_of_values);
            for( forcount = 0; forcount < numfields; forcount++ ){
                char *field = fields[forcount];
                memset(valuename,'\0',sizeof(valuename));
                position = strchr(field, '=');
                /* Test to see if we actually have a metric to split on
                 * and assign the values to the struct array */
                if (position != NULL){
                    strncpy(valuename, field, position - field);
                    strncpy(valuevalue, field + (position - field + 1),
                            strlen(field));
                    memset(tempstring,'\0',sizeof(tempstring));
                    strncat(tempstring,sectionbuffer,sizeof(tempstring));
                    strncat(tempstring,"_",1);
                    strncat(tempstring,valuename, sizeof(tempstring));
                    memset(values[valuecounter].name,'\0',
                            sizeof(values[valuecounter].name));
                    strncpy(values[valuecounter].name,tempstring,
                            sizeof(values[valuecounter].name));
                    values[valuecounter].value = atoll(valuevalue);
                }
            valuecounter++;
            }
        }
    count++;
    }
    fscache_submit(values);
}

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

