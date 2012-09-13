/**
 * collectd - src/utils_parse_option.c
 * Copyright (C) 2008  Florian Forster
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_parse_option.h"

int parse_string (char **ret_buffer, char **ret_string)
{
  char *buffer;
  char *string;

  buffer = *ret_buffer;

  /* Eat up leading spaces. */
  string = buffer;
  while (isspace ((int) *string))
    string++;
  if (*string == 0)
    return (1);

  /* A quoted string */
  if (*string == '"')
  {
    char *dst;

    string++;
    if (*string == 0)
      return (1);

    dst = string;
    buffer = string;
    while ((*buffer != '"') && (*buffer != 0))
    {
      /* Un-escape backslashes */
      if (*buffer == '\\')
      {
        buffer++;
        /* Catch a backslash at the end of buffer */
        if (*buffer == 0)
          return (-1);
      }
      *dst = *buffer;
      buffer++;
      dst++;
    }
    /* No quote sign has been found */
    if (*buffer == 0)
      return (-1);

    *dst = 0;
    dst++;
    *buffer = 0;
    buffer++;

    /* Check for trailing spaces. */
    if ((*buffer != 0) && !isspace ((int) *buffer))
      return (-1);
  }
  else /* an unquoted string */
  {
    buffer = string;
    while ((*buffer != 0) && !isspace ((int) *buffer))
      buffer++;
    if (*buffer != 0)
    {
      *buffer = 0;
      buffer++;
    }
  }
  
  /* Eat up trailing spaces */
  while (isspace ((int) *buffer))
    buffer++;

  *ret_buffer = buffer;
  *ret_string = string;

  return (0);
} /* int parse_string */

/*
 * parse_option
 * ------------
 *  Parses an ``option'' as used with the unixsock and exec commands. An
 *  option is of the form:
 *    name0="value"
 *    name1="value with \"quotes\""
 *    name2="value \\ backslash"
 *  However, if the value does *not* contain a space character, you can skip
 *  the quotes.
 */
int parse_option (char **ret_buffer, char **ret_key, char **ret_value)
{
  char *buffer;
  char *key;
  char *value;
  int status;

  buffer = *ret_buffer;

  /* Eat up leading spaces */
  key = buffer;
  while (isspace ((int) *key))
    key++;
  if (*key == 0)
    return (1);

  /* Look for the equal sign */
  buffer = key;
  while (isalnum ((int) *buffer))
    buffer++;
  if ((*buffer != '=') || (buffer == key))
    return (1);
  *buffer = 0;
  buffer++;
  /* Empty values must be written as "" */
  if (isspace ((int) *buffer) || (*buffer == 0))
    return (-1);

  status = parse_string (&buffer, &value);
  if (status != 0)
    return (-1);

  /* NB: parse_string will have eaten up all trailing spaces. */

  *ret_buffer = buffer;
  *ret_key = key;
  *ret_value = value;

  return (0);
} /* int parse_option */

int escape_string (char *buffer, size_t buffer_size)
{
  char *temp;
  size_t i;
  size_t j;

  /* Check if we need to escape at all first */
  temp = strpbrk (buffer, " \t\"\\");
  if (temp == NULL)
    return (0);

  temp = (char *) malloc (buffer_size);
  if (temp == NULL)
    return (-1);
  memset (temp, 0, buffer_size);

  temp[0] = '"';
  j = 1;

  for (i = 0; i < buffer_size; i++)
  {
    if (buffer[i] == 0)
    {
      break;
    }
    else if ((buffer[i] == '"') || (buffer[i] == '\\'))
    {
      if (j > (buffer_size - 4))
        break;
      temp[j] = '\\';
      temp[j + 1] = buffer[i];
      j += 2;
    }
    else
    {
      if (j > (buffer_size - 3))
        break;
      temp[j] = buffer[i];
      j++;
    }
  }

  assert ((j + 1) < buffer_size);
  temp[j] = '"';
  temp[j + 1] = 0;

  sstrncpy (buffer, temp, buffer_size);
  sfree (temp);
  return (0);
} /* int escape_string */

/* vim: set sw=2 ts=8 tw=78 et : */
