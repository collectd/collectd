#include "intel_rdt.c" /* sic */
#include "testing.h"

/***************************************************************************
 * PQOS mocks
 */
int pqos_mon_reset(void) { return 0; }
int pqos_mon_assoc_get(const unsigned lcore, pqos_rmid_t *rmid) { return 0; }
int pqos_mon_start(const unsigned num_cores, const unsigned *cores,
                   const enum pqos_mon_event event, void *context,
                   struct pqos_mon_data *group) {
  return 0;
}
#if PQOS_VERSION >= 30000
int pqos_mon_start_pids(const unsigned num_pids, const pid_t *pids,
                        const enum pqos_mon_event event, void *context,
                        struct pqos_mon_data *group) {
  return 0;
}
int pqos_mon_add_pids(const unsigned num_pids, const pid_t *pids,
                      struct pqos_mon_data *group) {
  return 0;
}
int pqos_mon_remove_pids(const unsigned num_pids, const pid_t *pids,
                         struct pqos_mon_data *group) {
  return 0;
}

#else
int pqos_mon_start_pid(const pid_t pids, const enum pqos_mon_event event,
                       void *context, struct pqos_mon_data *group) {
  return 0;
}
#endif
int pqos_mon_stop(struct pqos_mon_data *group) { return 0; }
int pqos_mon_poll(struct pqos_mon_data **groups, const unsigned num_groups) {
  return 0;
}

#if PQOS_VERSION >= 30000
int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg,
                     const enum pqos_cdp_config l2_cdp_cfg,
                     const enum pqos_mba_config mba_cfg) {
  return 0;
}
#elif PQOS_VERSION >= 20000
int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg,
                     const enum pqos_cdp_config l2_cdp_cfg) {
  return 0;
}
#else
int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg) { return 0; }
#endif
int pqos_alloc_assoc_set(const unsigned lcore, const unsigned class_id) {
  return 0;
}
int pqos_alloc_assoc_get(const unsigned lcore, unsigned *class_id) { return 0; }
int pqos_alloc_assoc_set_pid(const pid_t task, const unsigned class_id) {
  return 0;
}
int pqos_alloc_assoc_get_pid(const pid_t task, unsigned *class_id) { return 0; }
int pqos_alloc_assign(const unsigned technology, const unsigned *core_array,
                      const unsigned core_num, unsigned *class_id) {
  return 0;
}
int pqos_alloc_release(const unsigned *core_array, const unsigned core_num) {
  return 0;
}
int pqos_alloc_assign_pid(const unsigned technology, const pid_t *task_array,
                          const unsigned task_num, unsigned *class_id) {
  return 0;
}
int pqos_alloc_release_pid(const pid_t *task_array, const unsigned task_num) {
  return 0;
}
int pqos_init(const struct pqos_config *config) { return 0; }
int pqos_fini(void) { return 0; }
int pqos_cap_get_type(const struct pqos_cap *cap, const enum pqos_cap_type type,
                      const struct pqos_capability **cap_item) {
  return 0;
}
int pqos_cap_get(const struct pqos_cap **cap, const struct pqos_cpuinfo **cpu) {
  return 0;
}

#ifdef LIBPQOS2
/***************************************************************************
 * helper functions
 */
rdt_ctx_t *stub_rdt_setup() {

  rdt_ctx_t *rdt = calloc(1, sizeof(*rdt));
  struct pqos_cpuinfo *pqos_cpu = calloc(1, sizeof(*pqos_cpu));
  struct pqos_cap *pqos_cap = calloc(1, sizeof(*pqos_cap));
  struct pqos_cap_mon *mon = calloc(1, sizeof(*mon));
  struct pqos_capability *cap_mon = calloc(1, sizeof(*cap_mon));

  cap_mon->u.mon = mon;
  rdt->pqos_cap = pqos_cap;
  rdt->pqos_cpu = pqos_cpu;
  rdt->cap_mon = cap_mon;

  return rdt;
}

void stub_rdt_teardown(rdt_ctx_t *rdt) {
  free(rdt->cap_mon->u.mon);
  free((void *)rdt->cap_mon);
  free((void *)rdt->pqos_cpu);
  free((void *)rdt->pqos_cap);
  free(rdt);
}

/***************************************************************************
 * tests
 */
DEF_TEST(rdt_config_ngroups__one_process) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "proc1", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_STR(values[0].value.string, rdt->ngroups[0].desc);
  EXPECT_EQ_INT(1, rdt->num_ngroups);

  /* cleanup */
  rdt_free_ngroups(rdt);
  stub_rdt_teardown(rdt);

  return 0;
}

DEF_TEST(rdt_config_ngroups__two_groups) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "proc11,proc12,proc13", .type = OCONFIG_TYPE_STRING},
      {.value.string = "proc21,proc22,proc23", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(2, rdt->num_ngroups);
  EXPECT_EQ_STR("proc11,proc12,proc13", rdt->ngroups[0].desc);
  EXPECT_EQ_STR("proc21,proc22,proc23", rdt->ngroups[1].desc);
  EXPECT_EQ_STR("proc11", rdt->ngroups[0].names[0]);
  EXPECT_EQ_STR("proc12", rdt->ngroups[0].names[1]);
  EXPECT_EQ_STR("proc13", rdt->ngroups[0].names[2]);
  EXPECT_EQ_STR("proc21", rdt->ngroups[1].names[0]);
  EXPECT_EQ_STR("proc22", rdt->ngroups[1].names[1]);
  EXPECT_EQ_STR("proc23", rdt->ngroups[1].names[2]);

  /* cleanup */
  rdt_free_ngroups(rdt);
  stub_rdt_teardown(rdt);

  return 0;
}

DEF_TEST(rdt_config_ngroups__too_long_proc_name) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "_seventeen_chars_", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(-EINVAL, result);

  /* cleanup */
  stub_rdt_teardown(rdt);

  return 0;
}

DEF_TEST(rdt_config_ngroups__duplicate_proc_name_between_groups) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "proc11,proc12,proc", .type = OCONFIG_TYPE_STRING},
      {.value.string = "proc21,proc,proc23", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(-EINVAL, result);

  /* cleanup */
  stub_rdt_teardown(rdt);

  return 0;
}

DEF_TEST(rdt_config_ngroups__duplicate_proc_name_in_group) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "proc11,proc,proc,proc14", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(-EINVAL, result);

  /* cleanup */
  stub_rdt_teardown(rdt);

  return 0;
}

DEF_TEST(rdt_config_ngroups__empty_group) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "proc11,proc12,proc13", .type = OCONFIG_TYPE_STRING},
      {.value.string = "", .type = OCONFIG_TYPE_STRING},

  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(-EINVAL, result);

  /* cleanup */
  stub_rdt_teardown(rdt);

  return 0;
}

DEF_TEST(rdt_config_ngroups__empty_proc_name) {
  /* setup */
  rdt_ctx_t *rdt = stub_rdt_setup();

  oconfig_value_t values[] = {
      {.value.string = "proc11,,proc13", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .values = values, .values_num = STATIC_ARRAY_SIZE(values),
  };

  /* check */
  int result = rdt_config_ngroups(rdt, &config_item);
  EXPECT_EQ_INT(-EINVAL, result);

  /* cleanup */
  stub_rdt_teardown(rdt);

  return 0;
}

int main(void) {
  RUN_TEST(rdt_config_ngroups__one_process);
  RUN_TEST(rdt_config_ngroups__two_groups);
  RUN_TEST(rdt_config_ngroups__too_long_proc_name);
  RUN_TEST(rdt_config_ngroups__duplicate_proc_name_between_groups);
  RUN_TEST(rdt_config_ngroups__duplicate_proc_name_in_group);
  RUN_TEST(rdt_config_ngroups__empty_group);
  RUN_TEST(rdt_config_ngroups__empty_proc_name);
  END_TEST;
}

#else
DEF_TEST(pqos12_test_stub) {
  EXPECT_EQ_INT(0, 0);
  return 0;
}

int main(void) {
  RUN_TEST(pqos12_test_stub);
  END_TEST;
}
#endif
