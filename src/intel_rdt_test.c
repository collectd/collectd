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
#endif /* LIBPQOS2 */

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

/*
 * NAME
 *   pids_list_find_element
 *
 * DESCRIPTION
 *   Gets index of element in the list matching
 *   given pid. Assumes PIDs are unique, stops searching
 *   on the first match.
 *
 * PARAMETERS
 *   `list'     Pids list
 *   `pid'      PID number to find
 *
 * RETURN VALUE
 *   Index of list element holding given PID.
 */
int pids_list_find_element(pids_list_t *list, const pid_t pid) {
  assert(list);
  int result = -1;
  size_t current = 0;
  while (list != NULL) {
    if (list->pid == pid) {
      result = current;
      break;
    }
    list = list->next;
  }
  return result;
}

/*
 * NAME
 *   pids_list_has_element
 *
 * DESCRIPTION
 *  Checks if the list contains given pid.
 *  Wrapper for pids_list_find_element function.
 *  Used to make tests easier to read.
 *
 * PARAMETERS
 *   `list'     Pids list
 *   `pid'      PID number to find
 *
 * RETURN VALUE
 *   1 if list contains given PID
 *   0 if list does not contain given PID
 */
int pids_list_has_element(pids_list_t *list, const pid_t pid) {
  return pids_list_find_element(list, pid) >= 0 ? 1 : 0;
}

/*
 * NAME
 *   pids_list_free_all
 *
 * DESCRIPTION
 *   Frees memory allocated in the given list
 *
 * PARAMETERS
 *   `list'     Pids list
 */
void pids_list_free_all(pids_list_t *list) {
  while (list) {
    pids_list_t *previous = list;
    list = list->next;
    free(previous);
  }
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
  if (mkdir(proc_fs, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
    return -1;
  }
  char path[256];

  for (size_t i = 0; i < proc_pids_array_length; ++i) {
    memset(path, 0, sizeof(path));
    snprintf(path, STATIC_ARRAY_SIZE(path), "%s/%d", proc_fs,
             proc_pids_array[i].pid);
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    strncat(path, "/comm", STATIC_ARRAY_SIZE(path) - strlen(path) - 1);

    FILE *fp = fopen(path, "w");
    if (!fp) {
      return -2;
    }
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

/***************************************************************************
 * tests
 */
DEF_TEST(add_proc_pid_empty_list) {
  /* setup */
  proc_pids_t proc_pids_instance;
  proc_pids_instance.pids = NULL;
  pid_t pid = 1234;

  /* check */
  pids_list_add_pid(&proc_pids_instance.pids, pid);
  pids_list_t *added = pids_list_get_element(proc_pids_instance.pids, 0);
  EXPECT_EQ_INT(pid, added->pid);

  /* cleanup */
  pids_list_free_all(proc_pids_instance.pids);
  return 0;
}

DEF_TEST(add_proc_pid_non_empty_list) {
  /* setup */
  proc_pids_t proc_pids_instance;
  proc_pids_instance.pids = NULL;
  pid_t pids[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};

  /* check */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i) {
    pids_list_add_pid(&proc_pids_instance.pids, pids[i]);
  }

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i) {
    pids_list_t *added = pids_list_get_element(proc_pids_instance.pids, i);
    EXPECT_EQ_INT(pids[i], added->pid);
  }

  /* cleanup */
  pids_list_free_all(proc_pids_instance.pids);
  return 0;
}

DEF_TEST(get_pid_number_valid_dir) {
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

DEF_TEST(get_pid_number_invalid_dir_name) {
  /* setup */
  struct dirent d;
  sstrncpy(d.d_name, "invalid", STATIC_ARRAY_SIZE(d.d_name));
  d.d_type = DT_DIR;
  pid_t pid = 0;

  /* check */
  int pid_conversion = get_pid_number(&d, &pid);

  EXPECT_EQ_INT(-2, pid_conversion);
  EXPECT_EQ_INT(0, pid);

  /* cleanup */
  return 0;
}

DEF_TEST(read_proc_name_valid_name) {
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

DEF_TEST(read_proc_name_invalid_name) {
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

DEF_TEST(fetch_pids_for_procs_one_proc_many_pid) {
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
    if (0 == strcmp(pp_stubs[i].comm, proc_names[0])) {
      /* check if proc struct has correct pids */
      EXPECT_EQ_INT(pids_list_has_element(output[0].pids, pp_stubs[i].pid), 1);
    } else {
      /* check if proc struct has no incorrect pids */
      EXPECT_EQ_INT(pids_list_has_element(output[0].pids, pp_stubs[i].pid), 0);
    }
  }

  /* cleanup */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(proc_names); ++i) {
    pids_list_free_all(output[i].pids);
  }
  free(output);
  stub_procfs_teardown();
  return 0;
}

DEF_TEST(fetch_pids_for_procs_many_proc_many_pid) {
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
      if (0 == strcmp(pp_stubs[j].comm, proc_names[i])) {
        /* check if proc struct has correct pids */
        EXPECT_EQ_INT(pids_list_has_element(output[i].pids, pp_stubs[j].pid),
                      1);
      } else {
        /* check if proc struct has no incorrect pids */
        EXPECT_EQ_INT(pids_list_has_element(output[i].pids, pp_stubs[j].pid),
                      0);
      }
    }
  }

  /* cleanup */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(proc_names); ++i) {
    pids_list_free_all(output[i].pids);
  }
  free(output);
  stub_procfs_teardown();
  return 0;
}

int main(void) {
  stub_procfs_teardown();
  RUN_TEST(add_proc_pid_empty_list);
  RUN_TEST(add_proc_pid_non_empty_list);
  RUN_TEST(get_pid_number_valid_dir);
  RUN_TEST(get_pid_number_invalid_dir_name);
  RUN_TEST(read_proc_name_valid_name);
  RUN_TEST(read_proc_name_invalid_name);
  RUN_TEST(fetch_pids_for_procs_one_proc_many_pid);
  RUN_TEST(fetch_pids_for_procs_many_proc_many_pid);
  stub_procfs_teardown();
  END_TEST;
}
