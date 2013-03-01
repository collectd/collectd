/**
 * collectd - src/jsonrpc_cb_topps.c
 * Copyright (C) 2012 Yves Mettier
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
 *   Yves Mettier <ymettier at free dot fr>
 **/

#include "common.h"
#include "plugin.h"
#include "jsonrpc.h"
#include <json/json.h>
#include <zlib.h>
#define OUTPUT_PREFIX_JSONRPC_CB_TOPPS "JSONRPC plugin (topps) : "

extern char toppsdatadir[];

static int mkpath_by_tm_and_num(char *buffer, size_t bufferlen, time_t tm, int n) /* {{{ */
{
        struct tm stm;
        int status;

        char timebuffer[25]; /* 2^64 is a 20-digits decimal number. So 25 should be enough */
        if (localtime_r (&tm, &stm) == NULL)
        {
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "localtime_r failed");
                return (-1);
        }
        strftime(timebuffer, sizeof(timebuffer), "%s", &stm);
        status = ssnprintf (buffer, bufferlen,
                        "%1$.2s/%1$.4s/ps-%1$.6s0000-%2$d.gz", timebuffer,n);
        if ((status < 1) || (status >= bufferlen)) {
                ERROR(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "Filename buffer too small (%s:%d)", __FILE__, __LINE__);
                return (-1);
        }
        return(0);

} /* }}} mkpath_by_tm_and_num */

static int check_if_file_contains_tm(gzFile *gzfh, const char *filename, time_t tm_start, int *err) { /* {{{ */
        /* Return 0 if tm_start is inside the file,
         *          or if an error occured (*err is not nul if an error occured)
         * Return -n if we should look before 
         * Return n if we should look after
         * n is min(|tm_start-begin|, |tm_end-begin|)
         */
        char line[4096];
        size_t l;
        time_t tm_last;
        time_t tm_first;

        *err = 0;

        /* Read version */
        if(NULL == gzgets(gzfh, line, sizeof(line))) {
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not read a line (%s:%d)", filename, __FILE__, __LINE__);
                goto check_if_file_contains_tm_read_failed;
        }
        for( l = strlen(line) - 1; l>0; l--) {
                if(line[l] == '\n') line[l] = '\0';
                else if(line[l] == '\r') line[l] = '\0';
                else break;
        }
        if(!strcmp(line, "Version 1.0")) {
                time_t tm1, tm2;
                /* Read 2nd line : last tm */
                if(NULL == gzgets(gzfh, line, sizeof(line))) {
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not read a line (%s:%d)", filename, __FILE__, __LINE__);
                        goto check_if_file_contains_tm_read_failed;
                }

                errno=0;
                tm_last = strtol(line, NULL, 10);
                if(0 != errno) {
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not convert '%s' to integer (%s:%d)", filename, line, __FILE__, __LINE__);
                        goto check_if_file_contains_tm_read_failed;
                }

                /* Read 3rd line : first tm (and start of the records) */
                if(NULL == gzgets(gzfh, line, sizeof(line))) {
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not read a line (%s:%d)", filename, __FILE__, __LINE__);
                        goto check_if_file_contains_tm_read_failed;
                }

                errno=0;
                tm_first = strtol(line, NULL, 10);
                if(0 != errno) {
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not convert '%s' to integer (%s:%d)", filename, line, __FILE__, __LINE__);
                        goto check_if_file_contains_tm_read_failed;
                }

                if((tm_start >= tm_first) && (tm_start <= tm_last)) return(0); /* tm_start is inside the file */
                tm1 = abs(tm_start - tm_first);
                tm2 = abs(tm_start - tm_last);
                tm1 = (tm1 < tm2)?tm1:tm2;
                tm1 = ((tm_start - tm_first) > 0) ? tm1 : -tm1;
                return(tm1);

        } else {
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : wrong version nomber (found '%s')", filename, line);
                goto check_if_file_contains_tm_read_failed;
        }
        *err = 1;
        return(0);

check_if_file_contains_tm_read_failed:
        *err = 2;
        return(0);

} /* }}} check_if_file_contains_tm */

static int check_path(const char *hostname, int tm_start, int tm_end, char *buffer, size_t bufferlen) /* {{{ */
{
        /* Path syntax where timestamp = AABBCCDDDD :
         * ${toppsdatadir}/${hostname}/AA/AABB/AABBCC0000-X.gz
         * Checking path means testing that the ${toppsdatadir}/${hostname}/AA/AABB directory exists.
         * If not, check with tm_margin.
         *
         * Start at tm_start. If tm_end < tm_start, search backward.
         */
        gzFile *gzfh=NULL;
        int offset = 0;
        int status;
        short file_found;
        time_t tm;
        int n=0;
        int best_distance;
        int max_distance;
        int best_n;
        int best_tm;
        int last_seen_n_low,last_seen_n_high;
        int last_seen_tm_low,last_seen_tm_high;
        int last_before_flush_n;
        int last_before_flush_tm;
        int flush_needed = 0;
        int flush_already_done = 0;
        short watchdog;
        int search_direction;

        if (toppsdatadir != NULL)
        {
                status = ssnprintf (buffer, bufferlen, "%s/", toppsdatadir);
                if ((status < 1) || (status >= bufferlen )) {
                        ERROR(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "Filename buffer too small (%s:%d)", __FILE__, __LINE__);
                        return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
                }
                offset += status;
        }
        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG toppsdatadir='%s' (%s:%d)", toppsdatadir, __FILE__, __LINE__);
        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG offset = %d (%s:%d)", offset, __FILE__, __LINE__);

        status = ssnprintf (buffer + offset, bufferlen - offset,
                        "%s/", hostname);
        if ((status < 1) || (status >= bufferlen - offset)) {
                ERROR(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "Filename buffer too small (%s:%d)", __FILE__, __LINE__);
                return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
        }
        offset += status;
        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG offset = %d (%s:%d)", offset, __FILE__, __LINE__);

        /* Start search */
        max_distance = abs(tm_start - tm_end); /* distance should be < max_distance */
        file_found = 0;
        best_distance = max_distance + 1;
        best_n = 0;
        best_tm = 0;
        last_seen_tm_high = 0;
        last_seen_tm_low = 0;
        last_seen_n_high = 0;
        last_seen_n_low = 0;
        search_direction = 1; /* positive value to go forward at first time */

        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG WE ARE SEARCHING FOR tm_start = '%d' max_distance = %d (%s:%d)", tm_start, max_distance, __FILE__, __LINE__);

        n = 0;
        tm = 10000 * (int)(tm_start / 10000);
        if(tm_start <= tm_end) tm -= 10000; /* if searching forward, search starts before tm_start. */
        //        else                   tm += 10000; /* if searching backward, search starts after tm_start */

#define WATCHDOGMAX 100 /* max number of cycles in this loop. Prevent from infinite loop if something is missing in this complex algo */
        for(watchdog = 0; watchdog < WATCHDOGMAX; watchdog++) { /* There are many cases to get out of this loop. See the many 'break' instructions */
                int local_err;

                if((0 == flush_already_done) && (2 == flush_needed)) {
                        /* Back to flush position */
                        tm = last_before_flush_tm;
                        n = last_before_flush_n;
                }

                if(mkpath_by_tm_and_num(buffer + offset, bufferlen - offset,tm, n)) {
                        return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
                }

                /* Try to open the file.
                 * Flush if necessary
                 */
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG tm = %ld filename = '%s' (%s:%d)", tm, buffer, __FILE__, __LINE__);
                if(NULL == (gzfh = gzopen(buffer, "r"))) {
                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG COULD NOT OPEN = '%s' (%s:%d)", buffer, __FILE__, __LINE__);
                        if((0 == flush_already_done) && (2 == flush_needed)) {
                                /* Open failed. Maybe we should flush ? */
                                int status;
                                time_t flush_tm;
                                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG Calling plugin_flush('write_top',10,%s) (%s:%d)", hostname, __FILE__, __LINE__);
                                status = plugin_flush ("write_top", 0, hostname);
                                if (status == 0) {
                                        /* Flush done. Try again with older values */
                                }
                                flush_already_done = 1;

                                flush_tm = time(NULL);
                                while((time(NULL) - flush_tm) < 10) { /* wait no more than 10 seconds for flush */
                                        sleep(1);
                                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG Trying to open '%s' again... (%s:%d)", buffer, __FILE__, __LINE__);
                                        if(NULL != (gzfh = gzopen(buffer, "r")))  break;
                                }
                        }
                }

                /* File is supposed to be opened, with or without a flush.
                 * Check that the file was really opened
                 */
                if(NULL == gzfh) { /* File could NOT be opened */
                        if((0 == flush_already_done) && (search_direction > 0)) {
                                if(0 == flush_needed) {
                                        last_before_flush_tm = tm; /* save this position */
                                        last_before_flush_n = n;
                                }
                                flush_needed++;
                        }
                } else { /* File could be opened */
                        int distance;

                        flush_needed = 0;
                        distance = check_if_file_contains_tm(gzfh, buffer, tm_start,&local_err);
                        gzclose(gzfh);
                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG distance = '%d' (%s:%d)", distance, __FILE__, __LINE__);
                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG best_distance was = '%d' (%s:%d)", best_distance, __FILE__, __LINE__);
                        if(0 == local_err) { /* ignore this file if something wrong happened */
                                int adistance = abs(distance);
                                /* Check if file found */
                                if(0 == distance) {
                                        best_distance = distance;
                                        best_n = n;
                                        best_tm = tm;
                                        file_found = 1;
                                        break;
                                }
                                /* Check if we found a better file */
                                if(adistance <= best_distance) {
                                        if(
                                                        ((distance < 0) && (tm_start <= tm_end))
                                                        ||
                                                        ((distance > 0) && (tm_start >= tm_end))
                                          ) {
                                                best_distance = adistance;
                                                best_n = n;
                                                best_tm = tm;
                                                if(adistance < max_distance) file_found = 1;

                                        }
                                }
                                search_direction = distance;
                        } /* 0 == local_err */
                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG best_distance is now = '%d' (file found : %d)(%s:%d)", best_distance, file_found, __FILE__, __LINE__);
                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG best_tm/n = '%d/%d' (%s:%d)", best_tm, best_n, __FILE__, __LINE__);
                } /* NULL != gzfh */

                /* Move to next file and check if we should
                 * leave.
                 */
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG search_direction = '%d' (%s:%d)", search_direction, __FILE__, __LINE__);
                if(search_direction > 0) {
                        if(NULL == gzfh) {
                                n = 0;
                                tm += 10000;
                        } else {
                                last_seen_tm_low = tm;
                                last_seen_n_low = n;
                                n += 1;
                        }
                        if(last_seen_tm_high) {
                                if((tm >= last_seen_tm_high) && (n >= last_seen_n_high)) {
                                        break; /* already been there or after */
                                }
                        }
                } else { /* search_direction < 0 */
                        if(NULL != gzfh) {
                                last_seen_tm_high = tm;
                                last_seen_n_high = n;
                        }
                        n = 0;
                        tm -= 10000;
                        if(last_seen_tm_low) {
                                if((tm <= last_seen_tm_low) && (n <= last_seen_n_low)) {
                                        break; /* already been there or before */
                                }
                        }
                }
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG fenetre tm/n = '%d/%d','%d/%d' (%s:%d)", last_seen_tm_low,last_seen_n_low,last_seen_tm_high,last_seen_n_high, __FILE__, __LINE__);
                if(tm_start <= tm_end) { /* When searching forward */
                        if((tm > tm_end) && (
                                                !((0 == flush_already_done) && (2 == flush_needed)) /* Do not break if flush needed */
                                            )) break;
                        /* There should be no reason to search and limit in the past */
                } else { /* When searching backward */
                        if((tm > (tm_start + 10000)) && (
                                                !((0 == flush_already_done) && (2 == flush_needed)) /* Do not break if flush needed */

                                                )) break; /* Going too far in the future (or recent past) */
                        if(tm < (tm_end - 86400)) break; /* Going too far in the past */
                        /* Note : a big old file could contain the data we are
                         * looking for. However, the user should not keep more
                         * than 1 day of data in memory for each hosts. This
                         * is not optimal and is dangerous for the data.
                         */
                }
        }
        if(watchdog >= WATCHDOGMAX) {
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "Infinite loop in %s:%d. hostname='%s', tm=%d, tm_end=%d", __FILE__, __LINE__, hostname, tm_start, tm_end);
                return(JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
        }
        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG file_found = '%d' (%s:%d)", file_found, __FILE__, __LINE__);
        if(file_found) {
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG filename = '%s' (%s:%d)", buffer, __FILE__, __LINE__);
                if(mkpath_by_tm_and_num(buffer + offset, bufferlen - offset,best_tm, best_n)) {
                        return(JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
                }
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG filename = '%s' (%s:%d)", buffer, __FILE__, __LINE__);
        } else {
                buffer[0] = '\0';
        }

        return(0);
} /* }}} check_path */

static struct json_object *read_top_ps_file(const char *filename, int tm, short take_next, time_t *data_tm, int *err) /* {{{ */
{
        /* 
         * Return values :
         *   returned value : json array with the result if success. NULL otherwise.
         *   data_tm        : exact tm to search
         *   err            : not nul if an error occured.
         *
         * If returned value is not nul, it is the json array with the result. data_tm
         * contains the tm of the data found.
         * If the returned value is nul, check if err is nul or not.
         *   If err is nul, data_tm is set to the tm to search. Call again with this
         *   value.
         *   If err is not nul, an error occured.
         */
        gzFile *gzfh=NULL;
        int errnum;
        char line[4096];
        size_t l;
        struct json_object *top_ps_array = NULL;

        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG Trying to open '%s' (%s:%d)", filename, __FILE__, __LINE__);
        *data_tm = 0;
        *err = 0;
        if(NULL == (gzfh = gzopen(filename, "r"))) {
                *err = 1;
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not gzopen for reading (%s:%d)", filename, __FILE__, __LINE__);
                return(NULL);
        }
        /* Read version */
        if(NULL == gzgets(gzfh, line, sizeof(line))) {
                gzclose(gzfh);
                *err = 1;
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not read a line (%s:%d)", filename, __FILE__, __LINE__);
                return(NULL);
        }
        for( l = strlen(line) -1 ; l>0; l--) {
                if(line[l] == '\n') line[l] = '\0';
                else if(line[l] == '\r') line[l] = '\0';
                else break;
        }
        if(!strcmp(line, "Version 1.0")) {
                time_t tm_current, tm_prev, tm_last;
                enum { top_ps_state_tm, top_ps_state_nb_lines, top_ps_state_line } state;
                long n;
                long nb_lines;
                short record_lines = 0;
                short record_last = 0;
                /* Read 2nd line : last tm */
                if(NULL == gzgets(gzfh, line, sizeof(line))) {
                        gzclose(gzfh);
                        *err = 1;
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not read a line (%s:%d)", filename, __FILE__, __LINE__);
                        return(NULL);
                }
                /* Check if the last one is the one we want.
                 * If yes, optimize and remember that when we reach it, we
                 * record it.
                 */
                tm_last = strtol(line, NULL, 10);
                if(0 != errno) {
                        gzclose(gzfh);
                        *err = 1;
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not convert '%s' to integer (%s:%d)", filename, line, __FILE__, __LINE__);
                        return(NULL);
                }
                if((0 == take_next) && (tm > tm_last)) {
                        record_last = 1;
                }


                state = top_ps_state_tm;
                tm_current = 0;
                tm_prev = 0;
                nb_lines = 0;
                n = 0;
                while(
                                ((record_lines != 0) || (NULL == top_ps_array)) && 
                                (NULL != gzgets(gzfh, line, sizeof(line)))
                     ) {
                        json_object *json_string;

                        switch(state) {
                                case top_ps_state_tm :
                                        errno=0;
                                        tm_prev = tm_current;
                                        tm_current = strtol(line, NULL, 10);
                                        if(0 != errno) {
                                                gzclose(gzfh);
                                                *err = 1;
                                                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not convert '%s' to integer (%s:%d)", filename, line, __FILE__, __LINE__);
                                                return(NULL);
                                        }
                                        if(tm_current == tm) {
                                                /* We fould the one we are looking for.
                                                 * Start recording. */
                                                *data_tm = tm_current;
                                                record_lines = 1;
                                        } else if((tm_current == tm_last) && (record_last)) {
                                                *data_tm = tm_current;
                                                record_lines = 1;
                                        } else if(take_next && (tm > tm_prev) && (tm < tm_current)) {
                                                /* The one we are looking for does not exist. The one
                                                 * starting now is the best we can find.
                                                 * Start recording. */
                                                *data_tm = tm_current;
                                                record_lines = 1;
                                        } else if((0 == take_next) && (tm_current > tm)) {
                                                /* We wanted the previous one and we just missed it */
                                                gzclose(gzfh);
                                                if(tm_prev) {
                                                        *data_tm = tm_prev;
                                                        *err = 0; /* no error : try again with exact tm */
                                                        return(NULL);
                                                } else {
                                                        /* this one is not the one we want. And there is no
                                                         * previous one. Error. */
                                                        *err = 1;
                                                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not find '%d' before '%ld' (%s:%d)", filename, tm, tm_current, __FILE__, __LINE__);
                                                        return(NULL);
                                                }
                                        }
                                        state = top_ps_state_nb_lines;
                                        break;
                                case top_ps_state_nb_lines :
                                        errno=0;
                                        nb_lines = strtol(line, NULL, 10);
                                        if(0 != errno) {
                                                gzclose(gzfh);
                                                *err = 1;
                                                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not convert '%s' to integer (%s:%d)", filename, line, __FILE__, __LINE__);
                                                return(NULL);
                                        }
                                        n = 0;
                                        state = top_ps_state_line;
                                        break;
                                case top_ps_state_line :
                                        if(record_lines) {
                                                /* record the line */
                                                if(NULL == top_ps_array) {
                                                        if(NULL == (top_ps_array = json_object_new_array())) {
                                                                gzclose(gzfh);
                                                                *err = 1;
                                                                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not create a new JSON array (%s:%d)", filename, __FILE__, __LINE__);
                                                                return(NULL);
                                                        }
                                                }
                                                /* Remove CR and LF at the end of the line */
                                                l = strlen(line) - 1;
                                                while(l > 0 && ((line[l] == '\r' ) || (line[l] == '\r' ))) {
                                                        line[l] = '\0';
                                                        l -= 1;
                                                }
                                                if(NULL == (json_string = json_object_new_string(line))) {
                                                        json_object_put(top_ps_array);
                                                        gzclose(gzfh);
                                                        *err = 1;
                                                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not create a new JSON string (%s:%d)", filename, __FILE__, __LINE__);
                                                        return(NULL);
                                                }
                                                json_object_array_add(top_ps_array,json_string);
                                        }
                                        n++;
                                        if(n >= nb_lines) {
                                                state = top_ps_state_tm;
                                                record_lines = 0; /* End recoding */
                                        }
                                        break;
                        }
                }
                gzerror(gzfh, &errnum);
                gzclose(gzfh);
                if(errnum < 0) {
                        *err = 1;
                        ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not read a line (%s:%d)", filename, __FILE__, __LINE__);
                        return(NULL);
                }
        }
        if(NULL == top_ps_array) {
                ERROR (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "'%s' : Could not find '%d' before the end of the file (%s:%d)", filename, tm, __FILE__, __LINE__);
                return(NULL);
        }

        return(top_ps_array);
} /* }}} read_top_ps_file */

int jsonrpc_cb_topps_get_top (struct json_object *params, struct json_object *result, const char **errorstring) /* {{{ */
{
        /*
         * { params : { "hostname" : "<a host name>",
         *              "tm"       : <a timestamp to search>,
         *              "end_tm"   : <a timestamp on which search will end>
         *            }
         * }
         *
         * Return :
         * { result : { "status" : "OK" or "some string message if not found",
         *              "tm" : <the timestamp of the data>,
         *              "topps" : [ "string 1", "string 2", ... ]
         *            }
         * }
         *
         * Note : tm can be bigger or lower than end_tm.
         * If tm == end_tm, search exactly tm.
         * If tm < end_tm, search forward.
         * If tm > end_tm, search backward.
         *
         */
        struct json_object *obj;
        struct json_object *result_topps_object;
        int param_timestamp_start=0;
        int param_timestamp_end=0;
        const char *param_hostname = NULL;

        char topps_filename_dir[2048];
        int err;
        time_t result_tm;

        /* Parse the params */
        if(!json_object_is_type (params, json_type_object)) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        /* Params : get the "start_tm" timestamp */
        if(NULL == (obj = json_object_object_get(params, "tm"))) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        if(!json_object_is_type (obj, json_type_int)) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        errno = 0;
        param_timestamp_start = json_object_get_int(obj);
        if(errno != 0) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        /* Params : get the "end_tm" timestamp */
        if(NULL == (obj = json_object_object_get(params, "end_tm"))) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        if(!json_object_is_type (obj, json_type_int)) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        errno = 0;
        param_timestamp_end = json_object_get_int(obj);
        if(errno != 0) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        /* Params : get the "hostname" */
        if(NULL == (obj = json_object_object_get(params, "hostname"))) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        if(!json_object_is_type (obj, json_type_string)) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        errno = 0;
        if(NULL == (param_hostname = json_object_get_string(obj))) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }
        if(errno != 0) {
                return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
        }

        /* Check args */
        if(0 == param_timestamp_start) { return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS); }
        if(0 == param_timestamp_end) { return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS); }
        if(NULL == param_hostname) { return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS); }

        /* Check the servers and build the result array */
        if(NULL == (result_topps_object = json_object_new_object())) {
                DEBUG (OUTPUT_PREFIX_JSONRPC_CB_TOPPS "Could not create a json array");
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "Internal error %s:%d", __FILE__, __LINE__);
                return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
        }

        if(0 != (err = check_path(param_hostname, param_timestamp_start, param_timestamp_end, topps_filename_dir, sizeof(topps_filename_dir)))) {
                json_object_put(result_topps_object);
                return(err);
        }
        if('\0' == topps_filename_dir[0]) {
                DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG topps_filename_dir[0] == '\\0' bummer ! (%s:%d)", __FILE__, __LINE__);
                obj =  json_object_new_string("path not found or no file for this tm");
                json_object_object_add(result_topps_object, "status", obj);
                json_object_object_add(result, "result", result_topps_object);
                return(0);
        }
        /* Read the file, 1st time */
        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG read_top_ps_file('%s', '%d',...) (%s:%d)", topps_filename_dir,param_timestamp_start, __FILE__, __LINE__);
        obj = read_top_ps_file(
                        /* filename  = */ topps_filename_dir,
                        /* tm        = */ param_timestamp_start,
                        /* take_next = */ (param_timestamp_end>=param_timestamp_start)?1:0,
                        /* data_tm   = */ &result_tm, 
                        /* *err      = */ &err);
        if(NULL == obj) {
                /* If obj could not be created, check if it is an error.
                 * Otherwise, try again with returned result_tm.
                 */
                if(err) {
                        json_object_put(result_topps_object);
                        return(err);
                }
        }
        /* Check if result_tm is inside [start .. end] */
        if (
                        ( (param_timestamp_end >= param_timestamp_start) && (result_tm <= param_timestamp_end) ) || 
                        ( (param_timestamp_end <  param_timestamp_start) && (result_tm >= param_timestamp_end) )
           ) {
                /* OK, result_tm is correct. Go on... */
                if(NULL == obj) {
                        /* Here, we found a correct result_tm, but did
                         * not record. Try again with the exact tm.
                         */ 
                        time_t tm2 = result_tm;
                        DEBUG(OUTPUT_PREFIX_JSONRPC_CB_TOPPS "DEBUG read_top_ps_file('%s', '%ld',...) 2nd time (%s:%d)", topps_filename_dir,tm2, __FILE__, __LINE__);
                        obj = read_top_ps_file(
                                        /* filename  = */ topps_filename_dir,
                                        /* tm        = */ tm2,
                                        /* take_next = */ (param_timestamp_end>=param_timestamp_start)?1:0,
                                        /* data_tm   = */ &result_tm, 
                                        /* *err      = */ &err);
                        if(NULL == obj) {
                                json_object_put(result_topps_object);
                                return(err?err:1);
                        }
                }
        } else {
                /* result_tm is too far from what we want.
                 * If an object obj was defined, purge it. */
                if(NULL != obj) {
                        json_object_put(obj);
                }

                obj =  json_object_new_string("path not found or no file for this tm");
                json_object_object_add(result_topps_object, "status", obj);
                json_object_object_add(result, "result", result_topps_object);
                return(0);
        }

        json_object_object_add(result_topps_object, "topps", obj);
        obj =  json_object_new_int(result_tm);
        json_object_object_add(result_topps_object, "tm", obj);
        obj =  json_object_new_string("OK");
        json_object_object_add(result_topps_object, "status", obj);

        /* TODO */

        /* Last : add the "result" to the result object */
        json_object_object_add(result, "result", result_topps_object);

        return(0);
} /* }}} jsonrpc_cb_topps_get_top */

/* vim: set fdm=marker sw=8 ts=8 tw=78 et : */
