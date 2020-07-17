/**
 * collectd - src/utils_cmd_getthreshold.c
 * Copyright (C) 2008,2009  Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include "utils/avltree/avltree.h"
#include "utils/cmds/getthreshold.h"
#include "utils/cmds/parse_option.h" /* for `parse_string' */
#include "utils_threshold.h"

#define print_to_socket(fh, ...)                                               \
  if (fprintf(fh, __VA_ARGS__) < 0) {                                          \
    WARNING("handle_getthreshold: failed to write to socket #%i: %s",          \
            fileno(fh), STRERRNO);                                             \
    return -1;                                                                 \
  }

int handle_getthreshold(FILE *fh, char *buffer) {
  if ((fh == NULL) || (buffer == NULL))
    return -1;

  DEBUG("utils_cmd_getthreshold: handle_getthreshold (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  char *command = NULL;
  int status = parse_string(&buffer, &command);
  if (status != 0) {
    print_to_socket(fh, "-1 Cannot parse command.\n");
    return -1;
  }
  assert(command != NULL);

  if (strcasecmp("GETTHRESHOLD", command) != 0) {
    print_to_socket(fh, "-1 Unexpected command: `%s'.\n", command);
    return -1;
  }

  char *identifier = NULL;
  status = parse_string(&buffer, &identifier);
  if (status != 0) {
    print_to_socket(fh, "-1 Cannot parse identifier.\n");
    return -1;
  }
  assert(identifier != NULL);

  if (*buffer != 0) {
    print_to_socket(fh, "-1 Garbage after end of command: %s\n", buffer);
    return -1;
  }

  /* parse_identifier() modifies its first argument,
   * returning pointers into it */
  char *identifier_copy = sstrdup(identifier);

  char *host = NULL;
  char *plugin = NULL;
  char *type = NULL;
  char *data_source = NULL;
  status =
      parse_identifier(identifier_copy, &host, &plugin, &type, &data_source,
                       /* default_host = */ NULL);
  if (status != 0) {
    DEBUG("handle_getthreshold: Cannot parse identifier `%s'.", identifier);
    print_to_socket(fh, "-1 Cannot parse identifier `%s'.\n", identifier);
    sfree(identifier_copy);
    return -1;
  }

  char *plugin_instance = strchr(plugin, '-');
  if (plugin_instance != NULL) {
    *plugin_instance = 0;
    plugin_instance++;
  }
  char *type_instance = strchr(type, '-');
  if (type_instance != NULL) {
    *type_instance = 0;
    type_instance++;
  }

  metric_single_t m = {
      .identity = identity_create_legacy(plugin, type, data_source, host),
  };
  if (plugin_instance != NULL) {
    identity_add_label(m.identity, "plugin_instance", plugin_instance);
  }
  if (type_instance != NULL) {
    identity_add_label(m.identity, "type_instance", type_instance);
  }

  sfree(identifier_copy);

  threshold_t threshold;
  status = ut_search_threshold(&m, &threshold);
  if (status == ENOENT) {
    print_to_socket(fh, "-1 No threshold found for identifier %s\n",
                    identifier);
    return 0;
  } else if (status != 0) {
    print_to_socket(fh, "-1 Error while looking up threshold: %i\n", status);
    return -1;
  }

  /* Lets count the number of lines we'll return. */
  int lines_num = 0;
  if (threshold.host[0] != 0)
    lines_num++;
  if (threshold.plugin[0] != 0)
    lines_num++;
  if (threshold.type[0] != 0)
    lines_num++;
  if (threshold.data_source[0] != 0)
    lines_num++;
  if (!isnan(threshold.warning_min))
    lines_num++;
  if (!isnan(threshold.warning_max))
    lines_num++;
  if (!isnan(threshold.failure_min))
    lines_num++;
  if (!isnan(threshold.failure_max))
    lines_num++;
  if (threshold.hysteresis > 0.0)
    lines_num++;
  if (threshold.hits > 1)
    lines_num++;

  /* Print the response */
  print_to_socket(fh, "%d Threshold found\n", lines_num);

  if (threshold.host[0] != 0)
    print_to_socket(fh, "Host: %s\n", threshold.host);
  if (threshold.plugin[0] != 0)
    print_to_socket(fh, "Plugin: %s\n", threshold.plugin);
  if (threshold.type[0] != 0)
    print_to_socket(fh, "Type: %s\n", threshold.type);
  if (threshold.data_source[0] != 0)
    print_to_socket(fh, "Data Source: %s\n", threshold.data_source);
  if (!isnan(threshold.warning_min))
    print_to_socket(fh, "Warning Min: %g\n", threshold.warning_min);
  if (!isnan(threshold.warning_max))
    print_to_socket(fh, "Warning Max: %g\n", threshold.warning_max);
  if (!isnan(threshold.failure_min))
    print_to_socket(fh, "Failure Min: %g\n", threshold.failure_min);
  if (!isnan(threshold.failure_max))
    print_to_socket(fh, "Failure Max: %g\n", threshold.failure_max);
  if (threshold.hysteresis > 0.0)
    print_to_socket(fh, "Hysteresis: %g\n", threshold.hysteresis);
  if (threshold.hits > 1)
    print_to_socket(fh, "Hits: %i\n", threshold.hits);

  return 0;
} /* int handle_getthreshold */
