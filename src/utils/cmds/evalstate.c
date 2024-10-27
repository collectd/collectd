#include "collectd.h"

#include "daemon/utils_cache.h"
#include "plugin.h"
#include "utils/cmds/evalstate.h"
#include "utils/common/common.h"

cmd_status_t cmd_parse_evalstate(size_t argc, char **argv,
                                 cmd_evalstate_t *ret_evalstate,
                                 const cmd_options_t *opts,
                                 cmd_error_handler_t *err) {

  if ((ret_evalstate == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Invalid arguments to cmd_parse_evalstate.");
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

  int status = parse_identifier(argv[0], &ret_evalstate->identifier.host,
                                &ret_evalstate->identifier.plugin,
                                &ret_evalstate->identifier.plugin_instance,
                                &ret_evalstate->identifier.type,
                                &ret_evalstate->identifier.type_instance,
                                opts->identifier_default_host);
  if (status != 0) {
    DEBUG("cmd_parse_evalstate: Cannot parse identifier `%s'.",
          identifier_copy);
    cmd_error(CMD_PARSE_ERROR, err, "Cannot parse identifier `%s'.",
              identifier_copy);
    sfree(identifier_copy);
    return CMD_PARSE_ERROR;
  }

  ret_evalstate->raw_identifier = identifier_copy;

  return CMD_OK;
} /* cmd_status_t cmd_parse_evalstate */

cmd_status_t cmd_handle_evalstate(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};
  cmd_status_t status;
  cmd_t cmd;

  const data_set_t *ds;

  if ((fh == NULL) || (buffer == NULL))
    return -1;

  DEBUG("utils_cmd_evalstate: cmd_handle_evalstate (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return status;
  if (cmd.type != CMD_EVALSTATE) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return CMD_UNKNOWN_COMMAND;
  }

  ds = plugin_get_ds(cmd.cmd.evalstate.identifier.type);
  if (ds == NULL) {
    DEBUG("cmd_handle_evalstate: plugin_get_ds (%s) == NULL;",
          cmd.cmd.evalstate.identifier.type);
    cmd_error(CMD_ERROR, &err, "Type `%s' is unknown.\n",
              cmd.cmd.evalstate.identifier.type);
    cmd_destroy(&cmd);
    return -1;
  }
  value_list_t vl = VALUE_LIST_INIT;
  sstrncpy(vl.host, cmd.cmd.evalstate.identifier.host, sizeof(vl.host));
  sstrncpy(vl.plugin, cmd.cmd.evalstate.identifier.plugin, sizeof(vl.plugin));
  if (cmd.cmd.evalstate.identifier.plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, cmd.cmd.evalstate.identifier.plugin_instance,
             sizeof(vl.plugin_instance));
  sstrncpy(vl.type, cmd.cmd.evalstate.identifier.type, sizeof(vl.type));
  if (cmd.cmd.evalstate.identifier.type_instance != NULL)
    sstrncpy(vl.type_instance, cmd.cmd.evalstate.identifier.type_instance,
             sizeof(vl.type_instance));

  if (plugin_write("threshold", ds, &vl) < 0) {
    DEBUG("cmd_handle_evalstate: plugin_get_ds (%s) == NULL;",
          cmd.cmd.getval.identifier.type);
    cmd_error(CMD_ERROR, &err, "Type `%s' is unknown.\n",
              cmd.cmd.getval.identifier.type);
    cmd_destroy(&cmd);
    return CMD_ERROR;
  }

  cmd_error(CMD_OK, &err, "Done");
  cmd_destroy(&cmd);
  return CMD_OK;
} /* cmd_status_t cmd_handle_evalstate */

void cmd_destroy_evalstate(cmd_evalstate_t *evalstate) {
  if (evalstate == NULL)
    return;

  sfree(evalstate->raw_identifier);
} /* void cmd_destroy_evalstate */
