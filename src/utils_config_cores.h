/**
 * collectd - src/utils_config_cores.h
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
 *   Kamil Wiatrowski <kamilx.wiatrowski@intel.com>
 **/

#ifndef UTILS_CONFIG_CORES_H
#define UTILS_CONFIG_CORES_H 1

#include "configfile.h"

#ifndef PRIsz
#define PRIsz "zu"
#endif /* PRIsz */

struct core_group_s {
  char *desc;
  unsigned int *cores;
  size_t num_cores;
};
typedef struct core_group_s core_group_t;

struct core_groups_list_s {
  core_group_t *cgroups;
  size_t num_cgroups;
};
typedef struct core_groups_list_s core_groups_list_t;

/*
 * NAME
 *   config_cores_parse
 *
 * DESCRIPTION
 *   Convert strings from config item into list of core groups.
 *
 * PARAMETERS
 *   `ci'      Pointer to config item.
 *   `cgl'     Pointer to core groups list to be filled.
 *
 * RETURN VALUE
 *    Zero upon success or non-zero if an error occurred.
 *
 * NOTES
 *    In case of an error, *cgl is not modified.
 *    Numbers can be in decimal or hexadecimal format.
 *    The memory allocated for *cgroups in list needs to be freed
 *    with config_cores_cleanup.
 *
 * EXAMPLES
 *    If config is "0-3" "[4-15]" it means that cores 0-3 are aggregated
 *    into one group and cores 4 to 15 are stored individualily in
 *    separate groups. Examples of allowed formats:
 *    "0,3,4" "10-15" - cores collected into two groups
 *    "0" "0x3" "7" - 3 cores, each in individual group
 *    "[32-63]" - 32 cores, each in individual group
 *
 *    For empty string "" *cgl is not modified and zero is returned.
 */
int config_cores_parse(const oconfig_item_t *ci, core_groups_list_t *cgl);

/*
 * NAME
 *   config_cores_default
 *
 * DESCRIPTION
 *   Set number of cores starting from zero into individual
 *   core groups in *cgl list.
 *
 * PARAMETERS
 *   `num_cores'  Number of cores to be configured.
 *   `cgl'        Pointer to core groups list.
 *
 * RETURN VALUE
 *    Zero upon success or non-zero if an error occurred.
 *
 * NOTES
 *    The memory allocated for *cgroups in list needs to be freed
 *    with config_cores_cleanup. In case of error the memory is
 *    freed by the function itself.
 */
int config_cores_default(int num_cores, core_groups_list_t *cgl);

/*
 * NAME
 *   config_cores_cleanup
 *
 * DESCRIPTION
 *   Free the memory allocated for cgroups and set
 *   num_cgroups to zero.
 *
 * PARAMETERS
 *   `cgl'     Pointer to core groups list.
 */
void config_cores_cleanup(core_groups_list_t *cgl);

/*
 * NAME
 *   config_cores_cmp_cgroups
 *
 * DESCRIPTION
 *   Function to compare cores in 2 core groups.
 *
 * PARAMETERS
 *   `cg_a'      Pointer to core group a.
 *   `cg_b'      Pointer to core group b.
 *
 * RETURN VALUE
 *    1 if both groups contain the same cores
 *    0 if none of their cores match
 *    -1 if some but not all cores match
 */
int config_cores_cmp_cgroups(const core_group_t *cg_a,
                             const core_group_t *cg_b);

#endif /* UTILS_CONFIG_CORES_H */
