/**
 * collectd - src/ceph_test.c
 * Copyright (C) 2015      Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "ceph.c" /* sic */
#include "testing.h"

DEF_TEST(parse_keys)
{
  struct {
    char *str;
    char *want;
  } cases[] = {
    {"WBThrottle.bytes_dirtied.description.bytes_wb.description.ios_dirtied.description.ios_wb.type", "WBThrottle.bytesDirtied.description.bytesWb.description.iosDirt"},
    {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "Aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
    {"foo:bar", "FooBar"},
    {"foo:bar+", "FooBarPlus"},
    {"foo:bar-", "FooBarMinus"},
    {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+", "AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaPlus"},
    {"aa.bb.cc.dd.ee.ff", "Aa.bb.cc.dd.ee.ff"},
    {"aa.bb.cc.dd.ee.ff.type", "Aa.bb.cc.dd.ee.ff"},
    {"aa.type", "Aa.type"},
  };
  size_t i;

  for (i = 0; i < STATIC_ARRAY_SIZE (cases); i++)
  {
    char got[DATA_MAX_NAME_LEN];

    memset (got, 0, sizeof (got));

    parse_keys (cases[i].str, got);

    CHECK_ZERO (strcmp (got, cases[i].want));
  }

  return 0;
}

int main (void)
{
  RUN_TEST(parse_keys);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
