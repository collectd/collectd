/**
 * collectd - src/livestatus.c
 * Copyright (C) 2019 IN2P3 Computing Centre, IN2P3, CNRS
 *
 * Licensed under the same terms and conditions as src/livestatus.c.
 *
 * Authors:
 *   Remi Ferrand <remi.ferrand at cc.in2p3.fr>
 **/

#include "livestatus.c"

#include "testing.h"

// ls_lstatus_cmp_fn is a type of function used to compare two
// livestatus_status_t structs
typedef int (*ls_lstatus_cmp_fn)(livestatus_status_t *, livestatus_status_t *);

/* raw livestatus responses */
static char lresponse_ok[] =
    "0;2161;7.8094609445e-02;0;0.0000000000e+00;34289;5.1425594053e-01;0;0."
    "0000000000e+00;0;0.0000000000e+00;213267;2.7369494031e+00;741957;9."
    "4749721737e+00;68688;8.5057596878e-01;140849;1.6611919493e+00\n";
static char lresponse_too_many_fields[] =
    "0;2161;7.8094609445e-02;0;0.0000000000e+00;34289;5.1425594053e-01;0;0."
    "0000000000e+00;0;0.0000000000e+00;213267;2.7369494031e+00;741957;9."
    "4749721737e+00;68688;8.5057596878e-01;140849;1.6611919493e+00;1;45;56\n";
static char lresponse_not_enough_fields[] =
    "0;2161;7.8094609445e-02;0;0.0000000000e+00;34289;5.1425594053e-01;0;0."
    "0000000000e+00;0;0.0000000000e+00;213267;2.7369494031e+00;741957;9."
    "4749721737e+00;68688;8.5057596878e-01\n";
static char lresponse_empty[] = "";

/* livestatus_status_t structs */
static livestatus_status_t lresponse_ok_status_t = {
    .cached_log_messages = 0,
    .connections_rate = 0.078094609445,
    .neb_callbacks_rate = 9.4749721737,
    .service_checks_rate = 1.6611919493,
};

/* test_cmp_livestatus_status_t compares two livestatus_status_t structs by
 * comparing only a subset of the fields */
static int test_cmp_livestatus_status_t(livestatus_status_t *a,
                                        livestatus_status_t *b) {
  EXPECT_EQ_INT(a->cached_log_messages, b->cached_log_messages);
  EXPECT_EQ_DOUBLE(a->connections_rate, b->connections_rate);
  EXPECT_EQ_DOUBLE(a->neb_callbacks_rate, b->neb_callbacks_rate);
  EXPECT_EQ_DOUBLE(a->service_checks_rate, b->service_checks_rate);

  return 0;
} /* int test_cmp_livestatus_status_t */

DEF_TEST(parse) {
  struct {
    char *lresponse;
    int want_rc;
    ls_lstatus_cmp_fn lstatus_cmp;
    livestatus_status_t *want_lstatus;
  } cases[] = {
      /* all ok */
      {lresponse_ok, 0, test_cmp_livestatus_status_t, &lresponse_ok_status_t},
      /* too many fields */
      {lresponse_too_many_fields, -2, NULL, NULL},
      /* not enough fields */
      {lresponse_not_enough_fields, -3, NULL, NULL},
      /* empty response */
      {lresponse_empty, -3, NULL, NULL},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    livestatus_status_t lstatus;
    int rc = -1;

    memset(&lstatus, 0x0, sizeof(lstatus));

    rc = ls_parse((const char *)cases[i].lresponse, &lstatus);
    EXPECT_EQ_INT(cases[i].want_rc, rc);

    if (cases[i].lstatus_cmp == NULL)
      continue;

    rc = cases[i].lstatus_cmp(&lstatus, cases[i].want_lstatus);
    EXPECT_EQ_INT(0, rc);
  }

  return 0;
} /* DEF_TEST(parse) */

int main(void) {
  RUN_TEST(parse);

  END_TEST;
}
