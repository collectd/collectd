/**
 * collectd - src/virt_test.c
 * Copyright (C) 2016 Francesco Romani <fromani at redhat.com>
 * Based on
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

#include "testing.h"
#include "virt.c" /* sic */

#ifdef HAVE_LIST_ALL_DOMAINS

virDomainPtr *domains;

static int setup(void)
{
  if (virInitialize() != 0) {
    printf("ERROR: virInitialize() != 0\n");
    return -1;
  }

  conn = virConnectOpen("test:///default");
  if (conn == NULL) {
    printf("ERROR: virConnectOpen == NULL\n");
    return -1;
  }

  return 0;
}

static int teardown(void)
{
  sfree(domains);
  if (conn != NULL)
    virConnectClose(conn);

  return 0;
}

DEF_TEST(get_domain_state_notify) {
  if (setup() == 0) {
    int n_domains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_GET_ALL_DOMAINS_STATS_PERSISTENT);
    if (n_domains <= 0) {
      printf("ERROR: virConnectListAllDomains: n_domains <= 0\n");
      return -1;
    }

    int ret = get_domain_state_notify(domains[0]);
    EXPECT_EQ_INT(0, ret);
  }
  teardown();
  
  return 0;
}

DEF_TEST(persistent_domains_state_notification) {
  if (setup() == 0) {
    int ret = persistent_domains_state_notification();
    EXPECT_EQ_INT(0, ret);
  }
  teardown();
  
  return 0;
}
#endif

int main(void) {
#ifdef HAVE_LIST_ALL_DOMAINS
  RUN_TEST(get_domain_state_notify);
  RUN_TEST(persistent_domains_state_notification);
#endif

  END_TEST;
}
