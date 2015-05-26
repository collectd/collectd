/**
 * collectd - src/tests/test_common.c
 * Copyright (C) 2013       Florian octo Forster
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
 */

#include "testing.h"
#include "common.h"

DEF_TEST(sstrncpy)
{
  char buffer[16] = "";
  char *ptr = &buffer[4];
  char *ret;

  buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0xff;
  buffer[12] = buffer[13] = buffer[14] = buffer[15] = 0xff;

  ret = sstrncpy (ptr, "foobar", 8);
  OK(ret == ptr);
  STREQ ("foobar", ptr);
  OK(buffer[3] == buffer[12]);

  ret = sstrncpy (ptr, "abc", 8);
  OK(ret == ptr);
  STREQ ("abc", ptr);
  OK(buffer[3] == buffer[12]);

  ret = sstrncpy (ptr, "collectd", 8);
  OK(ret == ptr);
  OK(ptr[7] == 0);
  STREQ ("collect", ptr);
  OK(buffer[3] == buffer[12]);

  return (0);
}

DEF_TEST(ssnprintf)
{
  char buffer[16] = "";
  char *ptr = &buffer[4];
  int status;

  buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0xff;
  buffer[12] = buffer[13] = buffer[14] = buffer[15] = 0xff;

  status = ssnprintf (ptr, 8, "%i", 1337);
  OK(status == 4);
  STREQ ("1337", ptr);

  status = ssnprintf (ptr, 8, "%s", "collectd");
  OK(status == 8);
  OK(ptr[7] == 0);
  STREQ ("collect", ptr);
  OK(buffer[3] == buffer[12]);

  return (0);
}

DEF_TEST(sstrdup)
{
  char *ptr;

  ptr = sstrdup ("collectd");
  OK(ptr != NULL);
  STREQ ("collectd", ptr);

  sfree(ptr);
  OK(ptr == NULL);

  ptr = sstrdup (NULL);
  OK(ptr == NULL);

  return (0);
}

DEF_TEST(strsplit)
{
  char buffer[32];
  char *fields[8];
  int status;

  strncpy (buffer, "foo bar", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 2);
  STREQ ("foo", fields[0]);
  STREQ ("bar", fields[1]);

  strncpy (buffer, "foo \t bar", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 2);
  STREQ ("foo", fields[0]);
  STREQ ("bar", fields[1]);

  strncpy (buffer, "one two\tthree\rfour\nfive", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 5);
  STREQ ("one", fields[0]);
  STREQ ("two", fields[1]);
  STREQ ("three", fields[2]);
  STREQ ("four", fields[3]);
  STREQ ("five", fields[4]);

  strncpy (buffer, "\twith trailing\n", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 2);
  STREQ ("with", fields[0]);
  STREQ ("trailing", fields[1]);

  strncpy (buffer, "1 2 3 4 5 6 7 8 9 10 11 12 13", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 8);
  STREQ ("7", fields[6]);
  STREQ ("8", fields[7]);

  strncpy (buffer, "single", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 1);
  STREQ ("single", fields[0]);

  strncpy (buffer, "", sizeof (buffer));
  status = strsplit (buffer, fields, 8);
  OK(status == 0);

  return (0);
}

DEF_TEST(strjoin)
{
  char buffer[16];
  char *fields[4];
  int status;

  fields[0] = "foo";
  fields[1] = "bar";
  fields[2] = "baz";
  fields[3] = "qux";

  status = strjoin (buffer, sizeof (buffer), fields, 2, "!");
  OK(status == 7);
  STREQ ("foo!bar", buffer);

  status = strjoin (buffer, sizeof (buffer), fields, 1, "!");
  OK(status == 3);
  STREQ ("foo", buffer);

  status = strjoin (buffer, sizeof (buffer), fields, 0, "!");
  OK(status < 0);

  status = strjoin (buffer, sizeof (buffer), fields, 2, "rcht");
  OK(status == 10);
  STREQ ("foorchtbar", buffer);

  status = strjoin (buffer, sizeof (buffer), fields, 4, "");
  OK(status == 12);
  STREQ ("foobarbazqux", buffer);

  status = strjoin (buffer, sizeof (buffer), fields, 4, "!");
  OK(status == 15);
  STREQ ("foo!bar!baz!qux", buffer);

  fields[0] = "0123";
  fields[1] = "4567";
  fields[2] = "8901";
  fields[3] = "2345";
  status = strjoin (buffer, sizeof (buffer), fields, 4, "-");
  OK(status < 0);

  return (0);
}

DEF_TEST(strunescape)
{
  char buffer[16];
  int status;

  strncpy (buffer, "foo\\tbar", sizeof (buffer));
  status = strunescape (buffer, sizeof (buffer));
  OK(status == 0);
  STREQ ("foo\tbar", buffer);

  strncpy (buffer, "\\tfoo\\r\\n", sizeof (buffer));
  status = strunescape (buffer, sizeof (buffer));
  OK(status == 0);
  STREQ ("\tfoo\r\n", buffer);

  strncpy (buffer, "With \\\"quotes\\\"", sizeof (buffer));
  status = strunescape (buffer, sizeof (buffer));
  OK(status == 0);
  STREQ ("With \"quotes\"", buffer);

  /* Backslash before null byte */
  strncpy (buffer, "\\tbackslash end\\", sizeof (buffer));
  status = strunescape (buffer, sizeof (buffer));
  OK(status != 0);
  STREQ ("\tbackslash end", buffer);
  return (0);

  /* Backslash at buffer end */
  strncpy (buffer, "\\t3\\56", sizeof (buffer));
  status = strunescape (buffer, 4);
  OK(status != 0);
  OK(buffer[0] == '\t');
  OK(buffer[1] == '3');
  OK(buffer[2] == 0);
  OK(buffer[3] == 0);
  OK(buffer[4] == '5');
  OK(buffer[5] == '6');
  OK(buffer[6] == '7');

  return (0);
}

int main (void)
{
  RUN_TEST(sstrncpy);
  RUN_TEST(ssnprintf);
  RUN_TEST(sstrdup);
  RUN_TEST(strsplit);
  RUN_TEST(strjoin);
  RUN_TEST(strunescape);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
