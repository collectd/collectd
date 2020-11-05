/**
 * collectd - src/utils_curl_stats.h
 * Copyright (C) 2015       Sebastian 'tokkee' Harl
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Sebastian Harl <sh@tokkee.org>
 *   Barbara 'bkjg' Kaczorowska <bkjg at google.com>
 **/

#ifndef UTILS_CURL_STATS_H
#define UTILS_CURL_STATS_H 1

#include "plugin.h"

#include <curl/curl.h>

struct curl_stats_s;
typedef struct curl_stats_s curl_stats_t;

/*
 * curl_stats_from_config allocates and constructs a cURL statistics object
 * from the specified configuration which is expected to be a single block of
 * boolean options named after cURL information fields. The boolean value
 * indicates whether to collect the respective information.
 *
 * See http://curl.haxx.se/libcurl/c/curl_easy_getinfo.html
 */
curl_stats_t *curl_stats_from_config(oconfig_item_t *ci);

void curl_stats_destroy(curl_stats_t *s);

/*
 * curl_stats_dispatch dispatches performance values from the specified
 * cURL session to the daemon.
 */
int curl_stats_dispatch(curl_stats_t *s, CURL *curl, const char *hostname,
                        const char *plugin, const char *plugin_instance);

/* TODO(bkjg): add descriptions of these functions */
/* curl_stats_account_data accounts data from the specified cURL session and
 * save it to the proper metric_t data structure*/
int curl_stats_account_data(curl_stats_t *s, CURL *curl);

/* curl_stats_send_metric_to_daemon sends data which is saved in metrics to the
 * daemon*/
int curl_stats_send_metric_to_daemon(curl_stats_t *s);

/* curl_stats_get_enabled_attributes takes enabled attributes from s data
 * structure and saves in num_enabled_attr the number of them */
char **curl_stats_get_enabled_attributes(curl_stats_t *s,
                                         size_t *num_enabled_attr);

/* curl_stats_get_metric_families_for_attributes takes metric families for given
 * groups of attributes from s data structure and saves in num_attr the number
 * of these groups */
metric_family_t **
curl_stats_get_metric_families_for_attributes(curl_stats_t *s,
                                              size_t *num_attr);

#endif /* UTILS_CURL_STATS_H */
