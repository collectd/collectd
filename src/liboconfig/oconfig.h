/**
 * collectd - src/liboconfig/oconfig.h
 * Copyright (C) 2006-2009  Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 */

#ifndef OCONFIG_H
#define OCONFIG_H 1

#include <stdio.h>

/*
 * Types
 */
#define OCONFIG_TYPE_STRING  0
#define OCONFIG_TYPE_NUMBER  1
#define OCONFIG_TYPE_BOOLEAN 2

struct oconfig_value_s
{
  union
  {
    char  *string;
    double number;
    int    boolean;
  } value;
  int type;
};
typedef struct oconfig_value_s oconfig_value_t;

struct oconfig_item_s;
typedef struct oconfig_item_s oconfig_item_t;
struct oconfig_item_s
{
  char            *key;
  oconfig_value_t *values;
  int              values_num;

  oconfig_item_t  *parent;
  oconfig_item_t  *children;
  int              children_num;
};

/*
 * Functions
 */
oconfig_item_t *oconfig_parse_fh (FILE *fh);
oconfig_item_t *oconfig_parse_file (const char *file);

oconfig_item_t *oconfig_clone (const oconfig_item_t *ci);

void oconfig_free (oconfig_item_t *ci);

/*
 * vim: shiftwidth=2:tabstop=8:softtabstop=2
 */
#endif /* OCONFIG_H */
