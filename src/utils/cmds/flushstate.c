#include "collectd.h"

#include "daemon/utils_cache.h"
#include "plugin.h"
#include "utils/cmds/flushstate.h"
#include "utils/common/common.h"

cmd_status_t cmd_parse_flushstate(size_t argc, char **argv,
                                  cmd_flushstate_t *ret_flushstate,
                                  const cmd_options_t *opts,
                                  cmd_error_handler_t *err) {

  if ((ret_flushstate == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Invalid arguments to cmd_parse_flushstate.");
    return CMD_ERROR;
  }

  if (argc != 1) {
    if (argc == 0)
      cmd_error(CMD_PARSE_ERROR, err, "Missing identifier.");
    else
      cmd_error(CMD_PARSE_ERROR, err, "Garbage after identifier: `%s'.",
                argv[1]);
    return CMD_PARSE_ERROR;
  }

  char *identifier_copy = sstrdup(argv[0]);

  int status = parse_identifier(argv[0], &ret_flushstate->identifier.host,
                                &ret_flushstate->identifier.plugin,
                                &ret_flushstate->identifier.plugin_instance,
                                &ret_flushstate->identifier.type,
                                &ret_flushstate->identifier.type_instance,
                                opts->identifier_default_host);
  if (status != 0) {
    DEBUG("cmd_parse_flushstate: Cannot parse identifier `%s'.",
          identifier_copy);
    cmd_error(CMD_PARSE_ERROR, err, "Cannot parse identifier `%s'.",
              identifier_copy);
    sfree(identifier_copy);
    return CMD_PARSE_ERROR;
  }

  ret_flushstate->raw_identifier = identifier_copy;
  return CMD_OK;
} /* cmd_status_t cmd_parse_flushstate */

cmd_status_t cmd_handle_flushstate(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};
  cmd_status_t status;
  cmd_t cmd;

  const data_set_t *ds;

  if ((fh == NULL) || (buffer == NULL))
    return -1;

  DEBUG("utils_cmd_flushstate: cmd_handle_flushstate (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return status;
  if (cmd.type != CMD_FLUSHSTATE) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return CMD_UNKNOWN_COMMAND;
  }

  ds = plugin_get_ds(cmd.cmd.flushstate.identifier.type);
  if (ds == NULL) {
    DEBUG("cmd_handle_flushstate: plugin_get_ds (%s) == NULL;",
          cmd.cmd.flushstate.identifier.type);
    cmd_error(CMD_ERROR, &err, "Type `%s' is unknown.\n",
              cmd.cmd.flushstate.identifier.type);
    cmd_destroy(&cmd);
    return -1;
  }

  value_list_t vl = VALUE_LIST_INIT;
  sstrncpy(vl.host, cmd.cmd.flushstate.identifier.host, sizeof(vl.host));
  sstrncpy(vl.plugin, cmd.cmd.flushstate.identifier.plugin, sizeof(vl.plugin));
  if (cmd.cmd.flushstate.identifier.plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, cmd.cmd.flushstate.identifier.plugin_instance,
             sizeof(vl.plugin_instance));
  sstrncpy(vl.type, cmd.cmd.flushstate.identifier.type, sizeof(vl.type));
  if (cmd.cmd.flushstate.identifier.type_instance != NULL)
    sstrncpy(vl.type_instance, cmd.cmd.flushstate.identifier.type_instance,
             sizeof(vl.type_instance));

  if (uc_set_state(ds, &vl, STATE_UNKNOWN) < 0) {
    DEBUG("cmd_handle_getval: plugin_get_ds (%s) == NULL;",
          cmd.cmd.getval.identifier.type);
    cmd_error(CMD_ERROR, &err, "Type `%s' is unknown.\n",
              cmd.cmd.getval.identifier.type);
    cmd_destroy(&cmd);
    return CMD_ERROR;
  }

  cmd_error(CMD_OK, &err, "Done");
  cmd_destroy(&cmd);
  return CMD_OK;
} /* cmd_status_t cmd_handle_flushstate */

void cmd_destroy_flushstate(cmd_flushstate_t *flushstate) {
  if (flushstate == NULL)
    return;

  sfree(flushstate->raw_identifier);
} /* void cmd_destroy_flushstate */
