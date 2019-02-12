/**
 * collectd - src/linux_delay.c
 * Copyright (C) 2019       Florian octo Forster
 *
 * ISC License (ISC)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

#include "collectd.h"

#include "utils/common/common.h"
#include "plugin.h"
#include "utils_time.h"

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/taskstats.h>

typedef struct {
  struct mnl_socket *nl;
  pid_t pid;
  uint32_t seq;
  uint16_t genl_id_taskstats;
  unsigned int port_id;
} ld_ctx_t;

static int nlmsg_errno(struct nlmsghdr *nlh, size_t sz) {
  if (!mnl_nlmsg_ok(nlh, (int)sz)) {
    ERROR("linux_delay plugin: mnl_nlmsg_ok failed.");
    return EPROTO;
  }

  if (nlh->nlmsg_type != NLMSG_ERROR) {
    return 0;
  }

  struct nlmsgerr *nlerr = mnl_nlmsg_get_payload(nlh);
  /* (struct nlmsgerr).error holds a negative errno. */
  return nlerr->error * (-1);
}

static int get_taskstats_attr_cb(const struct nlattr *attr, void *data) {
  struct taskstats *ret_taskstats = data;

  uint16_t type = mnl_attr_get_type(attr);
  DEBUG("linux_delay plugin: get_taskstats_attr_cb(%" PRIu16 ")", type);
  switch (type) {
  case TASKSTATS_TYPE_STATS:
    if (mnl_attr_get_payload_len(attr) != sizeof(*ret_taskstats)) {
      ERROR("linux_delay plugin: mnl_attr_get_payload_len(attr) = %" PRIu32
            ", want %zu",
            mnl_attr_get_payload_len(attr), sizeof(*ret_taskstats));
      return MNL_CB_ERROR;
    }
    struct taskstats *ts = mnl_attr_get_payload(attr);
    memmove(ret_taskstats, ts, sizeof(*ret_taskstats));
    DEBUG("linux_delay plugin: Successfully received struct taskstats.");
    return MNL_CB_OK;

  case TASKSTATS_TYPE_AGGR_PID: /* fall through */
  case TASKSTATS_TYPE_AGGR_TGID:
    return mnl_attr_parse_nested(attr, get_taskstats_attr_cb, ret_taskstats);

  case TASKSTATS_TYPE_PID: /* fall through */
  case TASKSTATS_TYPE_TGID:
    /* ignore */
    return MNL_CB_OK;

  default:
    DEBUG("linux_delay plugin: unknown attribute %" PRIu16
          ", want one of TASKSTATS_TYPE_AGGR_PID, TASKSTATS_TYPE_AGGR_TGID, "
          "TASKSTATS_TYPE_STATS",
          type);
  }
  return MNL_CB_OK;
}

static int get_taskstats_msg_cb(const struct nlmsghdr *nlh, void *data) {
  DEBUG("linux_delay: get_taskstats_msg_cb()");
  return mnl_attr_parse(nlh, sizeof(struct genlmsghdr), get_taskstats_attr_cb,
                        data);
}

static int get_taskstats(ld_ctx_t *ctx, pid_t pid,
                         struct taskstats *ret_taskstats) {
  char buffer[MNL_SOCKET_BUFFER_SIZE];
  uint32_t seq = ctx->seq++;

  struct nlmsghdr *nlh = mnl_nlmsg_put_header(buffer);
  *nlh = (struct nlmsghdr){
      .nlmsg_len = nlh->nlmsg_len,
      .nlmsg_type = ctx->genl_id_taskstats,
      .nlmsg_flags = NLM_F_REQUEST,
      .nlmsg_seq = seq,
      .nlmsg_pid = ctx->pid,
  };

  struct genlmsghdr *genh = mnl_nlmsg_put_extra_header(nlh, sizeof(*genh));
  *genh = (struct genlmsghdr){
      .cmd = TASKSTATS_CMD_GET,
      .version = TASKSTATS_GENL_VERSION, // or TASKSTATS_VERSION?
  };

  // TODO(octo): can we get all the processes/threads if we don't set an
  // attribute?
  mnl_attr_put_u32(nlh, TASKSTATS_CMD_ATTR_PID, (uint32_t)pid);
  // or: TASKSTATS_CMD_ATTR_TGID
  // mnl_attr_put_strz(nlh, TASKSTATS_CMD_ATTR_REGISTER_CPUMASK, "0");

  if (mnl_socket_sendto(ctx->nl, nlh, nlh->nlmsg_len) < 0) {
    int status = errno;
    ERROR("linux_delay plugin: mnl_socket_sendto() = %s", STRERROR(status));
    return status;
  }

  DEBUG("linux_delay plugin: mnl_socket_recvfrom() ...");
  int status = mnl_socket_recvfrom(ctx->nl, buffer, sizeof(buffer));
  if (status < 0) {
    status = errno;
    ERROR("linux_delay plugin: mnl_socket_recvfrom() = %s", STRERROR(status));
    return status;
  } else if (status == 0) {
    ERROR("linux_delay plugin: mnl_socket_recvfrom() = 0");
    return ECONNABORTED;
  }
  size_t buffer_size = (size_t)status;
  DEBUG("linux_delay plugin: buffer_size = %zu", buffer_size);

  if ((status = nlmsg_errno((void *)buffer, buffer_size)) != 0) {
    ERROR("linux_delay plugin: TASKSTATS_CMD_GET(TASKSTATS_CMD_ATTR_PID = "
          "%" PRIu32 ") = %s",
          (uint32_t)pid, STRERROR(status));
    return status;
  }

  status = mnl_cb_run(buffer, buffer_size, seq, ctx->port_id,
                      get_taskstats_msg_cb, ret_taskstats);
  if (status < MNL_CB_STOP) {
    ERROR("linux_delay plugin: Parsing message failed.");
    return EPROTO;
  }

  return 0;
}

static int get_family_id_attr_cb(const struct nlattr *attr, void *data) {
  uint16_t type = mnl_attr_get_type(attr);
  DEBUG("linux_delay plugin: get_family_id_attr_cb(%" PRIu16 ")", type);
  if (type != CTRL_ATTR_FAMILY_ID) {
    return MNL_CB_OK;
  }

  if (mnl_attr_validate(attr, MNL_TYPE_U16) < 0) {
    ERROR("mnl_attr_validate() = %s", STRERRNO);
    return MNL_CB_ERROR;
  }

  uint16_t *ret_family_id = data;
  *ret_family_id = mnl_attr_get_u16(attr);
  return MNL_CB_STOP;
}

static int get_family_id_msg_cb(const struct nlmsghdr *nlh, void *data) {
  DEBUG("linux_delay plugin: get_family_id_msg_cb()");
  return mnl_attr_parse(nlh, sizeof(struct genlmsghdr), get_family_id_attr_cb,
                        data);
}

/* get_family_id initializes ctx->genl_id_taskstats. Returns 0 on success and
 * an error code otherwise. */
static int get_family_id(ld_ctx_t *ctx) {
  char buffer[MNL_SOCKET_BUFFER_SIZE];
  uint32_t seq = ctx->seq++;

  struct nlmsghdr *nlh = mnl_nlmsg_put_header(buffer);
  DEBUG("linux_delay plugin: nlh->nlmsg_len = %" PRIu32,
        (uint32_t)nlh->nlmsg_len);
  *nlh = (struct nlmsghdr){
      .nlmsg_len = nlh->nlmsg_len,
      .nlmsg_type = GENL_ID_CTRL,
      .nlmsg_flags = NLM_F_REQUEST,
      .nlmsg_seq = seq,
      .nlmsg_pid = ctx->pid,
  };
  DEBUG("linux_delay plugin: nlh->nlmsg_len = %" PRIu32,
        (uint32_t)nlh->nlmsg_len);

  struct genlmsghdr *genh = mnl_nlmsg_put_extra_header(nlh, sizeof(*genh));
  *genh = (struct genlmsghdr){
      .cmd = CTRL_CMD_GETFAMILY, .version = 0x01,
  };

  mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, TASKSTATS_GENL_NAME);

  assert(genh->cmd == CTRL_CMD_GETFAMILY);
  assert(genh->version == TASKSTATS_GENL_VERSION);

  if (mnl_socket_sendto(ctx->nl, nlh, nlh->nlmsg_len) < 0) {
    int status = errno;
    ERROR("linux_delay plugin: mnl_socket_sendto() = %s", STRERROR(status));
    return status;
  }

  ctx->genl_id_taskstats = 0;
  while (42) {
    int status = mnl_socket_recvfrom(ctx->nl, buffer, sizeof(buffer));
    if (status < 0) {
      status = errno;
      ERROR("linux_delay plugin: mnl_socket_recvfrom() = %s", STRERROR(status));
      return status;
    } else if (status == 0) {
      break;
    }
    size_t buffer_size = (size_t)status;

    if ((status = nlmsg_errno((void *)buffer, buffer_size)) != 0) {
      ERROR("linux_delay plugin: CTRL_CMD_GETFAMILY(\"%s\"): %s",
            TASKSTATS_GENL_NAME, STRERROR(status));
      return status;
    }

    status = mnl_cb_run(buffer, buffer_size, seq, ctx->port_id,
                        get_family_id_msg_cb, &ctx->genl_id_taskstats);
    if (status < MNL_CB_STOP) {
      ERROR("linux_delay plugin: Parsing message failed.");
      return EPROTO;
    } else if (status == MNL_CB_STOP) {
      break;
    }
  }

  if (ctx->genl_id_taskstats == 0) {
    ERROR("linux_delay plugin: Netlink communication succeeded, but "
          "genl_id_taskstats is still zero.");
    return ENOENT;
  }

  return 0;
}

static void ld_context_destroy(void *data) {
  ld_ctx_t *ctx = data;

  if (ctx == NULL) {
    return;
  }

  if (ctx->nl != NULL) {
    mnl_socket_close(ctx->nl);
    ctx->nl = NULL;
  }

  sfree(ctx);
}

static ld_ctx_t *ld_context_create(void) {
  ld_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL) {
    ERROR("linux_delay plugin: calloc failed: %s", STRERRNO);
    return NULL;
  }

  if ((ctx->nl = mnl_socket_open(NETLINK_GENERIC)) == NULL) {
    ERROR("linux_delay plugin: mnl_socket_open(NETLINK_GENERIC) = %s",
          STRERRNO);
    ld_context_destroy(ctx);
    return NULL;
  }

  if (mnl_socket_bind(ctx->nl, 0, MNL_SOCKET_AUTOPID) != 0) {
    ERROR("linux_delay plugin: mnl_socket_bind() = %s", STRERRNO);
    ld_context_destroy(ctx);
    return NULL;
  }

  ctx->pid = getpid();
  ctx->port_id = mnl_socket_get_portid(ctx->nl);

  int status = get_family_id(ctx);
  if (status != 0) {
    ERROR("linux_delay plugin: get_family_id() = %s", STRERROR(status));
    ld_context_destroy(ctx);
    return NULL;
  }

  return ctx;
}

static int ld_read(user_data_t *ud) {
  if (ud == NULL) {
    return EINVAL;
  }

  if (ud->data == NULL) {
    ud->data = ld_context_create();
    if (ud->data == NULL) {
      return EAGAIN;
    }
    ud->free_func = ld_context_destroy;
  }

  ld_ctx_t *ctx = ud->data;

  // TODO(octo): we're only reading about ourselves for now.
  struct taskstats ts = {0};
  int status = get_taskstats(ctx, getpid(), &ts);
  if (status != 0) {
    return status;
  }

  cdtime_t cpu = NS_TO_CDTIME_T(ts.cpu_delay_total);
  cdtime_t blkio = NS_TO_CDTIME_T(ts.blkio_delay_total);
  cdtime_t swapin = NS_TO_CDTIME_T(ts.swapin_delay_total);
  cdtime_t freepages = NS_TO_CDTIME_T(ts.freepages_delay_total);

  INFO("linux_delay plugin: ac_comm = \"%s\", cpu_delay_total=%.3f, "
        "blkio_delay_total=%.3f, swapin_delay_total=%.3f, "
        "freepages_delay_total=%.3f",
        ts.ac_comm, CDTIME_T_TO_DOUBLE(cpu), CDTIME_T_TO_DOUBLE(blkio),
        CDTIME_T_TO_DOUBLE(swapin), CDTIME_T_TO_DOUBLE(freepages));

  return 0;
}

void module_register(void) {
  plugin_register_complex_read(NULL, "linux_delay", ld_read, 0,
                               &(user_data_t){.data = NULL});
} /* void module_register */
