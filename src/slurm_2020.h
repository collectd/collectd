enum slurm_node_states {
  MAINT_NONRESP,
  MAINT,
  REBOOT_NONRESP,
  REBOOT,
  DRAINING_MAINT,
  DRAINING_REBOOT,
  DRAINING_POWERUP,
  DRAINING_POWERDOWN,
  DRAINING_NONRESP,
  DRAINING,
  DRAINED_MAINT,
  DRAINED_REBOOT,
  DRAINED_POWERUP,
  DRAINED_POWERDOWN,
  DRAINED_NONRESP,
  DRAINED,
  FAILING_NONRESP,
  FAILING,
  FAIL_NONRESP,
  FAIL,
  CANCEL_REBOOT,
  POWER_DOWN,
  POWER_UP,
  DOWN_MAINT,
  DOWN_REBOOT,
  DOWN_POWERUP,
  DOWN_POWERDOWN,
  DOWN_NONRESP,
  DOWN,
  ALLOCATED_MAINT,
  ALLOCATED_REBOOT,
  ALLOCATED_POWERUP,
  ALLOCATED_POWERDOWN,
  ALLOCATED_NONRESP,
  ALLOCATED_COMP,
  ALLOCATED,
  COMPLETING_MAINT,
  COMPLETING_REBOOT,
  COMPLETING_POWERUP,
  COMPLETING_POWERDOWN,
  COMPLETING_NONRESP,
  COMPLETING,
  IDLE_MAINT,
  IDLE_REBOOT,
  IDLE_POWERUP,
  IDLE_POWERDOWN,
  IDLE_NONRESP,
  PERFCTRS,
  RESERVED,
  IDLE,
  MIXED_MAINT,
  MIXED_REBOOT,
  MIXED_POWERUP,
  MIXED_POWERDOWN,
  MIXED_NONRESP,
  MIXED,
  FUTURE_MAINT,
  FUTURE_REBOOT,
  FUTURE_POWERUP,
  FUTURE_POWERDOWN,
  FUTURE_NONRESP,
  FUTURE,
  RESUME,
  UNKNOWN_NONRESP,
  UNKNOWN,
  UNKNOWN2
};

char *node_state_names[] = {"MAINT_NONRESP",
                            "MAINT",
                            "REBOOT_NONRESP",
                            "REBOOT",
                            "DRAINING_MAINT",
                            "DRAINING_REBOOT",
                            "DRAINING_POWERUP",
                            "DRAINING_POWERDOWN",
                            "DRAINING_NONRESP",
                            "DRAINING",
                            "DRAINED_MAINT",
                            "DRAINED_REBOOT",
                            "DRAINED_POWERUP",
                            "DRAINED_POWERDOWN",
                            "DRAINED_NONRESP",
                            "DRAINED",
                            "FAILING_NONRESP",
                            "FAILING",
                            "FAIL_NONRESP",
                            "FAIL",
                            "CANCEL_REBOOT",
                            "POWER_DOWN",
                            "POWER_UP",
                            "DOWN_MAINT",
                            "DOWN_REBOOT",
                            "DOWN_POWERUP",
                            "DOWN_POWERDOWN",
                            "DOWN_NONRESP",
                            "DOWN",
                            "ALLOCATED_MAINT",
                            "ALLOCATED_REBOOT",
                            "ALLOCATED_POWERUP",
                            "ALLOCATED_POWERDOWN",
                            "ALLOCATED_NONRESP",
                            "ALLOCATED_COMP",
                            "ALLOCATED",
                            "COMPLETING_MAINT",
                            "COMPLETING_REBOOT",
                            "COMPLETING_POWERUP",
                            "COMPLETING_POWERDOWN",
                            "COMPLETING_NONRESP",
                            "COMPLETING",
                            "IDLE_MAINT",
                            "IDLE_REBOOT",
                            "IDLE_POWERUP",
                            "IDLE_POWERDOWN",
                            "IDLE_NONRESP",
                            "PERFCTRS",
                            "RESERVED",
                            "IDLE",
                            "MIXED_MAINT",
                            "MIXED_REBOOT",
                            "MIXED_POWERUP",
                            "MIXED_POWERDOWN",
                            "MIXED_NONRESP",
                            "MIXED",
                            "FUTURE_MAINT",
                            "FUTURE_REBOOT",
                            "FUTURE_POWERUP",
                            "FUTURE_POWERDOWN",
                            "FUTURE_NONRESP",
                            "FUTURE",
                            "RESUME",
                            "UNKNOWN_NONRESP",
                            "UNKNOWN",
                            "?"};

/* based on src/common/slurm_protocol_defs.c node_state_string function */
uint8_t slurm_node_state(uint32_t inx) {
  int base = (inx & NODE_STATE_BASE);
  bool comp_flag = (inx & NODE_STATE_COMPLETING);
  bool drain_flag = (inx & NODE_STATE_DRAIN);
  bool fail_flag = (inx & NODE_STATE_FAIL);
  bool maint_flag = (inx & NODE_STATE_MAINT);
  bool net_flag = (inx & NODE_STATE_NET);
  bool reboot_flag = (inx & NODE_STATE_REBOOT);
  bool res_flag = (inx & NODE_STATE_RES);
  bool resume_flag = (inx & NODE_RESUME);
  bool no_resp_flag = (inx & NODE_STATE_NO_RESPOND);
  bool power_down_flag = (inx & NODE_STATE_POWER_SAVE);
  bool power_up_flag = (inx & NODE_STATE_POWER_UP);

  if (maint_flag) {
    if (drain_flag || (base == NODE_STATE_ALLOCATED) ||
        (base == NODE_STATE_DOWN) || (base == NODE_STATE_MIXED))
      ;
    else if (no_resp_flag)
      return MAINT_NONRESP;
    else
      return MAINT;
  }
  if (reboot_flag) {
    if ((base == NODE_STATE_ALLOCATED) || (base == NODE_STATE_MIXED))
      ;
    else if (no_resp_flag)
      return REBOOT_NONRESP;
    else
      return REBOOT;
  }
  if (drain_flag) {
    if (comp_flag || (base == NODE_STATE_ALLOCATED) ||
        (base == NODE_STATE_MIXED)) {
      if (maint_flag)
        return DRAINING_MAINT;
      if (reboot_flag)
        return DRAINING_REBOOT;
      if (power_up_flag)
        return DRAINING_POWERUP;
      if (power_down_flag)
        return DRAINING_POWERDOWN;
      if (no_resp_flag)
        return DRAINING_NONRESP;
      return DRAINING;
    } else {
      if (maint_flag)
        return DRAINED_MAINT;
      if (reboot_flag)
        return DRAINED_REBOOT;
      if (power_up_flag)
        return DRAINED_POWERUP;
      if (power_down_flag)
        return DRAINED_POWERDOWN;
      if (no_resp_flag)
        return DRAINED_NONRESP;
      return DRAINED;
    }
  }
  if (fail_flag) {
    if (comp_flag || (base == NODE_STATE_ALLOCATED)) {
      if (no_resp_flag)
        return FAILING_NONRESP;
      return FAILING;
    } else {
      if (no_resp_flag)
        return FAIL_NONRESP;
      return FAIL;
    }
  }

  if (inx == NODE_STATE_CANCEL_REBOOT)
    return CANCEL_REBOOT;
  if (inx == NODE_STATE_POWER_SAVE)
    return POWER_DOWN;
  if (inx == NODE_STATE_POWER_UP)
    return POWER_UP;
  if (base == NODE_STATE_DOWN) {
    if (maint_flag)
      return DOWN_MAINT;
    if (reboot_flag)
      return DOWN_REBOOT;
    if (power_up_flag)
      return DOWN_POWERUP;
    if (power_down_flag)
      return DOWN_POWERDOWN;
    if (no_resp_flag)
      return DOWN_NONRESP;
    return DOWN;
  }

  if (base == NODE_STATE_ALLOCATED) {
    if (maint_flag)
      return ALLOCATED_MAINT;
    if (reboot_flag)
      return ALLOCATED_REBOOT;
    if (power_up_flag)
      return ALLOCATED_POWERUP;
    if (power_down_flag)
      return ALLOCATED_POWERDOWN;
    if (no_resp_flag)
      return ALLOCATED_NONRESP;
    if (comp_flag)
      return ALLOCATED_COMP;
    return ALLOCATED;
  }
  if (comp_flag) {
    if (maint_flag)
      return COMPLETING_MAINT;
    if (reboot_flag)
      return COMPLETING_REBOOT;
    if (power_up_flag)
      return COMPLETING_POWERUP;
    if (power_down_flag)
      return COMPLETING_POWERDOWN;
    if (no_resp_flag)
      return COMPLETING_NONRESP;
    return COMPLETING;
  }
  if (base == NODE_STATE_IDLE) {
    if (maint_flag)
      return IDLE_MAINT;
    if (reboot_flag)
      return IDLE_REBOOT;
    if (power_up_flag)
      return IDLE_POWERUP;
    if (power_down_flag)
      return IDLE_POWERDOWN;
    if (no_resp_flag)
      return IDLE_NONRESP;
    if (net_flag)
      return PERFCTRS;
    if (res_flag)
      return RESERVED;
    return IDLE;
  }
  if (base == NODE_STATE_MIXED) {
    if (maint_flag)
      return MIXED_MAINT;
    if (reboot_flag)
      return MIXED_REBOOT;
    if (power_up_flag)
      return MIXED_POWERUP;
    if (power_down_flag)
      return MIXED_POWERDOWN;
    if (no_resp_flag)
      return MIXED_NONRESP;
    return MIXED;
  }
  if (base == NODE_STATE_FUTURE) {
    if (maint_flag)
      return FUTURE_MAINT;
    if (reboot_flag)
      return FUTURE_REBOOT;
    if (power_up_flag)
      return FUTURE_POWERUP;
    if (power_down_flag)
      return FUTURE_POWERDOWN;
    if (no_resp_flag)
      return FUTURE_NONRESP;
    return FUTURE;
  }
  if (resume_flag)
    return RESUME;
  if (base == NODE_STATE_UNKNOWN) {
    if (no_resp_flag)
      return UNKNOWN_NONRESP;
    return UNKNOWN;
  }
  return UNKNOWN2;
}
