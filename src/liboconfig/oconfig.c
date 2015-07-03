/**
 * collectd - src/liboconfig/oconfig.c
 * Copyright (C) 2006,2007  Florian Forster
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
 **/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "oconfig.h"

extern FILE *yyin;
extern int yyparse (void);

oconfig_item_t *ci_root;
const char     *c_file;

static void yyset_in  (FILE *fd)
{
  yyin = fd;
} /* void yyset_in */

oconfig_item_t *oconfig_parse_fh (FILE *fh)
{
  int status;
  oconfig_item_t *ret;

  char file[10];

  yyset_in (fh);

  if (NULL == c_file) {
    int status;

    status = snprintf (file, sizeof (file), "<fd#%d>", fileno (fh));

    if ((status < 0) || (status >= sizeof (file))) {
      c_file = "<unknown>";
    }
    else {
      file[sizeof (file) - 1] = '\0';
      c_file = file;
    }
  }

  status = yyparse ();
  if (status != 0)
  {
    fprintf (stderr, "yyparse returned error #%i\n", status);
    return (NULL);
  }

  c_file = NULL;

  ret = ci_root;
  ci_root = NULL;
  yyset_in ((FILE *) 0);

  return (ret);
} /* oconfig_item_t *oconfig_parse_fh */

oconfig_item_t *oconfig_parse_file (const char *file)
{
  FILE *fh;
  oconfig_item_t *ret;

  c_file = file;

  fh = fopen (file, "r");
  if (fh == NULL)
  {
    fprintf (stderr, "fopen (%s) failed: %s\n", file, strerror (errno));
    return (NULL);
  }

  ret = oconfig_parse_fh (fh);
  fclose (fh);

  c_file = NULL;

  return (ret);
} /* oconfig_item_t *oconfig_parse_file */

oconfig_item_t *oconfig_clone (const oconfig_item_t *ci_orig)
{
  oconfig_item_t *ci_copy;

  ci_copy = (oconfig_item_t *) malloc (sizeof (*ci_copy));
  if (ci_copy == NULL)
  {
    fprintf (stderr, "malloc failed.\n");
    return (NULL);
  }
  memset (ci_copy, 0, sizeof (*ci_copy));
  ci_copy->values = NULL;
  ci_copy->parent = NULL;
  ci_copy->children = NULL;

  ci_copy->key = strdup (ci_orig->key);
  if (ci_copy->key == NULL)
  {
    fprintf (stderr, "strdup failed.\n");
    free (ci_copy);
    return (NULL);
  }

  if (ci_orig->values_num > 0) /* {{{ */
  {
    int i;

    ci_copy->values = (oconfig_value_t *) calloc (ci_orig->values_num,
	sizeof (*ci_copy->values));
    if (ci_copy->values == NULL)
    {
      fprintf (stderr, "calloc failed.\n");
      free (ci_copy->key);
      free (ci_copy);
      return (NULL);
    }
    ci_copy->values_num = ci_orig->values_num;

    for (i = 0; i < ci_copy->values_num; i++)
    {
       ci_copy->values[i].type = ci_orig->values[i].type;
       if (ci_copy->values[i].type == OCONFIG_TYPE_STRING)
       {
	 ci_copy->values[i].value.string
	   = strdup (ci_orig->values[i].value.string);
	 if (ci_copy->values[i].value.string == NULL)
	 {
	   fprintf (stderr, "strdup failed.\n");
	   oconfig_free (ci_copy);
	   return (NULL);
	 }
       }
       else /* ci_copy->values[i].type != OCONFIG_TYPE_STRING) */
       {
	 ci_copy->values[i].value = ci_orig->values[i].value;
       }
    }
  } /* }}} if (ci_orig->values_num > 0) */

  if (ci_orig->children_num > 0) /* {{{ */
  {
    int i;

    ci_copy->children = (oconfig_item_t *) calloc (ci_orig->children_num,
	sizeof (*ci_copy->children));
    if (ci_copy->children == NULL)
    {
      fprintf (stderr, "calloc failed.\n");
      oconfig_free (ci_copy);
      return (NULL);
    }
    ci_copy->children_num = ci_orig->children_num;

    for (i = 0; i < ci_copy->children_num; i++)
    {
      oconfig_item_t *child;
      
      child = oconfig_clone (ci_orig->children + i);
      if (child == NULL)
      {
	oconfig_free (ci_copy);
	return (NULL);
      }
      child->parent = ci_copy;
      ci_copy->children[i] = *child;
      free (child);
    } /* for (i = 0; i < ci_copy->children_num; i++) */
  } /* }}} if (ci_orig->children_num > 0) */

  return (ci_copy);
} /* oconfig_item_t *oconfig_clone */

void oconfig_free_all (oconfig_item_t *ci)
{
  int i;

  if (ci == NULL)
    return;

  if (ci->key != NULL)
    free (ci->key);

  for (i = 0; i < ci->values_num; i++)
    if ((ci->values[i].type == OCONFIG_TYPE_STRING)
        && (NULL != ci->values[i].value.string))
      free (ci->values[i].value.string);

  if (ci->values != NULL)
    free (ci->values);

  for (i = 0; i < ci->children_num; i++)
    oconfig_free_all (ci->children + i);

  if (ci->children != NULL)
    free (ci->children);
}

void oconfig_free (oconfig_item_t *ci)
{
  oconfig_free_all (ci);
  free (ci);
  ci = NULL;
}

/*
 * vim:shiftwidth=2:tabstop=8:softtabstop=2:fdm=marker
 */
