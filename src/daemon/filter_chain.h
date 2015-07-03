/**
 * collectd - src/filter_chain.h
 * Copyright (C) 2008,2009  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#ifndef FILTER_CHAIN_H
#define FILTER_CHAIN_H 1

#include "collectd.h"
#include "plugin.h"

#define FC_MATCH_NO_MATCH  0
#define FC_MATCH_MATCHES   1

#define FC_TARGET_CONTINUE 0
#define FC_TARGET_STOP     1
#define FC_TARGET_RETURN   2

/*
 * Match functions
 */
struct match_proc_s
{
  int (*create) (const oconfig_item_t *ci, void **user_data);
  int (*destroy) (void **user_data);
  int (*match) (const data_set_t *ds, const value_list_t *vl,
      notification_meta_t **meta, void **user_data);
};
typedef struct match_proc_s match_proc_t;

int fc_register_match (const char *name, match_proc_t proc);

/*
 * Target functions
 */
struct target_proc_s
{
  int (*create) (const oconfig_item_t *ci, void **user_data);
  int (*destroy) (void **user_data);
  int (*invoke) (const data_set_t *ds, value_list_t *vl,
      notification_meta_t **meta, void **user_data);
};
typedef struct target_proc_s target_proc_t;

struct fc_chain_s;
typedef struct fc_chain_s fc_chain_t;

int fc_register_target (const char *name, target_proc_t proc);

/*
 * TODO: Chain management
 */
#if 0
int fc_chain_add (const char *chain_name,
    const char *target_name, int target_argc, char **target_argv);
int fc_chain_delete (const char *chain_name);
#endif

/*
 * TODO: Rule management
 */
#if 0
int fc_rule_add (const char *chain_name, int position,
    int match_num, const char **match_name, int *match_argc, char ***match_argv,
    const char *target_name, int target_argc, char **target_argv);
int fc_rule_delete (const char *chain_name, int position);
#endif

/*
 * Processing function
 */
fc_chain_t *fc_chain_get_by_name (const char *chain_name);

int fc_process_chain (const data_set_t *ds, value_list_t *vl,
    fc_chain_t *chain);

int fc_default_action (const data_set_t *ds, value_list_t *vl);

/* 
 * Shortcut for global configuration
 */
int fc_configure (const oconfig_item_t *ci);

#endif /* FILTER_CHAIN_H */
/* vim: set sw=2 sts=2 et : */
