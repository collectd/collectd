/**
 * collectd - src/utils_cmd_putval.c
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2016       Sebastian tokkee Harl
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
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#include "collectd.h"

#include "utils/cmds/putval.h"
#include "utils/common/common.h"

/*
 * private helper functions
 */

static int set_option(value_list_t *vl, char const *key, char const *value,
                      cmd_error_handler_t *errhndl) {
  if ((vl == NULL) || (key == NULL) || (value == NULL)) {
    return EINVAL;
  }

  if (strcasecmp("interval", key) == 0) {
    double tmp;
    char *endptr;

    endptr = NULL;
    errno = 0;
    tmp = strtod(value, &endptr);

    if ((errno == 0) && (endptr != NULL) && (endptr != value) && (tmp > 0.0))
      vl->interval = DOUBLE_TO_CDTIME_T(tmp);
  } else if (strncasecmp("meta:", key, 5) == 0) {
    char const *meta_key = key + 5;

    if (vl->meta == NULL) {
      vl->meta = meta_data_create();
      if (vl->meta == NULL) {
        return CMD_ERROR;
      }
    }

    int status = meta_data_add_string(vl->meta, meta_key, value);
    return (status == 0) ? CMD_OK : CMD_ERROR;
  } else {
    return CMD_ERROR;
  }
  return CMD_OK;
} /* int set_option */

/*
 * public API
 */

cmd_status_t cmd_parse_putval(size_t argc, char **argv,
                              cmd_putval_t *ret_putval,
                              const cmd_options_t *opts,
                              cmd_error_handler_t *errhndl) {
  if ((ret_putval == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, errhndl, "Invalid arguments to cmd_parse_putval.");
    return CMD_ERROR;
  }

  if (argc < 2) {
    cmd_error(CMD_PARSE_ERROR, errhndl,
              "Missing identifier and/or value-list.");
    return CMD_PARSE_ERROR;
  }

  value_list_t vl = VALUE_LIST_INIT;
  if ((opts != NULL) && (opts->identifier_default_host != NULL)) {
	  sstrncpy(vl.host, opts->identifier_default_host, sizeof(vl.host));
  }

  char const *identifier = argv[0];
  int status = parse_identifier_vl(identifier, &vl);
  if (status != 0) {
    DEBUG("cmd_handle_putval: Cannot parse identifier `%s'.", identifier);
    cmd_error(CMD_PARSE_ERROR, errhndl, "parse_identifier_vl(\"%s\"): %s", identifier, STRERROR(status));
    return CMD_PARSE_ERROR;
  }

  data_set_t const *ds = plugin_get_ds(vl.type);
  if (ds == NULL) {
    cmd_error(CMD_PARSE_ERROR, errhndl, "1 Type `%s' isn't defined.", vl.type);
    return CMD_PARSE_ERROR;
  }

  ret_putval->raw_identifier = strdup(identifier);
  if (ret_putval->raw_identifier == NULL) {
    cmd_error(CMD_ERROR, errhndl, "malloc failed.");
    cmd_destroy_putval(ret_putval);
    sfree(vl.values);
    return CMD_ERROR;
  }

  /* All the remaining fields are part of the option list. */
  cmd_status_t result = CMD_OK;
  for (size_t i = 1; i < argc; ++i) {
    value_list_t *tmp;

    char *key = NULL;
    char *value = NULL;

    status = cmd_parse_option(argv[i], &key, &value, errhndl);
    if (status == CMD_OK) {
      int option_err;

      assert(key != NULL);
      assert(value != NULL);
      option_err = set_option(&vl, key, value, errhndl);
      if (option_err != CMD_OK && option_err != CMD_NO_OPTION) {
        result = option_err;
        break;
      }
      continue;
    } else if (status != CMD_NO_OPTION) {
      /* parse_option failed, buffer has been modified.
       * => we need to abort */
      result = status;
      break;
    }
    /* else: cmd_parse_option did not find an option; treat this as a
     * value list. */

    vl.values_len = ds->ds_num;
    vl.values = calloc(vl.values_len, sizeof(*vl.values));
    if (vl.values == NULL) {
      cmd_error(CMD_ERROR, errhndl, "malloc failed.");
      result = CMD_ERROR;
      break;
    }

    status = parse_values(argv[i], &vl, ds);
    if (status != 0) {
      cmd_error(CMD_PARSE_ERROR, errhndl, "Parsing the values string failed.");
      result = CMD_PARSE_ERROR;
      vl.values_len = 0;
      sfree(vl.values);
      break;
    }

    tmp = realloc(ret_putval->vl,
                  (ret_putval->vl_num + 1) * sizeof(*ret_putval->vl));
    if (tmp == NULL) {
      cmd_error(CMD_ERROR, errhndl, "realloc failed.");
      cmd_destroy_putval(ret_putval);
      result = CMD_ERROR;
      vl.values_len = 0;
      sfree(vl.values);
      break;
    }

    ret_putval->vl = tmp;
    ret_putval->vl_num++;
    memcpy(&ret_putval->vl[ret_putval->vl_num - 1], &vl, sizeof(vl));

    /* pointer is now owned by ret_putval->vl[] */
    vl.values_len = 0;
    vl.values = NULL;
  } /* while (*buffer != 0) */
  /* Done parsing the options. */

  if (result != CMD_OK) {
    cmd_destroy_putval(ret_putval);
  }

  return result;
} /* cmd_status_t cmd_parse_putval */

void cmd_destroy_putval(cmd_putval_t *putval) {
  if (putval == NULL)
    return;

  sfree(putval->raw_identifier);

  for (size_t i = 0; i < putval->vl_num; ++i) {
    sfree(putval->vl[i].values);
    meta_data_destroy(putval->vl[i].meta);
    putval->vl[i].meta = NULL;
  }
  sfree(putval->vl);
  putval->vl = NULL;
  putval->vl_num = 0;
} /* void cmd_destroy_putval */

cmd_status_t cmd_handle_putval(FILE *fh, char *buffer) {
  cmd_error_handler_t errhndl = {cmd_error_fh, fh};
  cmd_t cmd;

  int status;

  DEBUG("utils_cmd_putval: cmd_handle_putval (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &errhndl)) != CMD_OK)
    return status;
  if (cmd.type != CMD_PUTVAL) {
    cmd_error(CMD_UNKNOWN_COMMAND, &errhndl, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return CMD_UNKNOWN_COMMAND;
  }

  for (size_t i = 0; i < cmd.cmd.putval.vl_num; ++i)
    plugin_dispatch_values(&cmd.cmd.putval.vl[i]);

  if (fh != stdout)
    cmd_error(CMD_OK, &errhndl, "Success: %i %s been dispatched.",
              (int)cmd.cmd.putval.vl_num,
              (cmd.cmd.putval.vl_num == 1) ? "value has" : "values have");

  cmd_destroy(&cmd);
  return CMD_OK;
} /* int cmd_handle_putval */
