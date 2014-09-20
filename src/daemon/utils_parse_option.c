/**
 * collectd - src/utils_parse_option.c
 * Copyright (C) 2008       Florian Forster
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
  while (isalnum ((int) *buffer) || *buffer == '_' || *buffer == ':')
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

/* vim: set sw=2 ts=8 tw=78 et : */
