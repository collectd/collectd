/**
 * collectd - src/utils_config_pids.h
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
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

/* Linked one-way list of pids. */
typedef struct pids_list_s {
  pid_t pid;
  struct pids_list_s *next;
} pids_list_t;

/* Holds process name and list of pids assigned to that name */
typedef struct proc_pids_s {
  proc_comm_t proccess_name;
  pids_list_t *pids;
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
 *   is_proc_name_valid
 *
 * DESCRIPTION
 *   Checks if given string as valid process name.
 *
 * PARAMETERS
 *   `name'     null-terminated char array
 *
 * RETURN VALUE
 *   If given name is a valid process name, returns 1,
 *   Otherwise returns 0.
 */
int is_proc_name_valid(const char *name);

/*
 * NAME
 *   pids_list_add_pid
 *
 * DESCRIPTION
 *   Adds pid at the end of the pids list.
 *   Allocates memory for new pid element, it is up to user to free it.
 *
 * PARAMETERS
 *   `list'     Head of target pids_list.
 *   `pid'      Pid to be added.
 *
 * RETURN VALUE
 *   On success, returns 0.
 *   -1 on memory allocation error.
 */
int pids_list_add_pid(pids_list_t **list, const pid_t pid);

/*
 * NAME
 *   pids_list_contains_pid
 *
 * DESCRIPTION
 *   Tests if pids list contains specific pid.
 *
 * PARAMETERS
 *   `list'     Head of pids_list.
 *   `pid'      Pid to be searched for.
 *
 * RETURN VALUE
 *   If PID found in list, returns 1,
 *   Otherwise returns 0.
 */
int pids_list_contains_pid(pids_list_t *list, const pid_t pid);

/*
 * NAME
 *   pids_list_add_pids_list
 *
 * DESCRIPTION
 *   Adds pids list at the end of the pids list.
 *   Allocates memory for new pid elements, it is up to user to free it.
 *   Increases dst_num by a number of added PIDs.
 *
 * PARAMETERS
 *   `dst'      Head of target PIDs list.
 *   `src'      Head of source PIDs list.
 *   `dst_num'  Variable to be increased by a number of appended PIDs.
 *
 * RETURN VALUE
 *   On success, returns 0.
 *   -1 on memory allocation error.
 */
int pids_list_add_pids_list(pids_list_t **dst, pids_list_t *src,
                            size_t *dst_num);
/*
 * NAME
 *   read_proc_name
 *
 * DESCRIPTION
 *   Reads process name from given pid directory.
 *   Strips new-line character (\n).
 *
 * PARAMETERS
 *   `procfs_path` Path to systems proc directory (e.g. /proc)
 *   `pid_entry'   Dirent for PID directory
 *   `name'        Output buffer for process name, recommended proc_comm.
 *   `out_size'    Output buffer size, recommended sizeof(proc_comm)
 *
 * RETURN VALUE
 *   On success, the number of read bytes (includes stripped \n).
 *   -1 on file open error
 */
int read_proc_name(const char *procfs_path, const struct dirent *pid_entry,
                   char *name, const size_t out_size);

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
int get_pid_number(struct dirent *entry, pid_t *pid);

/*
 * NAME
 *   pids_list_to_array
 *
 * DESCRIPTION
 *   Copies element from list to array. Assumes the space for the array is
 *   allocated.
 *
 * PARAMETERS
 *   `array'      First element of target array
 *   `list'       Head of the list
 *   `array_length' Length (element count) of the target array
 */
void pids_list_to_array(pid_t *array, pids_list_t *list,
                        const size_t array_length);

/*
 * NAME
 *   initialize_proc_pids
 *
 * DESCRIPTION
 *   Helper function to properly initialize array of proc_pids.
 *   Allocates memory for proc_pids structs.
 *
 * PARAMETERS
 *   `procs_names_array'      Array of null-terminated strings with
 *                            process' names to be copied to new array
 *   `procs_names_array_size' procs_names_array element count
 *   `proc_pids_array'        Address of pointer, under which new
 *                            array of proc_pids will be allocated.
 *                            Must be NULL.
 * RETURN VALUE
 *   0 on success. Negative number on error:
 *   -1: allocation error
 */
int initialize_proc_pids(const char **procs_names_array,
                         const size_t procs_names_array_size,
                         proc_pids_t **proc_pids_array);

/*
 * NAME
 *   fetch_pids_for_procs
 *
 * DESCRIPTION
 *   Finds PIDs matching given process's names.
 *   Searches all PID directories in /proc fs and
 *   allocates memory for proc_pids structs, it is up to user to free it.
 *   Output array will have same element count as input array.
 *
 * PARAMETERS
 *   `procfs_path'            Path to systems proc directory (e.g. /proc)
 *   `procs_names_array'      Array of null-terminated strings with
 *                            process' names to be copied to new array
 *   `procs_names_array_size' procs_names_array element count
 *   `proc_pids_array'        Address of pointer, under which new
 *                            array of proc_pids will be allocated.
 *                            Must be NULL.
 *
 * RETURN VALUE
 *   0 on success. -1 on error.
 */
int fetch_pids_for_procs(const char *procfs_path,
                         const char **procs_names_array,
                         const size_t procs_names_array_size,
                         proc_pids_t **proc_pids_array);

/*
 * NAME
 *   pids_list_diff
 *
 * DESCRIPTION
 *   Searches for differences in two given lists
 *
 * PARAMETERS
 *   `prev'            List of pids before changes
 *   `curr'            List of pids after changes
 *   `added'           Result array storing new pids which appeared in `curr'
 *   `added_num'       `added_num' array length
 *   `removed'         Result array storing pids which disappeared in `prev'
 *   `removed_num'     `removed' array length
 * RETURN VALUE
 *   0 on success. Negative number on error.
 */
int pids_list_diff(pids_list_t *prev, pids_list_t *curr, pids_list_t **added,
                   size_t *added_num, pids_list_t **removed,
                   size_t *removed_num);
