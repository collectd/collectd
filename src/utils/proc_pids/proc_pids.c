/**
 * collectd - src/utils/proc_pids/proc_pids.c
 *
 * Copyright(c) 2018-2019 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Starzyk, Mateusz <mateuszx.starzyk@intel.com>
 *   Wojciech Andralojc <wojciechx.andralojc@intel.com>
 *   Michał Aleksiński <michalx.aleksinski@intel.com>
 **/

#include "collectd.h"
#include "utils/common/common.h"
#include "utils/proc_pids/proc_pids.h"

#define UTIL_NAME "utils_proc_pids"

void pids_list_free(pids_list_t *list) {
  assert(list);

  sfree(list->pids);
  sfree(list);
}

int proc_pids_is_name_valid(const char *name) {

  if (name != NULL) {
    unsigned len = strlen(name);
    if (len > 0 && len <= MAX_PROC_NAME_LEN)
      return 1;
    else {
      DEBUG(UTIL_NAME
            ": Process name \'%s\' is too long. Max supported len is %d chars.",
            name, MAX_PROC_NAME_LEN);
    }
  }

  return 0;
}

int pids_list_add_pid(pids_list_t *list, const pid_t pid) {
  assert(list);

  if (list->allocated == list->size) {
    size_t new_allocated = list->allocated + 1 + list->allocated / 10;
    pid_t *new_pids = realloc(list->pids, sizeof(pid_t) * new_allocated);

    if (NULL == new_pids) {
      ERROR(UTIL_NAME ": Alloc error\n");
      return -1;
    }

    list->pids = new_pids;
    list->allocated = new_allocated;
  }

  list->pids[list->size] = pid;
  list->size++;

  return 0;
}

int pids_list_add_list(pids_list_t *dst, pids_list_t *src) {
  assert(dst);
  assert(src);

  if (dst->allocated < dst->size + src->size) {
    pid_t *new_pids =
        realloc(dst->pids, sizeof(pid_t) * (dst->size + src->size));

    if (NULL == new_pids) {
      ERROR(UTIL_NAME ": Alloc error\n");
      return -1;
    }

    dst->allocated = dst->size + src->size;
    dst->pids = new_pids;
  }

  memcpy(dst->pids + dst->size, src->pids, src->size * sizeof(*(src->pids)));
  dst->size += src->size;

  return 0;
}

int pids_list_clear(pids_list_t *list) {
  assert(list);

  if (list->pids != NULL)
    sfree(list->pids);

  list->size = 0;
  list->allocated = 0;

  return 0;
}

int pids_list_contains_pid(pids_list_t *list, const pid_t pid) {
  assert(list);

  for (int i = 0; i < list->size; i++)
    if (list->pids[i] == pid)
      return 1;

  return 0;
}

/*
 * NAME
 *   read_proc_name
 *
 * DESCRIPTION
 *   Reads process name from given pid directory.
 *   Strips new-line character (\n).
 *
 * PARAMETERS
 *   `procfs_path' Path to systems proc directory (e.g. /proc)
 *   `pid_entry'   Dirent for PID directory
 *   `name'        Output buffer for process name, recommended proc_comm.
 *   `out_size'    Output buffer size, recommended sizeof(proc_comm)
 *
 * RETURN VALUE
 *   On success, the number of read bytes (includes stripped \n).
 *   -1 on file open error
*/
static int read_proc_name(const char *procfs_path,
                          const struct dirent *pid_entry, char *name,
                          const size_t out_size) {
  assert(pid_entry);
  assert(name);
  assert(out_size);
  memset(name, 0, out_size);

  const char *comm_file_name = "comm";

  char *path = ssnprintf_alloc("%s/%s/%s", procfs_path, pid_entry->d_name,
                               comm_file_name);
  if (path == NULL)
    return -1;
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ERROR(UTIL_NAME ": Failed to open comm file, error: %d\n", errno);
    sfree(path);
    return -1;
  }
  size_t read_length = fread(name, sizeof(char), out_size, f);
  name[out_size - 1] = '\0';
  fclose(f);
  sfree(path);
  /* strip new line ending */
  char *newline = strchr(name, '\n');
  if (newline) {
    *newline = '\0';
  }

  return read_length;
}

/*
 * NAME
 *   get_pid_number
 *
 * DESCRIPTION
 *   Gets pid number for given /proc/pid directory entry or
 *   returns error if input directory does not hold PID information.
 *
 * PARAMETERS
 *   `entry'    Dirent for PID directory
 *   `pid'      PID number to be filled
 *
 * RETURN VALUE
 *   0 on success. -1 on error.
 */
static int get_pid_number(struct dirent *entry, pid_t *pid) {
  char *tmp_end; /* used for strtoul error check*/

  if (pid == NULL || entry == NULL)
    return -1;

  if (entry->d_type != DT_DIR)
    return -1;

  /* trying to get pid number from directory name*/
  *pid = strtoul(entry->d_name, &tmp_end, 10);
  if (*tmp_end != '\0') {
    return -1; /* conversion failed, not proc-pid */
  }
  /* all checks passed, marking as success */
  return 0;
}

int proc_pids_init(const char **procs_names_array,
                   const size_t procs_names_array_size,
                   proc_pids_t **proc_pids[]) {

  proc_pids_t **proc_pids_array;
  assert(proc_pids);
  assert(NULL == *proc_pids);

  /* Copy procs names to output array. Initialize pids list with NULL value. */
  proc_pids_array = calloc(procs_names_array_size, sizeof(*proc_pids_array));

  if (NULL == proc_pids_array)
    return -1;

  for (size_t i = 0; i < procs_names_array_size; ++i) {
    proc_pids_array[i] = calloc(1, sizeof(**proc_pids_array));
    if (NULL == proc_pids_array[i])
      goto proc_pids_init_error;

    sstrncpy(proc_pids_array[i]->process_name, procs_names_array[i],
             STATIC_ARRAY_SIZE(proc_pids_array[i]->process_name));
    proc_pids_array[i]->prev = NULL;
    proc_pids_array[i]->curr = NULL;
  }

  *proc_pids = proc_pids_array;

  return 0;
proc_pids_init_error:
  if (NULL != proc_pids_array) {
    for (size_t i = 0; i < procs_names_array_size; ++i) {
      free(proc_pids_array[i]);
    }
    free(proc_pids_array);
  }
  return -1;
}

static void swap_proc_pids(proc_pids_t **proc_pids, size_t proc_pids_num) {
  for (size_t i = 0; i < proc_pids_num; i++) {
    pids_list_t *swap = proc_pids[i]->prev;
    proc_pids[i]->prev = proc_pids[i]->curr;
    proc_pids[i]->curr = swap;
  }
}

int proc_pids_update(const char *procfs_path, proc_pids_t **proc_pids,
                     size_t proc_pids_num) {
  assert(procfs_path);
  assert(proc_pids);

  DIR *proc_dir = opendir(procfs_path);
  if (proc_dir == NULL) {
    ERROR(UTIL_NAME ": Could not open %s directory, error: %d", procfs_path,
          errno);
    return -1;
  }

  swap_proc_pids(proc_pids, proc_pids_num);

  for (size_t i = 0; i < proc_pids_num; i++) {
    if (NULL == proc_pids[i]->curr)
      proc_pids[i]->curr = calloc(1, sizeof(*(proc_pids[i]->curr)));

    if (NULL == proc_pids[i]->curr) {
      ERROR(UTIL_NAME ": Alloc error\n");
      goto update_error;
    }

    proc_pids[i]->curr->size = 0;
  }

  /* Go through procfs and find PIDS and their comms */
  struct dirent *entry;
  while ((entry = readdir(proc_dir)) != NULL) {
    pid_t pid;
    int pid_conversion = get_pid_number(entry, &pid);
    if (pid_conversion < 0)
      continue;

    proc_comm_t comm;
    int read_result =
        read_proc_name(procfs_path, entry, comm, sizeof(proc_comm_t));
    if (read_result <= 0)
      continue;

    /* Try to find comm in input procs array */
    for (size_t i = 0; i < proc_pids_num; ++i) {
      if (0 ==
          strncmp(comm, proc_pids[i]->process_name, STATIC_ARRAY_SIZE(comm)))
        pids_list_add_pid(proc_pids[i]->curr, pid);
    }
  }

  int close_result = closedir(proc_dir);
  if (0 != close_result) {
    ERROR(UTIL_NAME ": failed to close /proc directory, error: %d", errno);
    goto update_error;
  }
  return 0;

update_error:
  swap_proc_pids(proc_pids, proc_pids_num);
  return -1;
}

int pids_list_diff(proc_pids_t *proc, pids_list_t *added,
                   pids_list_t *removed) {
  assert(proc);
  assert(added);
  assert(removed);

  added->size = 0;
  removed->size = 0;

  if (NULL == proc->prev || 0 == proc->prev->size) {
    /* append all PIDs from curr to added*/
    return pids_list_add_list(added, proc->curr);
  } else if (NULL == proc->curr || 0 == proc->curr->size) {
    /* append all PIDs from prev to removed*/
    return pids_list_add_list(removed, proc->prev);
  }

  for (int i = 0; i < proc->prev->size; i++)
    if (0 == pids_list_contains_pid(proc->curr, proc->prev->pids[i])) {
      int add_result = pids_list_add_pid(removed, proc->prev->pids[i]);
      if (add_result < 0)
        return add_result;
    }

  for (int i = 0; i < proc->curr->size; i++)
    if (0 == pids_list_contains_pid(proc->prev, proc->curr->pids[i])) {
      int add_result = pids_list_add_pid(added, proc->curr->pids[i]);
      if (add_result < 0)
        return add_result;
    }

  return 0;
}

int proc_pids_free(proc_pids_t *proc_pids[], size_t proc_pids_num) {
  for (size_t i = 0; i < proc_pids_num; i++) {
    if (NULL != proc_pids[i]->curr)
      pids_list_free(proc_pids[i]->curr);
    if (NULL != proc_pids[i]->prev)
      pids_list_free(proc_pids[i]->prev);
    sfree(proc_pids[i]);
  }
  sfree(proc_pids);

  return 0;
}
