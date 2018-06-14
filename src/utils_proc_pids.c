#include "collectd.h"
#include "utils/common/common.h"
#include "utils_proc_pids.h"

#define UTIL_NAME "utils_proc_pids"

void pids_list_free(pids_list_t *list) {
  assert(list);

  pids_list_t *current = list;
  while (current != NULL) {
    pids_list_t *previous = current;
    current = current->next;
    sfree(previous);
  }
}

int is_proc_name_valid(const char *name) {

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

int pids_list_add_pid(pids_list_t **list, const pid_t pid) {
  assert(list);

  pids_list_t *new_element = calloc(1, sizeof(*new_element));

  if (new_element == NULL) {
    ERROR(UTIL_NAME ": Alloc error\n");
    return -1;
  }
  new_element->pid = pid;
  new_element->next = NULL;

  pids_list_t **current = list;
  while (*current != NULL) {
    current = &((*current)->next);
  }
  *current = new_element;
  return 0;
}

int pids_list_contains_pid(pids_list_t *list, const pid_t pid) {
  assert(list);

  pids_list_t *current = list;
  while (current != NULL) {
    if (current->pid == pid)
      return 1;
    current = current->next;
  }
  return 0;
}

int pids_list_add_pids_list(pids_list_t **dst, pids_list_t *src,
                            size_t *dst_num) {
  assert(dst);
  assert(src);
  assert(dst_num);

  pids_list_t *current = src;
  int ret;

  while (current != NULL) {
    ret = pids_list_add_pid(dst, current->pid);
    if (0 != ret)
      return ret;

    ++(*dst_num);
    current = current->next;
  }

  return 0;
}

int read_proc_name(const char *procfs_path, const struct dirent *pid_entry,
                   char *name, const size_t out_size) {
  assert(procfs_path);
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

int get_pid_number(struct dirent *entry, pid_t *pid) {
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

void pids_list_to_array(pid_t *array, pids_list_t *list,
                        const size_t array_length) {

  assert(list);
  assert(array);
  assert(array_length > 0);

  size_t current = 0;

  while (list != NULL && current < array_length) {
    array[current] = list->pid;
    list = list->next;
    ++current;
  }
}

int initialize_proc_pids(const char **procs_names_array,
                         const size_t procs_names_array_size,
                         proc_pids_t **proc_pids_array) {

  assert(proc_pids_array);
  assert(NULL == *proc_pids_array);

  /* Copy procs names to output array. Initialize pids list with NULL value. */
  (*proc_pids_array) =
      calloc(procs_names_array_size, sizeof(**proc_pids_array));

  if (NULL == (*proc_pids_array))
    return -1;

  for (size_t i = 0; i < procs_names_array_size; ++i) {
    sstrncpy((*proc_pids_array)[i].proccess_name, procs_names_array[i],
             STATIC_ARRAY_SIZE((*proc_pids_array)[i].proccess_name));
    (*proc_pids_array)[i].pids = NULL;
  }

  return 0;
}

int fetch_pids_for_procs(const char *procfs_path,
                         const char **procs_names_array,
                         const size_t procs_names_array_size,
                         proc_pids_t **proc_pids_array) {
  assert(procfs_path);
  assert(procs_names_array);
  assert(procs_names_array_size);

  DIR *proc_dir = opendir(procfs_path);
  if (proc_dir == NULL) {
    ERROR(UTIL_NAME ": Could not open %s directory, error: %d", procfs_path,
          errno);
    return -1;
  }

  int init_result = initialize_proc_pids(
      procs_names_array, procs_names_array_size, proc_pids_array);
  if (0 != init_result)
    return -1;

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
    if (read_result <= 0) {
      ERROR(UTIL_NAME ": Comm file skipped. Read result: %d", read_result);
      continue;
    }

    /* Try to find comm in input procs array (proc_pids_array has same names) */
    for (size_t i = 0; i < procs_names_array_size; ++i) {
      if (0 == strncmp(comm, (*proc_pids_array)[i].proccess_name,
                       STATIC_ARRAY_SIZE(comm)))
        pids_list_add_pid(&((*proc_pids_array)[i].pids), pid);
    }
  }

  int close_result = closedir(proc_dir);
  if (0 != close_result) {
    ERROR(UTIL_NAME ": failed to close %s directory, error: %d", procfs_path,
          errno);
    sfree((*proc_pids_array));
    return -1;
  }
  return 0;
}

int pids_list_diff(pids_list_t *prev, pids_list_t *curr, pids_list_t **added,
                   size_t *added_num, pids_list_t **removed,
                   size_t *removed_num) {
  assert(prev || curr);
  assert(added);
  assert(removed);

  if (NULL == prev) {
    /* append all PIDs from curr to added*/
    return pids_list_add_pids_list(added, curr, added_num);
  } else if (NULL == curr) {
    /* append all PIDs from prev to removed*/
    return pids_list_add_pids_list(removed, prev, removed_num);
  }

  pids_list_t *item = prev;
  while (item != NULL) {
    if (0 == pids_list_contains_pid(curr, item->pid)) {
      int add_result = pids_list_add_pid(removed, item->pid);
      if (add_result < 0)
        return add_result;
      ++(*removed_num);
    }
    item = item->next;
  }

  item = curr;
  while (item != NULL) {
    if (0 == pids_list_contains_pid(prev, item->pid)) {
      int add_result = pids_list_add_pid(added, item->pid);
      if (add_result < 0)
        return add_result;
      ++(*added_num);
    }
    item = item->next;
  }

  return 0;
}
