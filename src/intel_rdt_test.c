#include "intel_rdt.c" /* sic */
#include "testing.h"
#include <sys/stat.h>

/***************************************************************************
 * PQOS mocks
 */
#if PQOS_VERSION >= 30000
int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg,
                     const enum pqos_cdp_config l2_cdp_cfg,
		     const enum pqos_mba_config mba_cfg) {
  return 0;
}
#elif PQOS_VERSION >= 2000
int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg,
                     const enum pqos_cdp_config l2_cdp_cfg) {
  return 0;
}
#else
int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg) {
  return 0;
}
#endif

#ifdef LIBPQOS2
/***************************************************************************
 * PQOS v2.0 mocks
 */
int pqos_mon_reset(void) { return 0; }
int pqos_mon_assoc_get(const unsigned lcore, pqos_rmid_t *rmid) { return 0; }
int pqos_mon_start(const unsigned num_cores, const unsigned *cores,
                   const enum pqos_mon_event event, void *context,
                   struct pqos_mon_data *group) {
  return 0;
}
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
int pqos_mon_stop(struct pqos_mon_data *group) { return 0; }
int pqos_mon_poll(struct pqos_mon_data **groups, const unsigned num_groups) {
  return 0;
}
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
/***************************************************************************
 * helper functions
 */

/*
 * NAME
 *   pids_list_get_element
 *
 * DESCRIPTION
 *   Gets list element at index position. Assumes list was created by
 *   pids_list_add_pid function.
 *
 * PARAMETERS
 *   `list'      Pids list
 *   `index'     Position of desired element relative to given list pointer.
 *
 * RETURN VALUE
 *   Pointer to element at index position.
 *   NULL if index exceeds list's length.
 */
pids_list_t *pids_list_get_element(pids_list_t *list, const size_t index) {
  assert(list);
  size_t current = 0;
  while (list != NULL && current != index) {
    list = list->next;
    current++;
  }
  return list;
}

typedef struct stub_proc_pid {
  proc_comm_t comm;
  pid_t pid;
} stub_proc_pid_t;

static const char *proc_fs = "/tmp/procfs_stub";

/*
 * NAME
 *   stub_procfs_setup
 *
 * DESCRIPTION
 *   Prepares testing environment by creating temporary
 *   PID/comm file structure.
 *
 * PARAMETERS
 *   `proc_pids_array'          Array of stub_proc_pid_t structs. Represents
 *                              which PIDs should hold given process name.
 *   `proc_pids_array_length'   Element count of input array.
 *
 * RETURN VALUE
 *   0 on success.
 *   -1 on base dir creation error.
 *   -2 on comm file creation error.
 */
int stub_procfs_setup(const stub_proc_pid_t *proc_pids_array,
                      const size_t proc_pids_array_length) {
  if (mkdir(proc_fs, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
    return -1;
  char path[256];

  for (size_t i = 0; i < proc_pids_array_length; ++i) {
    memset(path, 0, sizeof(path));
    snprintf(path, STATIC_ARRAY_SIZE(path), "%s/%d", proc_fs,
             proc_pids_array[i].pid);
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    strncat(path, "/comm", STATIC_ARRAY_SIZE(path) - strlen(path) - 1);

    FILE *fp = fopen(path, "w");
    if (!fp)
      return -2;
    fwrite(proc_pids_array[i].comm, sizeof(char),
           strlen(proc_pids_array[i].comm), fp);
    fclose(fp);
  }
  return 0;
}

/*
 * NAME
 *   stub_procfs_teardown
 *
 * DESCRIPTION
 *   Clears testing environment: removes stub proc files.
 *   NOTE - This function could be implemented by usage of nftw, but this
 *   would require #define _XOPEN_SOURCE 500, which
 *   messes up intel_rdt includes.
 *
 * RETURN VALUE
 *   system command result
 */
int stub_procfs_teardown() {
  char cmd[256];
  sstrncpy(cmd, "rm -rf ", STATIC_ARRAY_SIZE(cmd));
  strncat(cmd, proc_fs, STATIC_ARRAY_SIZE(cmd) - strlen(cmd) - 1);
  return system(cmd);
}

/* Max PID value. More info:
 * http://web.archive.org/web/20111209081734/http://research.cs.wisc.edu/condor/condorg/linux_scalability.html
 */
#define MAX_PID 4194304
#define MAX_PID_STR "4194304"

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
DEF_TEST(initialize_proc_pids__on_nullptr) {
  /* setup */
  const char *procs_names_array[] = {"proc1", "proc2", "proc3"};
  const size_t procs_names_array_size = STATIC_ARRAY_SIZE(procs_names_array);
  proc_pids_t *proc_pids_array = NULL;

  /* check */
  int result = initialize_proc_pids(procs_names_array, procs_names_array_size,
                                    &proc_pids_array);
  EXPECT_EQ_INT(0, result);
  for (size_t i = 0; i < procs_names_array_size; ++i)
    EXPECT_EQ_STR(procs_names_array[i], proc_pids_array[i].proccess_name);

  /* cleanup */
  free(proc_pids_array);
  return 0;
}

DEF_TEST(add_proc_pid__empty_list) {
  /* setup */
  proc_pids_t proc_pids_instance;
  proc_pids_instance.pids = NULL;
  pid_t pid = 1234;

  /* check */
  pids_list_add_pid(&proc_pids_instance.pids, pid);
  pids_list_t *added = pids_list_get_element(proc_pids_instance.pids, 0);
  EXPECT_EQ_INT(pid, added->pid);

  /* cleanup */
  pids_list_free(proc_pids_instance.pids);
  return 0;
}

DEF_TEST(add_proc_pid__non_empty_list) {
  /* setup */
  proc_pids_t proc_pids_instance;
  proc_pids_instance.pids = NULL;
  pid_t pids[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};

  /* check */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i)
    pids_list_add_pid(&proc_pids_instance.pids, pids[i]);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i) {
    pids_list_t *added = pids_list_get_element(proc_pids_instance.pids, i);
    EXPECT_EQ_INT(pids[i], added->pid);
  }

  /* cleanup */
  pids_list_free(proc_pids_instance.pids);
  return 0;
}

DEF_TEST(pids_list_to_array__non_empty_list) {
  /* setup */
  pid_t pids[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pids_list_t *pids_list = NULL;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i)
    pids_list_add_pid(&pids_list, pids[i]);

  /* check */
  pid_t target_array[STATIC_ARRAY_SIZE(pids)];
  pids_list_to_array(target_array, pids_list, STATIC_ARRAY_SIZE(target_array));
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i)
    EXPECT_EQ_INT(pids[i], target_array[i]);

  /* cleanup */
  pids_list_free(pids_list);
  return 0;
}

DEF_TEST(pids_list_add_pids_list__non_empty_lists) {
  /* setup */
  pid_t pids_array_1[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pid_t pids_array_2[] = {2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007};
  pids_list_t *pids_list_1 = NULL;
  pids_list_t *pids_list_2 = NULL;
  size_t increase = 0;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_1); ++i) {
    pids_list_add_pid(&pids_list_1, pids_array_1[i]);
    pids_list_add_pid(&pids_list_2, pids_array_2[i]);
  }

  /* check */
  int result = pids_list_add_pids_list(&pids_list_1, pids_list_2, &increase);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array_2), increase);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_1); ++i) {
    EXPECT_EQ_INT(1, pids_list_contains_pid(pids_list_1, pids_array_1[i]));
    EXPECT_EQ_INT(1, pids_list_contains_pid(pids_list_1, pids_array_2[i]));
  }

  /* setup */
  pids_list_free(pids_list_1);
  pids_list_free(pids_list_2);
  return 0;
}

DEF_TEST(pids_list_add_pids_list__add_to_empty) {
  /* setup */
  pid_t pids_array[] = {2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007};
  pids_list_t *pids_list_1 = NULL;
  pids_list_t *pids_list_2 = NULL;
  size_t increase = 0;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array); ++i)
    pids_list_add_pid(&pids_list_2, pids_array[i]);

  /* check */
  int result = pids_list_add_pids_list(&pids_list_1, pids_list_2, &increase);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array), increase);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array); ++i)
    EXPECT_EQ_INT(1, pids_list_contains_pid(pids_list_1, pids_array[i]));

  /* setup */
  pids_list_free(pids_list_1);
  pids_list_free(pids_list_2);
  return 0;
}

DEF_TEST(get_pid_number__valid_dir) {
  /* setup */
  struct dirent d;
  sstrncpy(d.d_name, MAX_PID_STR, STATIC_ARRAY_SIZE(d.d_name));
  d.d_type = DT_DIR;
  pid_t pid = 0;

  /* check */
  int pid_conversion = get_pid_number(&d, &pid);

  EXPECT_EQ_INT(0, pid_conversion);
  EXPECT_EQ_INT(MAX_PID, pid);

  /* cleanup */
  return 0;
}

DEF_TEST(get_pid_number__invalid_dir_name) {
  /* setup */
  struct dirent d;
  sstrncpy(d.d_name, "invalid", STATIC_ARRAY_SIZE(d.d_name));
  d.d_type = DT_DIR;
  pid_t pid = 0;

  /* check */
  int pid_conversion = get_pid_number(&d, &pid);

  EXPECT_EQ_INT(-1, pid_conversion);
  EXPECT_EQ_INT(0, pid);

  /* cleanup */
  return 0;
}

DEF_TEST(read_proc_name__valid_name) {
  /* setup */
  stub_proc_pid_t pp_stubs[] = {{"proc1", MAX_PID}};
  stub_procfs_setup(pp_stubs, STATIC_ARRAY_SIZE(pp_stubs));
  struct dirent d;
  sstrncpy(d.d_name, MAX_PID_STR, STATIC_ARRAY_SIZE(d.d_name));
  d.d_type = DT_DIR;

  /* check */
  proc_comm_t comm;
  int read_result = read_proc_name(proc_fs, &d, comm, STATIC_ARRAY_SIZE(comm));

  EXPECT_EQ_INT(strlen(pp_stubs[0].comm), read_result);
  EXPECT_EQ_STR(pp_stubs[0].comm, comm);

  /* cleanup */
  stub_procfs_teardown();
  return 0;
}

DEF_TEST(read_proc_name__invalid_name) {
  /* setup */
  struct dirent d;
  sstrncpy(d.d_name, MAX_PID_STR, STATIC_ARRAY_SIZE(d.d_name));
  d.d_type = DT_DIR;

  /* check */
  proc_comm_t comm;
  int read_result = read_proc_name(proc_fs, &d, comm, STATIC_ARRAY_SIZE(comm));

  EXPECT_EQ_INT(-1, read_result);

  /* cleanup */
  return 0;
}

DEF_TEST(fetch_pids_for_procs__one_proc_many_pid) {
  /* setup */
  const char *proc_names[] = {"proc1"};
  stub_proc_pid_t pp_stubs[] = {{"proc1", 1007},
                                {"proc1", 1008},
                                {"proc1", 1009},
                                {"proc2", 1010},
                                {"proc3", 1011}};
  stub_procfs_setup(pp_stubs, STATIC_ARRAY_SIZE(pp_stubs));
  proc_pids_t *output = NULL;

  /* check */
  int result = fetch_pids_for_procs(proc_fs, proc_names,
                                    STATIC_ARRAY_SIZE(proc_names), &output);
  EXPECT_EQ_INT(0, result);

  /* proc name check */
  EXPECT_EQ_STR(proc_names[0], output[0].proccess_name);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pp_stubs); ++i) {
    if (0 == strcmp(pp_stubs[i].comm, proc_names[0]))
      /* check if proc struct has correct pids */
      EXPECT_EQ_INT(pids_list_contains_pid(output[0].pids, pp_stubs[i].pid), 1);
    else
      /* check if proc struct has no incorrect pids */
      EXPECT_EQ_INT(pids_list_contains_pid(output[0].pids, pp_stubs[i].pid), 0);
  }

  /* cleanup */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(proc_names); ++i)
    pids_list_free(output[i].pids);
  free(output);
  stub_procfs_teardown();
  return 0;
}

DEF_TEST(fetch_pids_for_procs__many_proc_many_pid) {
  /* setup */
  const char *proc_names[] = {"proc1", "proc2", "proc3"};
  stub_proc_pid_t pp_stubs[] = {
      {"proc1", 1007}, {"proc1", 1008}, {"proc1", 1009}, {"proc2", 2007},
      {"proc2", 2008}, {"proc2", 2009}, {"proc3", 3007}, {"proc3", 3008},
      {"proc3", 3009}, {"proc4", 4007}, {"proc4", 4008}, {"proc4", 4009},
      {"proc5", 5007}, {"proc5", 5008}, {"proc5", 5009}};
  stub_procfs_setup(pp_stubs, STATIC_ARRAY_SIZE(pp_stubs));
  proc_pids_t *output = NULL;

  /* check */
  int result = fetch_pids_for_procs(proc_fs, proc_names,
                                    STATIC_ARRAY_SIZE(proc_names), &output);
  EXPECT_EQ_INT(0, result);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(proc_names); ++i) {

    /* proc name check */
    EXPECT_EQ_STR(proc_names[i], output[i].proccess_name);

    for (size_t j = 0; j < STATIC_ARRAY_SIZE(pp_stubs); ++j) {
      if (0 == strcmp(pp_stubs[j].comm, proc_names[i]))
        /* check if proc struct has correct pids */
        EXPECT_EQ_INT(pids_list_contains_pid(output[i].pids, pp_stubs[j].pid),
                      1);
      else
        /* check if proc struct has no incorrect pids */
        EXPECT_EQ_INT(pids_list_contains_pid(output[i].pids, pp_stubs[j].pid),
                      0);
    }
  }

  /* cleanup */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(proc_names); ++i)
    pids_list_free(output[i].pids);
  free(output);
  stub_procfs_teardown();
  return 0;
}

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

DEF_TEST(rdt_pid_list_diff__all_changed) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pid_t pids_array_after[] = {2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007};
  pids_list_t *pids_list_before = NULL;
  pids_list_t *pids_list_after = NULL;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_after); ++i) {
    pids_list_add_pid(&pids_list_before, pids_array_before[i]);
    pids_list_add_pid(&pids_list_after, pids_array_after[i]);
  }

  pids_list_t *new_pids = NULL;
  size_t new_pids_count = 0;
  pids_list_t *lost_pids = NULL;
  size_t lost_pids_count = 0;

  /* check */
  int result = rdt_pid_list_diff(pids_list_before, pids_list_after, &new_pids,
                                 &new_pids_count, &lost_pids, &lost_pids_count);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array_before), lost_pids_count);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array_after), new_pids_count);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_before); ++i) {
    EXPECT_EQ_INT(1, pids_list_contains_pid(new_pids, pids_array_after[i]));
    EXPECT_EQ_INT(1, pids_list_contains_pid(lost_pids, pids_array_before[i]));
  }

  /* cleanup */
  pids_list_free(pids_list_before);
  pids_list_free(pids_list_after);
  pids_list_free(new_pids);
  pids_list_free(lost_pids);

  return 0;
}

DEF_TEST(rdt_pid_list_diff__nothing_changed) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pids_list_t *pids_list_before = NULL;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_before); ++i) {
    pids_list_add_pid(&pids_list_before, pids_array_before[i]);
  }

  pids_list_t *new_pids = NULL;
  size_t new_pids_count = 0;
  pids_list_t *lost_pids = NULL;
  size_t lost_pids_count = 0;

  /* check */
  int result = rdt_pid_list_diff(pids_list_before, pids_list_before, &new_pids,
                                 &new_pids_count, &lost_pids, &lost_pids_count);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(0, lost_pids_count);
  EXPECT_EQ_INT(0, new_pids_count);
  OK(NULL == new_pids);
  OK(NULL == lost_pids);

  /* cleanup */
  pids_list_free(pids_list_before);

  return 0;
}

DEF_TEST(rdt_pid_list_diff__one_added) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pid_t pids_array_after[] = {1000, 1001, 1002, 1003, 1004,
                              1005, 1006, 1007, 1008};
  pids_list_t *pids_list_before = NULL;
  pids_list_t *pids_list_after = NULL;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_before); ++i)
    pids_list_add_pid(&pids_list_before, pids_array_before[i]);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_after); ++i)
    pids_list_add_pid(&pids_list_after, pids_array_after[i]);

  pids_list_t *new_pids = NULL;
  size_t new_pids_count = 0;
  pids_list_t *lost_pids = NULL;
  size_t lost_pids_count = 0;

  /* check */
  int result = rdt_pid_list_diff(pids_list_before, pids_list_after, &new_pids,
                                 &new_pids_count, &lost_pids, &lost_pids_count);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(0, lost_pids_count);
  EXPECT_EQ_INT(1, new_pids_count);
  EXPECT_EQ_INT(1008, new_pids->pid);

  /* cleanup */
  pids_list_free(pids_list_before);
  pids_list_free(pids_list_after);
  pids_list_free(new_pids);

  return 0;
}

DEF_TEST(rdt_pid_list_diff__one_removed) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004,
                               1005, 1006, 1007, 1008};
  pid_t pids_array_after[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pids_list_t *pids_list_before = NULL;
  pids_list_t *pids_list_after = NULL;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_before); ++i)
    pids_list_add_pid(&pids_list_before, pids_array_before[i]);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_after); ++i)
    pids_list_add_pid(&pids_list_after, pids_array_after[i]);

  pids_list_t *new_pids = NULL;
  size_t new_pids_count = 0;
  pids_list_t *lost_pids = NULL;
  size_t lost_pids_count = 0;

  /* check */
  int result = rdt_pid_list_diff(pids_list_before, pids_list_after, &new_pids,
                                 &new_pids_count, &lost_pids, &lost_pids_count);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(1, lost_pids_count);
  EXPECT_EQ_INT(0, new_pids_count);
  EXPECT_EQ_INT(1008, lost_pids->pid);

  /* cleanup */
  pids_list_free(pids_list_before);
  pids_list_free(pids_list_after);
  pids_list_free(lost_pids);

  return 0;
}

int main(void) {
  stub_procfs_teardown();
  RUN_TEST(initialize_proc_pids__on_nullptr);
  RUN_TEST(add_proc_pid__empty_list);
  RUN_TEST(add_proc_pid__non_empty_list);
  RUN_TEST(pids_list_to_array__non_empty_list);
  RUN_TEST(pids_list_add_pids_list__non_empty_lists);
  RUN_TEST(pids_list_add_pids_list__add_to_empty);
  RUN_TEST(get_pid_number__valid_dir);
  RUN_TEST(get_pid_number__invalid_dir_name);
  RUN_TEST(read_proc_name__valid_name);
  RUN_TEST(read_proc_name__invalid_name);
  RUN_TEST(fetch_pids_for_procs__one_proc_many_pid);
  RUN_TEST(fetch_pids_for_procs__many_proc_many_pid);
  RUN_TEST(rdt_config_ngroups__one_process);
  RUN_TEST(rdt_config_ngroups__two_groups);
  RUN_TEST(rdt_config_ngroups__too_long_proc_name);
  RUN_TEST(rdt_config_ngroups__duplicate_proc_name_between_groups);
  RUN_TEST(rdt_config_ngroups__duplicate_proc_name_in_group);
  RUN_TEST(rdt_config_ngroups__empty_group);
  RUN_TEST(rdt_config_ngroups__empty_proc_name);
  RUN_TEST(rdt_pid_list_diff__all_changed);
  RUN_TEST(rdt_pid_list_diff__nothing_changed);
  RUN_TEST(rdt_pid_list_diff__one_added);
  RUN_TEST(rdt_pid_list_diff__one_removed);
  stub_procfs_teardown();
  END_TEST;
}

#else
/***************************************************************************
 * PQOS v1.2 mocks
 */
int pqos_mon_reset(void) { return 0; }
int pqos_mon_assoc_get(const unsigned lcore, pqos_rmid_t *rmid) { return 0; }
int pqos_mon_start(const unsigned num_cores, const unsigned *cores,
                   const enum pqos_mon_event event, void *context,
                   struct pqos_mon_data *group) {
  return 0;
}
int pqos_mon_start_pid(const pid_t pids, const enum pqos_mon_event event,
                       void *context, struct pqos_mon_data *group) {
  return 0;
}
int pqos_mon_stop(struct pqos_mon_data *group) { return 0; }
int pqos_mon_poll(struct pqos_mon_data **groups, const unsigned num_groups) {
  return 0;
}
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

DEF_TEST(pqos12_test_stub) {
  EXPECT_EQ_INT(0, 0);
  return 0;
}

int main(void) {
  RUN_TEST(pqos12_test_stub);
  END_TEST;
}
#endif
