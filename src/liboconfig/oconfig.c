/**
 * oconfig - src/oconfig.c
 * Copyright (C) 2006,2007  Florian octo Forster <octo at verplant.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "oconfig.h"

extern FILE *yyin;

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

void oconfig_free (oconfig_item_t *ci)
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
    oconfig_free (ci->children + i);

  if (ci->children != NULL)
    free (ci->children);
}

/*
 * vim:shiftwidth=2:tabstop=8:softtabstop=2
 */
