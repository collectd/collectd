#include "testing.h"
#include "utils/proc_pids/proc_pids.c" /* sic */
#include <sys/stat.h>

/***************************************************************************
 * helper functions
 */

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
 *   -3 on comm file write error.
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

    size_t slen = strlen(proc_pids_array[i].comm);
    size_t wlen = fwrite(proc_pids_array[i].comm, sizeof(char), slen, fp);
    fclose(fp);

    if (slen != wlen)
      return -3;
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
DEF_TEST(proc_pids_init__on_nullptr) {
  /* setup */
  const char *procs_names_array[] = {"proc1", "proc2", "proc3"};
  const size_t procs_names_array_size = STATIC_ARRAY_SIZE(procs_names_array);
  proc_pids_t **proc_pids_array = NULL;

  /* check */
  int result = proc_pids_init(procs_names_array, procs_names_array_size,
                              &proc_pids_array);
  EXPECT_EQ_INT(0, result);
  for (size_t i = 0; i < procs_names_array_size; ++i)
    EXPECT_EQ_STR(procs_names_array[i], proc_pids_array[i]->process_name);

  /* cleanup */
  proc_pids_free(proc_pids_array, procs_names_array_size);
  return 0;
}

DEF_TEST(pid_list_add_pid__empty_list) {
  /* setup */
  pids_list_t *proc_pids_instance = calloc(1, sizeof(*proc_pids_instance));
  pid_t pid = 1234;

  /* check */
  pids_list_add_pid(proc_pids_instance, pid);
  EXPECT_EQ_INT(pid, proc_pids_instance->pids[0]);

  /* cleanup */
  pids_list_free(proc_pids_instance);
  return 0;
}

DEF_TEST(pid_list_add_pid__non_empty_list) {
  /* setup */
  pids_list_t *proc_pids_instance = calloc(1, sizeof(*proc_pids_instance));
  pid_t pids[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};

  /* check */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i)
    pids_list_add_pid(proc_pids_instance, pids[i]);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids); ++i) {
    EXPECT_EQ_INT(pids[i], proc_pids_instance->pids[i]);
  }

  /* cleanup */
  pids_list_free(proc_pids_instance);
  return 0;
}

DEF_TEST(pids_list_add_pids_list__non_empty_lists) {
  /* setup */
  pid_t pids_array_1[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pid_t pids_array_2[] = {2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007};
  pids_list_t *pids_list_1 = calloc(1, sizeof(*pids_list_1));
  pids_list_t *pids_list_2 = calloc(1, sizeof(*pids_list_2));
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_1); ++i) {
    pids_list_add_pid(pids_list_1, pids_array_1[i]);
    pids_list_add_pid(pids_list_2, pids_array_2[i]);
  }

  /* check */
  int result = pids_list_add_list(pids_list_1, pids_list_2);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array_2) +
                    STATIC_ARRAY_SIZE(pids_array_1),
                pids_list_1->size);

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
  pids_list_t *pids_list_1 = calloc(1, sizeof(*pids_list_1));
  pids_list_t *pids_list_2 = calloc(1, sizeof(*pids_list_2));
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array); ++i)
    pids_list_add_pid(pids_list_2, pids_array[i]);

  /* check */
  int result = pids_list_add_list(pids_list_1, pids_list_2);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array), pids_list_1->size);

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

DEF_TEST(proc_pids_update__one_proc_many_pid) {
  /* setup */
  const char *proc_names[] = {"proc1"};
  stub_proc_pid_t pp_stubs[] = {{"proc1", 1007},
                                {"proc1", 1008},
                                {"proc1", 1009},
                                {"proc2", 1010},
                                {"proc3", 1011}};
  proc_pids_t **proc_pids = NULL;
  int result;
  stub_procfs_setup(pp_stubs, STATIC_ARRAY_SIZE(pp_stubs));

  result =
      proc_pids_init(proc_names, STATIC_ARRAY_SIZE(proc_names), &proc_pids);
  EXPECT_EQ_INT(0, result);

  /* check */
  result = proc_pids_update(proc_fs, proc_pids, STATIC_ARRAY_SIZE(proc_names));
  EXPECT_EQ_INT(0, result);

  /* proc name check */
  EXPECT_EQ_STR(proc_names[0], proc_pids[0]->process_name);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pp_stubs); ++i) {
    if (0 == strcmp(pp_stubs[i].comm, proc_names[0]))
      /* check if proc struct has correct pids */
      EXPECT_EQ_INT(pids_list_contains_pid(proc_pids[0]->curr, pp_stubs[i].pid),
                    1);
    else
      /* check if proc struct has no incorrect pids */
      EXPECT_EQ_INT(pids_list_contains_pid(proc_pids[0]->curr, pp_stubs[i].pid),
                    0);
  }

  /* cleanup */
  proc_pids_free(proc_pids, STATIC_ARRAY_SIZE(proc_names));
  stub_procfs_teardown();
  return 0;
}

DEF_TEST(proc_pids_update__many_proc_many_pid) {
  /* setup */
  const char *proc_names[] = {"proc1", "proc2", "proc3"};
  stub_proc_pid_t pp_stubs[] = {
      {"proc1", 1007}, {"proc1", 1008}, {"proc1", 1009}, {"proc2", 2007},
      {"proc2", 2008}, {"proc2", 2009}, {"proc3", 3007}, {"proc3", 3008},
      {"proc3", 3009}, {"proc4", 4007}, {"proc4", 4008}, {"proc4", 4009},
      {"proc5", 5007}, {"proc5", 5008}, {"proc5", 5009}};
  proc_pids_t **proc_pids = NULL;
  int result;
  stub_procfs_setup(pp_stubs, STATIC_ARRAY_SIZE(pp_stubs));

  result =
      proc_pids_init(proc_names, STATIC_ARRAY_SIZE(proc_names), &proc_pids);
  EXPECT_EQ_INT(0, result);

  /* check */
  result = proc_pids_update(proc_fs, proc_pids, STATIC_ARRAY_SIZE(proc_names));
  EXPECT_EQ_INT(0, result);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(proc_names); ++i) {

    /* proc name check */
    EXPECT_EQ_STR(proc_names[i], proc_pids[i]->process_name);

    for (size_t j = 0; j < STATIC_ARRAY_SIZE(pp_stubs); ++j) {
      if (0 == strcmp(pp_stubs[j].comm, proc_names[i]))
        /* check if proc struct has correct pids */
        EXPECT_EQ_INT(
            pids_list_contains_pid(proc_pids[i]->curr, pp_stubs[j].pid), 1);
      else
        /* check if proc struct has no incorrect pids */
        EXPECT_EQ_INT(
            pids_list_contains_pid(proc_pids[i]->curr, pp_stubs[j].pid), 0);
    }
  }

  /* cleanup */
  proc_pids_free(proc_pids, STATIC_ARRAY_SIZE(proc_names));
  stub_procfs_teardown();
  return 0;
}

DEF_TEST(pids_list_diff__all_changed) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pid_t pids_array_after[] = {2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007};
  proc_pids_t proc_pids;
  pids_list_t curr;
  pids_list_t prev;

  prev.pids = pids_array_before;
  prev.size = STATIC_ARRAY_SIZE(pids_array_before);
  prev.allocated = prev.size;
  curr.pids = pids_array_after;
  curr.size = STATIC_ARRAY_SIZE(pids_array_after);
  curr.allocated = curr.size;
  proc_pids.curr = &curr;
  proc_pids.prev = &prev;

  pids_list_t *new_pids = calloc(1, sizeof(*new_pids));
  pids_list_t *lost_pids = calloc(1, sizeof(*lost_pids));

  /* check */
  int result = pids_list_diff(&proc_pids, new_pids, lost_pids);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array_before), lost_pids->size);
  EXPECT_EQ_INT(STATIC_ARRAY_SIZE(pids_array_after), new_pids->size);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(pids_array_before); ++i) {
    EXPECT_EQ_INT(1, pids_list_contains_pid(new_pids, pids_array_after[i]));
    EXPECT_EQ_INT(1, pids_list_contains_pid(lost_pids, pids_array_before[i]));
  }

  /* cleanup */
  pids_list_free(new_pids);
  pids_list_free(lost_pids);

  return 0;
}

DEF_TEST(pids_list_diff__nothing_changed) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  proc_pids_t proc_pids;
  pids_list_t curr;
  pids_list_t prev;

  prev.pids = pids_array_before;
  prev.size = STATIC_ARRAY_SIZE(pids_array_before);
  prev.allocated = prev.size;
  curr.pids = pids_array_before;
  curr.size = STATIC_ARRAY_SIZE(pids_array_before);
  curr.allocated = curr.size;
  proc_pids.curr = &curr;
  proc_pids.prev = &prev;

  pids_list_t *new_pids = calloc(1, sizeof(*new_pids));
  pids_list_t *lost_pids = calloc(1, sizeof(*lost_pids));

  /* check */
  int result = pids_list_diff(&proc_pids, new_pids, lost_pids);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(0, lost_pids->size);
  EXPECT_EQ_INT(0, new_pids->size);

  /* cleanup */
  pids_list_free(lost_pids);
  pids_list_free(new_pids);

  return 0;
}

DEF_TEST(pids_list_diff__one_added) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};
  pid_t pids_array_after[] = {1000, 1001, 1002, 1003, 1004,
                              1005, 1006, 1007, 1008};
  proc_pids_t proc_pids;
  pids_list_t curr;
  pids_list_t prev;

  prev.pids = pids_array_before;
  prev.size = STATIC_ARRAY_SIZE(pids_array_before);
  prev.allocated = prev.size;
  curr.pids = pids_array_after;
  curr.size = STATIC_ARRAY_SIZE(pids_array_after);
  curr.allocated = curr.size;
  proc_pids.curr = &curr;
  proc_pids.prev = &prev;

  pids_list_t *new_pids = calloc(1, sizeof(*new_pids));
  pids_list_t *lost_pids = calloc(1, sizeof(*lost_pids));

  /* check */
  int result = pids_list_diff(&proc_pids, new_pids, lost_pids);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(0, lost_pids->size);
  EXPECT_EQ_INT(1, new_pids->size);
  EXPECT_EQ_INT(1008, new_pids->pids[0]);

  /* cleanup */
  pids_list_free(lost_pids);
  pids_list_free(new_pids);

  return 0;
}

DEF_TEST(pids_list_diff__one_removed) {
  /* setup */
  pid_t pids_array_before[] = {1000, 1001, 1002, 1003, 1004,
                               1005, 1006, 1007, 1008};
  pid_t pids_array_after[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007};

  proc_pids_t proc_pids;
  pids_list_t curr;
  pids_list_t prev;

  prev.pids = pids_array_before;
  prev.size = STATIC_ARRAY_SIZE(pids_array_before);
  prev.allocated = prev.size;
  curr.pids = pids_array_after;
  curr.size = STATIC_ARRAY_SIZE(pids_array_after);
  curr.allocated = curr.size;
  proc_pids.curr = &curr;
  proc_pids.prev = &prev;

  pids_list_t *new_pids = calloc(1, sizeof(*new_pids));
  pids_list_t *lost_pids = calloc(1, sizeof(*lost_pids));

  /* check */
  int result = pids_list_diff(&proc_pids, new_pids, lost_pids);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(0, new_pids->size);
  EXPECT_EQ_INT(1, lost_pids->size);
  EXPECT_EQ_INT(1008, lost_pids->pids[0]);

  /* cleanup */
  pids_list_free(lost_pids);
  pids_list_free(new_pids);

  return 0;
}

int main(void) {
  stub_procfs_teardown();
  RUN_TEST(proc_pids_init__on_nullptr);
  RUN_TEST(pid_list_add_pid__empty_list);
  RUN_TEST(pid_list_add_pid__non_empty_list);
  RUN_TEST(pids_list_add_pids_list__non_empty_lists);
  RUN_TEST(pids_list_add_pids_list__add_to_empty);
  RUN_TEST(get_pid_number__valid_dir);
  RUN_TEST(get_pid_number__invalid_dir_name);
  RUN_TEST(read_proc_name__valid_name);
  RUN_TEST(read_proc_name__invalid_name);
  RUN_TEST(proc_pids_update__one_proc_many_pid);
  RUN_TEST(proc_pids_update__many_proc_many_pid);
  RUN_TEST(pids_list_diff__all_changed);
  RUN_TEST(pids_list_diff__nothing_changed);
  RUN_TEST(pids_list_diff__one_added);
  RUN_TEST(pids_list_diff__one_removed);
  stub_procfs_teardown();
  END_TEST;
}
