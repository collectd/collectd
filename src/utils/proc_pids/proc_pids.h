/**
 * collectd - src/utils/proc_pids/proc_pids.h
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

#include <dirent.h>
#include <sys/types.h>

/*
 * Process name inside comm file is limited to 16 chars.
 * More info here: http://man7.org/linux/man-pages/man5/proc.5.html
 */
#define MAX_PROC_NAME_LEN 16

/* Helper typedef for process name array
 * Extra 1 char is added for string null termination.
 */
typedef char proc_comm_t[MAX_PROC_NAME_LEN + 1];

/* List of pids. */
typedef struct pids_list_s {
  pid_t *pids;
  size_t size;
  size_t allocated;
} pids_list_t;

/* Holds process name and list of pids assigned to that name */
typedef struct proc_pids_s {
  proc_comm_t process_name;
  pids_list_t *prev;
  pids_list_t *curr;
} proc_pids_t;

/*
 * NAME
 *   pids_list_free
 *
 * DESCRIPTION
 *   Free all elements of given pids list
 *
 * PARAMETERS
 *   `list'     Head of target pids_list.
 */
void pids_list_free(pids_list_t *list);

/*
 * NAME
 *   pids_list_add_pid
 *
 * DESCRIPTION
 *   Adds pid at the end of the pids array.
 *   Reallocates memory for new pid element, it is up to user to free it.
 *
 * PARAMETERS
 *   `list'     Target pids_list.
 *   `pid'      Pid to be added.
 *
 * RETURN VALUE
 *   On success, returns 0.
 *   -1 on memory allocation error.
 */
int pids_list_add_pid(pids_list_t *list, const pid_t pid);

/*
 * NAME
 *   pids_list_clear
 *
 * DESCRIPTION
 *   Remove all pids from the list
 *
 * PARAMETERS
 *   `list'     Target pids_list.
 *
 * RETURN VALUE
 *   On success, return 0
 */
int pids_list_clear(pids_list_t *list);

/*
 * NAME
 *   pids_list_add_list
 *
 * DESCRIPTION
 *   Adds pids list at the end of the pids list.
 *   Allocates memory for new pid elements, it is up to user to free it.
 *
 * PARAMETERS
 *   `dst'      Target PIDs list.
 *   `src'      Source PIDs list.
 *
 * RETURN VALUE
 *   On success, returns 0.
 *   -1 on memory allocation error.
 */
int pids_list_add_list(pids_list_t *dst, pids_list_t *src);

/*
 * NAME
 *   pids_list_contains_pid
 *
 * DESCRIPTION
 *   Tests if pids list contains specific pid.
 *
 * PARAMETERS
 *   `list'     pids_list to check.
 *   `pid'      Pid to be searched for.
 *
 * RETURN VALUE
 *   If PID found in list, returns 1,
 *   Otherwise returns 0.
 */
int pids_list_contains_pid(pids_list_t *list, const pid_t pid);

/*
 * NAME
 *   pids_list_diff
 *
 * DESCRIPTION
 *   Searches for differences in two given lists
 *
 * PARAMETERS
 *   `proc'            List of pids
 *   `added'           New pids which appeared
 *   `removed'         Result array storing pids which disappeared
 * RETURN VALUE
 *   0 on success. Negative number on error.
 */
int pids_list_diff(proc_pids_t *proc, pids_list_t *added, pids_list_t *removed);

/*
 * NAME
 *   proc_pids_is_name_valid
 *
 * DESCRIPTION
 *   Checks if given string is valid process name.
 *
 * PARAMETERS
 *   `name'     null-terminated char array
 *
 * RETURN VALUE
 *   If given name is a valid process name, returns 1,
 *   Otherwise returns 0.
 */
int proc_pids_is_name_valid(const char *name);

/*
 * NAME
 *   proc_pids_init
 *
 * DESCRIPTION
 *   Helper function to properly initialize array of proc_pids.
 *   Allocates memory for proc_pids structs.
 *
 * PARAMETERS
 *   `procs_names_array'      Array of null-terminated strings with
 *                            process' names to be copied to new array
 *   `procs_names_array_size' procs_names_array element count
 *   `proc_pids'              Address of pointer, under which new
 *                            array of proc_pids will be allocated.
 *                            Must be NULL.
 * RETURN VALUE
 *   0 on success. Negative number on error:
 *   -1: allocation error
 */
int proc_pids_init(const char **procs_names_array,
                   const size_t procs_names_array_size,
                   proc_pids_t **proc_pids[]);

/*
 * NAME
 *   proc_pids_update
 *
 * DESCRIPTION
 *   Updates PIDs matching processes's names.
 *   Searches all PID directories in /proc fs and updates current pids_list.
 *
 * PARAMETERS
 *   `procfs_path'     Path to systems proc directory (e.g. /proc)
 *   `proc_pids'       Array of proc_pids pointers to be updated.
 *   `proc_pids_num'   proc_pids element count
 *
 * RETURN VALUE
 *   0 on success. -1 on error.
 */
int proc_pids_update(const char *procfs_path, proc_pids_t *proc_pids[],
                     size_t proc_pids_num);

/*
 * NAME
 *   proc_pids_free
 *
 * DESCRIPTION
 *   Releses memory allocatd for proc_pids
 *
 * PARAMETERS
 *   `proc_pids'       Array of proc_pids
 *   `proc_pids_num'   proc_pids element count
 *
 * RETURN VALUE
 *   0 on success. -1 on error.
 */
int proc_pids_free(proc_pids_t *proc_pids[], size_t proc_pids_num);
