enum slurm_node_states {
  INVAL,
  MAINT_NONRESP,
  MAINT,
  REBOOT_REBOOT_ISSUED,
  REBOOT_NONRESP,
  REBOOT,
  DRAINING_MAINT,
  DRAINING_REBOOT_ISSUED,
  DRAINING_REBOOT,
  DRAINING_POWERUP,
  DRAINING_POWERING_DOWN,
  DRAINING_POWERED_DOWN,
  DRAINING_POWERDOWN,
  DRAINING_NONRESP,
  DRAINING,
  DRAINED_MAINT,
  DRAINED_REBOOT_ISSUED,
  DRAINED_REBOOT,
  DRAINED_POWERUP,
  DRAINED_POWERING_DOWN,
  DRAINED_POWERED_DOWN,
  DRAINED_POWERDOWN,
  DRAINED_NONRESP,
  DRAINED,
  FAILING_NONRESP,
  FAILING,
  FAIL_NONRESP,
  FAIL,
  REBOOT_ISSUED,
  CANCEL_REBOOT,
  CLOUD,
  POWER_DOWN,
  POWER_UP,
  POWERING_DOWN,
  POWERED_DOWN,
  POWERING_UP,
  DOWN_MAINT,
  DOWN_REBOOT_ISSUED,
  DOWN_REBOOT,
  DOWN_POWERUP,
  DOWN_POWERING_DOWN,
  DOWN_POWERED_DOWN,
  DOWN_POWERDOWN,
  DOWN_NONRESP,
  DOWN,
  ALLOCATED_MAINT,
  ALLOCATED_REBOOT_ISSUED,
  ALLOCATED_REBOOT,
  ALLOCATED_POWERUP,
  ALLOCATED_POWERING_DOWN,
  ALLOCATED_POWERED_DOWN,
  ALLOCATED_POWERDOWN,
  ALLOCATED_NONRESP,
  ALLOCATED_COMP,
  ALLOCATED,
  COMPLETING_MAINT,
  COMPLETING_REBOOT_ISSUED,
  COMPLETING_REBOOT,
  COMPLETING_POWERUP,
  COMPLETING_POWERING_DOWN,
  COMPLETING_POWERED_DOWN,
  COMPLETING_POWERDOWN,
  COMPLETING_NONRESP,
  COMPLETING,
  IDLE_MAINT,
  IDLE_REBOOT_ISSUED,
  IDLE_REBOOT,
  IDLE_POWERUP,
  IDLE_POWERING_DOWN,
  IDLE_POWERED_DOWN,
  IDLE_POWERDOWN,
  IDLE_NONRESP,
  PERFCTRS,
  RESERVED,
  PLANNED,
  IDLE,
  MIXED_MAINT,
  MIXED_REBOOT_ISSUED,
  MIXED_REBOOT,
  MIXED_POWERUP,
  MIXED_POWERING_DOWN,
  MIXED_POWERED_DOWN,
  MIXED_POWERDOWN,
  MIXED_NONRESP,
  MIXED_PLANNED,
  MIXED,
  FUTURE_MAINT,
  FUTURE_REBOOT_ISSUED,
  FUTURE_REBOOT,
  FUTURE_POWERUP,
  FUTURE_POWERING_DOWN,
  FUTURE_POWERED_DOWN,
  FUTURE_POWERDOWN,
  FUTURE_NONRESP,
  FUTURE,
  RESUME,
  UNKNOWN_NONRESP,
  UNKNOWN,
  UNKNOWN2
};

char *node_state_names[] = {"INVAL",
                            "MAINT_NONRESP",
                            "MAINT",
                            "REBOOT_REBOOT_ISSUED",
                            "REBOOT_NONRESP",
                            "REBOOT",
                            "DRAINING_MAINT",
                            "DRAINING_REBOOT_ISSUED",
                            "DRAINING_REBOOT",
                            "DRAINING_POWERUP",
                            "DRAINING_POWERING_DOWN",
                            "DRAINING_POWERED_DOWN",
                            "DRAINING_POWERDOWN",
                            "DRAINING_NONRESP",
                            "DRAINING",
                            "DRAINED_MAINT",
                            "DRAINED_REBOOT_ISSUED",
                            "DRAINED_REBOOT",
                            "DRAINED_POWERUP",
                            "DRAINED_POWERING_DOWN",
                            "DRAINED_POWERED_DOWN",
                            "DRAINED_POWERDOWN",
                            "DRAINED_NONRESP",
                            "DRAINED",
                            "FAILING_NONRESP",
                            "FAILING",
                            "FAIL_NONRESP",
                            "FAIL",
                            "REBOOT_ISSUED",
                            "CANCEL_REBOOT",
                            "CLOUD",
                            "POWER_DOWN",
                            "POWER_UP",
                            "POWERING_DOWN",
                            "POWERED_DOWN",
                            "POWERING_UP",
                            "DOWN_MAINT",
                            "DOWN_REBOOT_ISSUED",
                            "DOWN_REBOOT",
                            "DOWN_POWERUP",
                            "DOWN_POWERING_DOWN",
                            "DOWN_POWERED_DOWN",
                            "DOWN_POWERDOWN",
                            "DOWN_NONRESP",
                            "DOWN",
                            "ALLOCATED_MAINT",
                            "ALLOCATED_REBOOT_ISSUED",
                            "ALLOCATED_REBOOT",
                            "ALLOCATED_POWERUP",
                            "ALLOCATED_POWERING_DOWN",
                            "ALLOCATED_POWERED_DOWN",
                            "ALLOCATED_POWERDOWN",
                            "ALLOCATED_NONRESP",
                            "ALLOCATED_COMP",
                            "ALLOCATED",
                            "COMPLETING_MAINT",
                            "COMPLETING_REBOOT_ISSUED",
                            "COMPLETING_REBOOT",
                            "COMPLETING_POWERUP",
                            "COMPLETING_POWERING_DOWN",
                            "COMPLETING_POWERED_DOWN",
                            "COMPLETING_POWERDOWN",
                            "COMPLETING_NONRESP",
                            "COMPLETING",
                            "IDLE_MAINT",
                            "IDLE_REBOOT_ISSUED",
                            "IDLE_REBOOT",
                            "IDLE_POWERUP",
                            "IDLE_POWERING_DOWN",
                            "IDLE_POWERED_DOWN",
                            "IDLE_POWERDOWN",
                            "IDLE_NONRESP",
                            "PERFCTRS",
                            "RESERVED",
                            "PLANNED",
                            "IDLE",
                            "MIXED_MAINT",
                            "MIXED_REBOOT_ISSUED",
                            "MIXED_REBOOT",
                            "MIXED_POWERUP",
                            "MIXED_POWERING_DOWN",
                            "MIXED_POWERED_DOWN",
                            "MIXED_POWERDOWN",
                            "MIXED_NONRESP",
                            "MIXED_PLANNED",
                            "MIXED",
                            "FUTURE_MAINT",
                            "FUTURE_REBOOT_ISSUED",
                            "FUTURE_REBOOT",
                            "FUTURE_POWERUP",
                            "FUTURE_POWERING_DOWN",
                            "FUTURE_POWERED_DOWN",
                            "FUTURE_POWERDOWN",
                            "FUTURE_NONRESP",
                            "FUTURE",
                            "RESUME",
                            "UNKNOWN_NONRESP",
                            "UNKNOWN",
                            "UNKNOWN2"};

/* based on src/common/slurm_protocol_defs.c node_state_string function */
uint8_t slurm_node_state(uint32_t inx) {
  int base = (inx & NODE_STATE_BASE);
  bool comp_flag = (inx & NODE_STATE_COMPLETING);
  bool drain_flag = (inx & NODE_STATE_DRAIN);
  bool fail_flag = (inx & NODE_STATE_FAIL);
  bool maint_flag = (inx & NODE_STATE_MAINT);
  bool net_flag = (inx & NODE_STATE_NET);
  bool reboot_flag = (inx & NODE_STATE_REBOOT_REQUESTED);
  bool reboot_issued_flag = (inx & NODE_STATE_REBOOT_ISSUED);
  bool res_flag = (inx & NODE_STATE_RES);
  bool resume_flag = (inx & NODE_RESUME);
  bool no_resp_flag = (inx & NODE_STATE_NO_RESPOND);
  bool planned_flag = (inx & NODE_STATE_PLANNED);
  bool powered_down_flag = (inx & NODE_STATE_POWERED_DOWN);
  bool power_up_flag = (inx & NODE_STATE_POWERING_UP);
  bool powering_down_flag = (inx & NODE_STATE_POWERING_DOWN);
  bool power_down_flag = (inx & NODE_STATE_POWER_DOWN);

  if (inx & NODE_STATE_INVALID_REG)
    return INVAL;

  if (maint_flag) {
    if (drain_flag || (base == NODE_STATE_ALLOCATED) ||
        (base == NODE_STATE_DOWN) || (base == NODE_STATE_MIXED))
      ;
    else if (no_resp_flag)
      return MAINT_NONRESP;
    else
      return MAINT;
  }
  if (reboot_flag || reboot_issued_flag) {
    if ((base == NODE_STATE_ALLOCATED) || (base == NODE_STATE_MIXED))
      ;
    else if (reboot_issued_flag)
      return REBOOT_REBOOT_ISSUED;
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
      if (reboot_issued_flag)
        return DRAINING_REBOOT_ISSUED;
      if (reboot_flag)
        return DRAINING_REBOOT;
      if (power_up_flag)
        return DRAINING_POWERUP;
      if (powering_down_flag)
        return DRAINING_POWERING_DOWN;
      if (powered_down_flag)
        return DRAINING_POWERED_DOWN;
      if (power_down_flag)
        return DRAINING_POWERDOWN;
      if (no_resp_flag)
        return DRAINING_NONRESP;
      return DRAINING;
    } else {
      if (maint_flag)
        return DRAINED_MAINT;
      if (reboot_issued_flag)
        return DRAINED_REBOOT_ISSUED;
      if (reboot_flag)
        return DRAINED_REBOOT;
      if (power_up_flag)
        return DRAINED_POWERUP;
      if (powering_down_flag)
        return DRAINED_POWERING_DOWN;
      if (powered_down_flag)
        return DRAINED_POWERED_DOWN;
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

  if (inx == NODE_STATE_REBOOT_ISSUED)
    return REBOOT_ISSUED;
  if (inx == NODE_STATE_REBOOT_CANCEL)
    return CANCEL_REBOOT;
  if (inx == NODE_STATE_CLOUD)
    return CLOUD;
  if (inx == NODE_STATE_POWER_DOWN)
    return POWER_DOWN;
  if (inx == NODE_STATE_POWER_UP)
    return POWER_UP;
  if (inx == NODE_STATE_POWERING_DOWN)
    return POWERING_DOWN;
  if (inx == NODE_STATE_POWERED_DOWN)
    return POWERED_DOWN;
  if (inx == NODE_STATE_POWERING_UP)
    return POWERING_UP;
  if (base == NODE_STATE_DOWN) {
    if (maint_flag)
      return DOWN_MAINT;
    if (reboot_issued_flag)
      return DOWN_REBOOT_ISSUED;
    if (reboot_flag)
      return DOWN_REBOOT;
    if (power_up_flag)
      return DOWN_POWERUP;
    if (powering_down_flag)
      return DOWN_POWERING_DOWN;
    if (powered_down_flag)
      return DOWN_POWERED_DOWN;
    if (power_down_flag)
      return DOWN_POWERDOWN;
    if (no_resp_flag)
      return DOWN_NONRESP;
    return DOWN;
  }

  if (base == NODE_STATE_ALLOCATED) {
    if (maint_flag)
      return ALLOCATED_MAINT;
    if (reboot_issued_flag)
      return ALLOCATED_REBOOT_ISSUED;
    if (reboot_flag)
      return ALLOCATED_REBOOT;
    if (power_up_flag)
      return ALLOCATED_POWERUP;
    if (powering_down_flag)
      return ALLOCATED_POWERING_DOWN;
    if (powered_down_flag)
      return ALLOCATED_POWERED_DOWN;
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
    if (reboot_issued_flag)
      return COMPLETING_REBOOT_ISSUED;
    if (reboot_flag)
      return COMPLETING_REBOOT;
    if (power_up_flag)
      return COMPLETING_POWERUP;
    if (powering_down_flag)
      return COMPLETING_POWERING_DOWN;
    if (powered_down_flag)
      return COMPLETING_POWERED_DOWN;
    if (power_down_flag)
      return COMPLETING_POWERDOWN;
    if (no_resp_flag)
      return COMPLETING_NONRESP;
    return COMPLETING;
  }
  if (base == NODE_STATE_IDLE) {
    if (maint_flag)
      return IDLE_MAINT;
    if (reboot_issued_flag)
      return IDLE_REBOOT_ISSUED;
    if (reboot_flag)
      return IDLE_REBOOT;
    if (power_up_flag)
      return IDLE_POWERUP;
    if (powering_down_flag)
      return IDLE_POWERING_DOWN;
    if (powered_down_flag)
      return IDLE_POWERED_DOWN;
    if (power_down_flag)
      return IDLE_POWERDOWN;
    if (no_resp_flag)
      return IDLE_NONRESP;
    if (net_flag)
      return PERFCTRS;
    if (res_flag)
      return RESERVED;
    if (planned_flag)
      return PLANNED;
    return IDLE;
  }
  if (base == NODE_STATE_MIXED) {
    if (maint_flag)
      return MIXED_MAINT;
    if (reboot_issued_flag)
      return MIXED_REBOOT_ISSUED;
    if (reboot_flag)
      return MIXED_REBOOT;
    if (power_up_flag)
      return MIXED_POWERUP;
    if (powering_down_flag)
      return MIXED_POWERING_DOWN;
    if (powered_down_flag)
      return MIXED_POWERED_DOWN;
    if (power_down_flag)
      return MIXED_POWERDOWN;
    if (no_resp_flag)
      return MIXED_NONRESP;
    if (planned_flag)
      return MIXED_PLANNED;
    return MIXED;
  }
  if (base == NODE_STATE_FUTURE) {
    if (maint_flag)
      return FUTURE_MAINT;
    if (reboot_issued_flag)
      return FUTURE_REBOOT_ISSUED;
    if (reboot_flag)
      return FUTURE_REBOOT;
    if (power_up_flag)
      return FUTURE_POWERUP;
    if (powering_down_flag)
      return FUTURE_POWERING_DOWN;
    if (powered_down_flag)
      return FUTURE_POWERED_DOWN;
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
