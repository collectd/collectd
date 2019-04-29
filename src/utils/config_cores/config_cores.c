/**
 * collectd - src/utils_config_cores.c
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

#include "collectd.h"

#include "utils/common/common.h"

#include "utils/config_cores/config_cores.h"

#define UTIL_NAME "utils_config_cores"

#define MAX_SOCKETS 8
#define MAX_SOCKET_CORES 64
#define MAX_CORES (MAX_SOCKET_CORES * MAX_SOCKETS)

static inline _Bool is_in_list(unsigned val, const unsigned *list, size_t len) {
  for (size_t i = 0; i < len; i++)
    if (list[i] == val)
      return 1;
  return 0;
}

static int str_to_uint(const char *s, unsigned *n) {
  if (s == NULL || n == NULL)
    return -EINVAL;
  char *endptr = NULL;

  *n = (unsigned)strtoul(s, &endptr, 0);
  if (*s == '\0' || *endptr != '\0') {
    ERROR(UTIL_NAME ": Failed to parse '%s' into unsigned number", s);
    return -EINVAL;
  }

  return 0;
}

/*
 * NAME
 *   str_list_to_nums
 *
 * DESCRIPTION
 *   Converts string of characters representing list of numbers into array of
 *   numbers. Allowed formats are:
 *     0,1,2,3
 *     0-10,20-18
 *     1,3,5-8,10,0x10-12
 *
 *   Numbers can be in decimal or hexadecimal format.
 *
 * PARAMETERS
 *   `s'         String representing list of unsigned numbers.
 *   `nums'      Array to put converted numeric values into.
 *   `nums_len'  Maximum number of elements that nums can accommodate.
 *
 * RETURN VALUE
 *    Number of elements placed into nums.
 */
static size_t str_list_to_nums(char *s, unsigned *nums, size_t nums_len) {
  char *saveptr = NULL;
  char *token;
  size_t idx = 0;

  while ((token = strtok_r(s, ",", &saveptr))) {
    char *pos;
    unsigned start, end = 0;
    s = NULL;

    while (isspace(*token))
      token++;
    if (*token == '\0')
      continue;

    pos = strchr(token, '-');
    if (pos) {
      *pos = '\0';
    }

    if (str_to_uint(token, &start))
      return 0;

    if (pos) {
      if (str_to_uint(pos + 1, &end))
        return 0;
    } else {
      end = start;
    }

    if (start > end) {
      unsigned swap = start;
      start = end;
      end = swap;
    }

    for (unsigned i = start; i <= end; i++) {
      if (is_in_list(i, nums, idx))
        continue;
      if (idx >= nums_len) {
        WARNING(UTIL_NAME ": exceeded the cores number limit: %" PRIsz,
                nums_len);
        return idx;
      }
      nums[idx] = i;
      idx++;
    }
  }
  return idx;
}

/*
 * NAME
 *   check_core_grouping
 *
 * DESCRIPTION
 *   Look for [...] brackets in *in string and if found copy the
 *   part between brackets into *out string and set grouped to 0.
 *   Otherwise grouped is set to 1 and input is copied without leading
 *   whitespaces.
 *
 * PARAMETERS
 *   `out'       Output string to store result.
 *   `in'        Input string to be parsed and copied.
 *   `out_size'  Maximum number of elements that out can accommodate.
 *   `grouped'   Set by function depending if cores should be grouped or not.
 *
 * RETURN VALUE
 *    Zero upon success or non-zero if an error occurred.
 */
static int check_core_grouping(char *out, const char *in, size_t out_size,
                               _Bool *grouped) {
  const char *start = in;
  char *end;
  while (isspace(*start))
    ++start;
  if (start[0] == '[') {
    *grouped = 0;
    ++start;
    end = strchr(start, ']');
    if (end == NULL) {
      ERROR(UTIL_NAME ": Missing closing bracket ] in option %s.", in);
      return -EINVAL;
    }
    if ((size_t)(end - start) >= out_size) {
      ERROR(UTIL_NAME ": Out buffer is too small.");
      return -EINVAL;
    }
    sstrncpy(out, start, end - start + 1);
    DEBUG(UTIL_NAME ": Mask for individual (not aggregated) cores: %s", out);
  } else {
    *grouped = 1;
    sstrncpy(out, start, out_size);
  }
  return 0;
}

int config_cores_parse(const oconfig_item_t *ci, core_groups_list_t *cgl) {
  if (ci == NULL || cgl == NULL)
    return -EINVAL;
  if (ci->values_num == 0 || ci->values_num > MAX_CORES)
    return -EINVAL;
  core_group_t cgroups[MAX_CORES] = {{0}};
  size_t cg_idx = 0; /* index for cgroups array */
  int ret = 0;

  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING(UTIL_NAME ": The %s option requires string arguments.", ci->key);
      return -EINVAL;
    }
  }

  if (ci->values_num == 1 && ci->values[0].value.string &&
      strlen(ci->values[0].value.string) == 0)
    return 0;

  for (int i = 0; i < ci->values_num; i++) {
    size_t n;
    _Bool grouped = 1;
    char str[DATA_MAX_NAME_LEN];
    unsigned cores[MAX_CORES] = {0};

    if (cg_idx >= STATIC_ARRAY_SIZE(cgroups)) {
      ERROR(UTIL_NAME
            ": Configuration exceeds maximum number of cores: %" PRIsz,
            STATIC_ARRAY_SIZE(cgroups));
      ret = -EINVAL;
      goto parse_error;
    }
    if ((ci->values[i].value.string == NULL) ||
        (strlen(ci->values[i].value.string) == 0)) {
      ERROR(UTIL_NAME ": Failed to parse parameters for %s option.", ci->key);
      ret = -EINVAL;
      goto parse_error;
    }

    ret = check_core_grouping(str, ci->values[i].value.string, sizeof(str),
                              &grouped);
    if (ret != 0) {
      ERROR(UTIL_NAME ": Failed to parse config option [%d] %s.", i,
            ci->values[i].value.string);
      goto parse_error;
    }
    n = str_list_to_nums(str, cores, STATIC_ARRAY_SIZE(cores));
    if (n == 0) {
      ERROR(UTIL_NAME ": Failed to parse config option [%d] %s.", i,
            ci->values[i].value.string);
      ret = -EINVAL;
      goto parse_error;
    }

    if (grouped) {
      cgroups[cg_idx].desc = strdup(ci->values[i].value.string);
      if (cgroups[cg_idx].desc == NULL) {
        ERROR(UTIL_NAME ": Failed to allocate description.");
        ret = -ENOMEM;
        goto parse_error;
      }

      cgroups[cg_idx].cores = calloc(n, sizeof(*cgroups[cg_idx].cores));
      if (cgroups[cg_idx].cores == NULL) {
        ERROR(UTIL_NAME ": Failed to allocate cores for cgroup.");
        ret = -ENOMEM;
        goto parse_error;
      }

      for (size_t j = 0; j < n; j++)
        cgroups[cg_idx].cores[j] = cores[j];

      cgroups[cg_idx].num_cores = n;
      cg_idx++;
    } else {
      for (size_t j = 0; j < n && cg_idx < STATIC_ARRAY_SIZE(cgroups); j++) {
        char desc[DATA_MAX_NAME_LEN];
        snprintf(desc, sizeof(desc), "%u", cores[j]);

        cgroups[cg_idx].desc = strdup(desc);
        if (cgroups[cg_idx].desc == NULL) {
          ERROR(UTIL_NAME ": Failed to allocate desc for core %u.", cores[j]);
          ret = -ENOMEM;
          goto parse_error;
        }

        cgroups[cg_idx].cores = calloc(1, sizeof(*(cgroups[cg_idx].cores)));
        if (cgroups[cg_idx].cores == NULL) {
          ERROR(UTIL_NAME ": Failed to allocate cgroup for core %u.", cores[j]);
          ret = -ENOMEM;
          goto parse_error;
        }
        cgroups[cg_idx].num_cores = 1;
        cgroups[cg_idx].cores[0] = cores[j];
        cg_idx++;
      }
    }
  }

  cgl->cgroups = calloc(cg_idx, sizeof(*cgl->cgroups));
  if (cgl->cgroups == NULL) {
    ERROR(UTIL_NAME ": Failed to allocate core groups.");
    ret = -ENOMEM;
    goto parse_error;
  }

  cgl->num_cgroups = cg_idx;
  for (size_t i = 0; i < cg_idx; i++)
    cgl->cgroups[i] = cgroups[i];

  return 0;

parse_error:

  cg_idx = 0;
  while (cg_idx < STATIC_ARRAY_SIZE(cgroups) && cgroups[cg_idx].desc != NULL) {
    sfree(cgroups[cg_idx].desc);
    sfree(cgroups[cg_idx].cores);
    cg_idx++;
  }
  return ret;
}

int config_cores_default(int num_cores, core_groups_list_t *cgl) {
  if (cgl == NULL || num_cores < 0 || num_cores > MAX_CORES)
    return -EINVAL;

  cgl->cgroups = calloc(num_cores, sizeof(*(cgl->cgroups)));
  if (cgl->cgroups == NULL) {
    ERROR(UTIL_NAME ": Failed to allocate memory for core groups.");
    return -ENOMEM;
  }
  cgl->num_cgroups = num_cores;

  for (int i = 0; i < num_cores; i++) {
    char desc[DATA_MAX_NAME_LEN];
    snprintf(desc, sizeof(desc), "%d", i);

    cgl->cgroups[i].cores = calloc(1, sizeof(*(cgl->cgroups[i].cores)));
    if (cgl->cgroups[i].cores == NULL) {
      ERROR(UTIL_NAME ": Failed to allocate default cores for cgroup %d.", i);
      config_cores_cleanup(cgl);
      return -ENOMEM;
    }
    cgl->cgroups[i].num_cores = 1;
    cgl->cgroups[i].cores[0] = i;

    cgl->cgroups[i].desc = strdup(desc);
    if (cgl->cgroups[i].desc == NULL) {
      ERROR(UTIL_NAME ": Failed to allocate description for cgroup %d.", i);
      config_cores_cleanup(cgl);
      return -ENOMEM;
    }
  }
  return 0;
}

void config_cores_cleanup(core_groups_list_t *cgl) {
  if (cgl == NULL)
    return;
  for (size_t i = 0; i < cgl->num_cgroups; i++) {
    sfree(cgl->cgroups[i].desc);
    sfree(cgl->cgroups[i].cores);
  }
  sfree(cgl->cgroups);
  cgl->num_cgroups = 0;
}

int config_cores_cmp_cgroups(const core_group_t *cg_a,
                             const core_group_t *cg_b) {
  size_t found = 0;

  assert(cg_a != NULL);
  assert(cg_b != NULL);

  const size_t sz_a = cg_a->num_cores;
  const size_t sz_b = cg_b->num_cores;
  const unsigned *tab_a = cg_a->cores;
  const unsigned *tab_b = cg_b->cores;

  for (size_t i = 0; i < sz_a; i++)
    if (is_in_list(tab_a[i], tab_b, sz_b))
      found++;

  /* if no cores are the same */
  if (!found)
    return 0;
  /* if group contains same cores */
  if (sz_a == sz_b && sz_b == found)
    return 1;
  /* if not all cores are the same */
  return -1;
}
