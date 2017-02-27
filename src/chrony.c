/* chrony plugin for collectd (monitoring of chrony time server daemon)
 **********************************************************************
 * Copyright (C) Claudius M Zingerli, ZSeng, 2015-2016
 *
 * Internals roughly based on the ntpd plugin
 * Some functions copied from chronyd/web (as marked)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * TODO:
 *	- More robust udp parsing (using offsets instead of structs?)
 *	  -> Currently chrony parses its data the same way as we do (using
 *structs)
 *	- Plausibility checks on values received
 *	  -> Done at higher levels
 */

#include "collectd.h"

#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */

#if HAVE_NETDB_H
#include <netdb.h> /* struct addrinfo */
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h> /* ntohs/ntohl */
#endif

#define CONFIG_KEY_HOST "Host"
#define CONFIG_KEY_PORT "Port"
#define CONFIG_KEY_TIMEOUT "Timeout"

#define URAND_DEVICE_PATH                                                      \
  "/dev/urandom" /* Used to initialize seq nr generator */
#define RAND_DEVICE_PATH                                                       \
  "/dev/random" /* Used to initialize seq nr generator (fall back) */

static const char *g_config_keys[] = {CONFIG_KEY_HOST, CONFIG_KEY_PORT,
                                      CONFIG_KEY_TIMEOUT};

static int g_config_keys_num = STATIC_ARRAY_SIZE(g_config_keys);
static int g_chrony_is_connected;
static int g_chrony_socket = -1;
static time_t g_chrony_timeout = -1;
static char *g_chrony_plugin_instance;
static char *g_chrony_host;
static char *g_chrony_port;
static uint32_t g_chrony_rand = 1;
static uint32_t g_chrony_seq_is_initialized;

#define PLUGIN_NAME_SHORT "chrony"
#define PLUGIN_NAME PLUGIN_NAME_SHORT " plugin"
#define DAEMON_NAME PLUGIN_NAME_SHORT
#define CHRONY_DEFAULT_HOST "localhost"
#define CHRONY_DEFAULT_PORT "323"
#define CHRONY_DEFAULT_TIMEOUT 2

/* Return codes (collectd expects non-zero on errors) */
#define CHRONY_RC_OK 0
#define CHRONY_RC_FAIL 1

/* Chronyd command packet variables adapted from chrony/candm.h (GPL2) */
#define PROTO_VERSION_NUMBER 6
#define IPADDR_UNSPEC 0
#define IPADDR_INET4 1
#define IPADDR_INET6 2
#define IPV6_STR_MAX_SIZE (8 * 4 + 7 + 1)

typedef enum { PKT_TYPE_CMD_REQUEST = 1, PKT_TYPE_CMD_REPLY = 2 } ePacketType;

typedef enum {
  REQ_N_SOURCES = 14,
  REQ_SOURCE_DATA = 15,
  REQ_TRACKING = 33,
  REQ_SOURCE_STATS = 34
} eDaemonRequests;

typedef enum {
  RPY_NULL = 1,
  RPY_N_SOURCES = 2,
  RPY_SOURCE_DATA = 3,
  RPY_MANUAL_TIMESTAMP = 4,
  RPY_TRACKING = 5,
  RPY_SOURCE_STATS = 6,
  RPY_RTC = 7
} eDaemonReplies;

#if defined(__GNUC__) || defined(__SUNPRO_C) || defined(lint)
#/* extension to enforce struct packing. */
#define ATTRIB_PACKED __attribute__((packed))
#else
#error Not defining packed attribute (unknown compiler)
#define ATTRIB_PACKED
#endif

typedef struct ATTRIB_PACKED { int32_t value; } tFloat;

typedef struct ATTRIB_PACKED {
  uint32_t tv_sec_high;
  uint32_t tv_sec_low;
  uint32_t tv_nsec;
} tTimeval;

typedef enum {
  STT_SUCCESS = 0,
  STT_FAILED = 1,
  STT_UNAUTH = 2,
  STT_INVALID = 3,
  STT_NOSUCHSOURCE = 4,
  STT_INVALIDTS = 5,
  STT_NOTENABLED = 6,
  STT_BADSUBNET = 7,
  STT_ACCESSALLOWED = 8,
  STT_ACCESSDENIED = 9,
  STT_NOHOSTACCESS = 10,
  STT_SOURCEALREADYKNOWN = 11,
  STT_TOOMANYSOURCES = 12,
  STT_NORTC = 13,
  STT_BADRTCFILE = 14,
  STT_INACTIVE = 15,
  STT_BADSAMPLE = 16,
  STT_INVALIDAF = 17,
  STT_BADPKTVERSION = 18,
  STT_BADPKTLENGTH = 19
} eChrony_Status;

/* Chrony client request packets */
typedef struct ATTRIB_PACKED {
  uint8_t f_dummy0[80]; /* Chrony expects 80bytes dummy data (Avoiding UDP
                           Amplification) */
} tChrony_Req_Tracking;

typedef struct ATTRIB_PACKED { uint32_t f_n_sources; } tChrony_Req_N_Sources;

typedef struct ATTRIB_PACKED {
  int32_t f_index;
  uint8_t f_dummy0[44];
} tChrony_Req_Source_data;

typedef struct ATTRIB_PACKED {
  int32_t f_index;
  uint8_t f_dummy0[56];
} tChrony_Req_Source_stats;

typedef struct ATTRIB_PACKED {
  struct {
    uint8_t f_version;
    uint8_t f_type;
    uint8_t f_dummy0;
    uint8_t f_dummy1;
    uint16_t f_cmd;
    uint16_t f_cmd_try;
    uint32_t f_seq;

    uint32_t f_dummy2;
    uint32_t f_dummy3;
  } header; /* Packed: 20Bytes */
  union {
    tChrony_Req_N_Sources n_sources;
    tChrony_Req_Source_data source_data;
    tChrony_Req_Source_stats source_stats;
    tChrony_Req_Tracking tracking;
  } body;
  uint8_t padding[4 + 16]; /* Padding to match minimal response size */
} tChrony_Request;

/* Chrony daemon response packets */
typedef struct ATTRIB_PACKED { uint32_t f_n_sources; } tChrony_Resp_N_Sources;

typedef struct ATTRIB_PACKED {
  union {
    uint32_t ip4;
    uint8_t ip6[16];
  } addr;
  uint16_t f_family;
} tChrony_IPAddr;

typedef struct ATTRIB_PACKED {
  tChrony_IPAddr addr;
  uint16_t
      dummy; /* FIXME: Strange dummy space. Needed on gcc 4.8.3/clang 3.4.1 on
                x86_64 */
  int16_t f_poll;     /* 2^f_poll = Time between polls (s) */
  uint16_t f_stratum; /* Remote clock stratum */
  uint16_t f_state;   /* 0 = RPY_SD_ST_SYNC,    1 = RPY_SD_ST_UNREACH,   2 =
                         RPY_SD_ST_FALSETICKER */
  /* 3 = RPY_SD_ST_JITTERY, 4 = RPY_SD_ST_CANDIDATE, 5 = RPY_SD_ST_OUTLIER */
  uint16_t f_mode;  /* 0 = RPY_SD_MD_CLIENT,  1 = RPY_SD_MD_PEER,      2 =
                       RPY_SD_MD_REF         */
  uint16_t f_flags; /* unused */
  uint16_t
      f_reachability; /* Bit mask of successfull tries to reach the source */

  uint32_t f_since_sample;     /* Time since last sample (s) */
  tFloat f_origin_latest_meas; /*  */
  tFloat f_latest_meas;        /*  */
  tFloat f_latest_meas_err;    /*  */
} tChrony_Resp_Source_data;

typedef struct ATTRIB_PACKED {
  uint32_t f_ref_id;
  tChrony_IPAddr addr;
  uint16_t
      dummy; /* FIXME: Strange dummy space. Needed on gcc 4.8.3/clang 3.4.1 on
                x86_64 */
  uint32_t f_n_samples;       /* Number of measurements done   */
  uint32_t f_n_runs;          /* How many measurements to come */
  uint32_t f_span_seconds;    /* For how long we're measuring  */
  tFloat f_rtc_seconds_fast;  /* ??? */
  tFloat f_rtc_gain_rate_ppm; /* Estimated relative frequency error */
  tFloat f_skew_ppm;       /* Clock skew (ppm) (worst case freq est error (skew:
                              peak2peak)) */
  tFloat f_est_offset;     /* Estimated offset of source */
  tFloat f_est_offset_err; /* Error of estimation        */
} tChrony_Resp_Source_stats;

typedef struct ATTRIB_PACKED {
  uint32_t f_ref_id;
  tChrony_IPAddr addr;
  uint16_t
      dummy; /* FIXME: Strange dummy space. Needed on gcc 4.8.3/clang 3.4.1 on
                x86_64 */
  uint16_t f_stratum;
  uint16_t f_leap_status;
  tTimeval f_ref_time;
  tFloat f_current_correction;
  tFloat f_last_offset;
  tFloat f_rms_offset;
  tFloat f_freq_ppm;
  tFloat f_resid_freq_ppm;
  tFloat f_skew_ppm;
  tFloat f_root_delay;
  tFloat f_root_dispersion;
  tFloat f_last_update_interval;
} tChrony_Resp_Tracking;

typedef struct ATTRIB_PACKED {
  struct {
    uint8_t f_version;
    uint8_t f_type;
    uint8_t f_dummy0;
    uint8_t f_dummy1;
    uint16_t f_cmd;
    uint16_t f_reply;
    uint16_t f_status;
    uint16_t f_dummy2;
    uint16_t f_dummy3;
    uint16_t f_dummy4;
    uint32_t f_seq;
    uint32_t f_dummy5;
    uint32_t f_dummy6;
  } header; /* Packed: 28 Bytes */

  union {
    tChrony_Resp_N_Sources n_sources;
    tChrony_Resp_Source_data source_data;
    tChrony_Resp_Source_stats source_stats;
    tChrony_Resp_Tracking tracking;
  } body;

  uint8_t padding[1024];
} tChrony_Response;

/*****************************************************************************/
/* Internal functions */
/*****************************************************************************/

/* connect_client code adapted from:
 * http://long.ccaba.upc.edu/long/045Guidelines/eva/ipv6.html#daytimeClient6 */
/* License granted by Eva M Castro via e-mail on 2016-02-18 under the terms of
 * GPLv3 */
static int connect_client(const char *p_hostname, const char *p_service,
                          int p_family, int p_socktype) {
  struct addrinfo *res, *ressave;
  int n, sockfd;

  struct addrinfo ai_hints = {.ai_family = p_family, .ai_socktype = p_socktype};

  n = getaddrinfo(p_hostname, p_service, &ai_hints, &res);

  if (n < 0) {
    ERROR(PLUGIN_NAME ": getaddrinfo error:: [%s]", gai_strerror(n));
    return -1;
  }

  ressave = res;

  sockfd = -1;
  while (res) {
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (!(sockfd < 0)) {
      if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0) {
        /* Success */
        break;
      }

      close(sockfd);
      sockfd = -1;
    }
    res = res->ai_next;
  }

  freeaddrinfo(ressave);
  return sockfd;
}

/* niptoha code originally from:
 * git://git.tuxfamily.org/gitroot/chrony/chrony.git:util.c */
/* Original code licensed as GPLv2, by Richard P. Purnow, Miroslav Lichvar */
/* Original name: char * UTI_IPToString(IPAddr *addr)*/
static char *niptoha(const tChrony_IPAddr *addr, char *p_buf,
                     size_t p_buf_size) {
  int rc = 1;
  unsigned long a, b, c, d, ip;

  switch (ntohs(addr->f_family)) {
  case IPADDR_UNSPEC:
    rc = snprintf(p_buf, p_buf_size, "[UNSPEC]");
    break;
  case IPADDR_INET4:
    ip = ntohl(addr->addr.ip4);
    a = (ip >> 24) & 0xff;
    b = (ip >> 16) & 0xff;
    c = (ip >> 8) & 0xff;
    d = (ip >> 0) & 0xff;
    rc = snprintf(p_buf, p_buf_size, "%ld.%ld.%ld.%ld", a, b, c, d);
    break;
  case IPADDR_INET6: {
    const char *rp = inet_ntop(AF_INET6, addr->addr.ip6, p_buf, p_buf_size);
    if (rp == NULL) {
      ERROR(PLUGIN_NAME ": Error converting ipv6 address to string. Errno = %d",
            errno);
      rc = snprintf(p_buf, p_buf_size, "[UNKNOWN]");
    }
    break;
  }
  default:
    rc = snprintf(p_buf, p_buf_size, "[UNKNOWN]");
  }
  assert(rc > 0);
  return p_buf;
}

static int chrony_set_timeout(void) {
  /* Set the socket's  timeout to g_chrony_timeout; a value of 0 signals
   * infinite timeout */
  /* Returns 0 on success, !0 on error (check errno) */

  struct timeval tv;
  tv.tv_sec = g_chrony_timeout;
  tv.tv_usec = 0;

  assert(g_chrony_socket >= 0);
  if (setsockopt(g_chrony_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
                 sizeof(struct timeval)) < 0) {
    return CHRONY_RC_FAIL;
  }
  return CHRONY_RC_OK;
}

static int chrony_connect(void) {
  /* Connects to the chrony daemon */
  /* Returns 0 on success, !0 on error (check errno) */
  int socket;

  if (g_chrony_host == NULL) {
    g_chrony_host = strdup(CHRONY_DEFAULT_HOST);
    if (g_chrony_host == NULL) {
      ERROR(PLUGIN_NAME ": Error duplicating chrony host name");
      return CHRONY_RC_FAIL;
    }
  }
  if (g_chrony_port == NULL) {
    g_chrony_port = strdup(CHRONY_DEFAULT_PORT);
    if (g_chrony_port == NULL) {
      ERROR(PLUGIN_NAME ": Error duplicating chrony port string");
      return CHRONY_RC_FAIL;
    }
  }
  if (g_chrony_timeout < 0) {
    g_chrony_timeout = CHRONY_DEFAULT_TIMEOUT;
    assert(g_chrony_timeout >= 0);
  }

  DEBUG(PLUGIN_NAME ": Connecting to %s:%s", g_chrony_host, g_chrony_port);
  socket = connect_client(g_chrony_host, g_chrony_port, AF_UNSPEC, SOCK_DGRAM);
  if (socket < 0) {
    ERROR(PLUGIN_NAME ": Error connecting to daemon. Errno = %d", errno);
    return CHRONY_RC_FAIL;
  }
  DEBUG(PLUGIN_NAME ": Connected");
  g_chrony_socket = socket;

  if (chrony_set_timeout()) {
    ERROR(PLUGIN_NAME ": Error setting timeout to %llds. Errno = %d",
          (long long)g_chrony_timeout, errno);
    return CHRONY_RC_FAIL;
  }
  return CHRONY_RC_OK;
}

static int chrony_send_request(const tChrony_Request *p_req,
                               size_t p_req_size) {
  if (send(g_chrony_socket, p_req, p_req_size, 0) < 0) {
    ERROR(PLUGIN_NAME ": Error sending packet. Errno = %d", errno);
    return CHRONY_RC_FAIL;
  }
  return CHRONY_RC_OK;
}

static int chrony_recv_response(tChrony_Response *p_resp,
                                size_t p_resp_max_size, size_t *p_resp_size) {
  ssize_t rc = recv(g_chrony_socket, p_resp, p_resp_max_size, 0);
  if (rc <= 0) {
    ERROR(PLUGIN_NAME ": Error receiving packet: %s (%d)", strerror(errno),
          errno);
    return CHRONY_RC_FAIL;
  } else {
    *p_resp_size = rc;
    return CHRONY_RC_OK;
  }
}

static int chrony_query(const int p_command, tChrony_Request *p_req,
                        tChrony_Response *p_resp, size_t *p_resp_size) {
  /* Check connection. We simply perform one try as collectd already handles
   * retries */
  assert(p_req);
  assert(p_resp);
  assert(p_resp_size);

  if (g_chrony_is_connected == 0) {
    if (chrony_connect() == CHRONY_RC_OK) {
      g_chrony_is_connected = 1;
    } else {
      ERROR(PLUGIN_NAME ": Unable to connect. Errno = %d", errno);
      return CHRONY_RC_FAIL;
    }
  }

  do {
    int valid_command = 0;
    size_t req_size = sizeof(p_req->header) + sizeof(p_req->padding);
    size_t resp_size = sizeof(p_resp->header);
    uint16_t resp_code = RPY_NULL;
    switch (p_command) {
    case REQ_TRACKING:
      req_size += sizeof(p_req->body.tracking);
      resp_size += sizeof(p_resp->body.tracking);
      resp_code = RPY_TRACKING;
      valid_command = 1;
      break;
    case REQ_N_SOURCES:
      req_size += sizeof(p_req->body.n_sources);
      resp_size += sizeof(p_resp->body.n_sources);
      resp_code = RPY_N_SOURCES;
      valid_command = 1;
      break;
    case REQ_SOURCE_DATA:
      req_size += sizeof(p_req->body.source_data);
      resp_size += sizeof(p_resp->body.source_data);
      resp_code = RPY_SOURCE_DATA;
      valid_command = 1;
      break;
    case REQ_SOURCE_STATS:
      req_size += sizeof(p_req->body.source_stats);
      resp_size += sizeof(p_resp->body.source_stats);
      resp_code = RPY_SOURCE_STATS;
      valid_command = 1;
      break;
    default:
      ERROR(PLUGIN_NAME ": Unknown request command (Was: %d)", p_command);
      break;
    }

    if (valid_command == 0)
      break;

    uint32_t seq_nr = rand_r(&g_chrony_rand);
    p_req->header.f_cmd = htons(p_command);
    p_req->header.f_cmd_try = 0;
    p_req->header.f_seq = seq_nr;

    DEBUG(PLUGIN_NAME ": Sending request (.cmd = %d, .seq = %d)", p_command,
          seq_nr);
    if (chrony_send_request(p_req, req_size) != 0)
      break;

    DEBUG(PLUGIN_NAME ": Waiting for response");
    if (chrony_recv_response(p_resp, resp_size, p_resp_size) != 0)
      break;

    DEBUG(PLUGIN_NAME ": Received response: .version = %u, .type = %u, .cmd = "
                      "%u, .reply = %u, .status = %u, .seq = %u",
          p_resp->header.f_version, p_resp->header.f_type,
          ntohs(p_resp->header.f_cmd), ntohs(p_resp->header.f_reply),
          ntohs(p_resp->header.f_status), p_resp->header.f_seq);

    if (p_resp->header.f_version != p_req->header.f_version) {
      ERROR(PLUGIN_NAME ": Wrong protocol version (Was: %d, expected: %d)",
            p_resp->header.f_version, p_req->header.f_version);
      return CHRONY_RC_FAIL;
    }
    if (p_resp->header.f_type != PKT_TYPE_CMD_REPLY) {
      ERROR(PLUGIN_NAME ": Wrong packet type (Was: %d, expected: %d)",
            p_resp->header.f_type, PKT_TYPE_CMD_REPLY);
      return CHRONY_RC_FAIL;
    }
    if (p_resp->header.f_seq != seq_nr) {
      /* FIXME: Implement sequence number handling */
      ERROR(PLUGIN_NAME ": Unexpected sequence number (Was: %d, expected: %d)",
            p_resp->header.f_seq, p_req->header.f_seq);
      return CHRONY_RC_FAIL;
    }
    if (p_resp->header.f_cmd != p_req->header.f_cmd) {
      ERROR(PLUGIN_NAME ": Wrong reply command (Was: %d, expected: %d)",
            p_resp->header.f_cmd, p_req->header.f_cmd);
      return CHRONY_RC_FAIL;
    }

    if (ntohs(p_resp->header.f_reply) != resp_code) {
      ERROR(PLUGIN_NAME ": Wrong reply code (Was: %d, expected: %d)",
            ntohs(p_resp->header.f_reply), resp_code);
      return CHRONY_RC_FAIL;
    }

    switch (p_resp->header.f_status) {
    case STT_SUCCESS:
      DEBUG(PLUGIN_NAME ": Reply packet status STT_SUCCESS");
      break;
    default:
      ERROR(PLUGIN_NAME
            ": Reply packet contains error status: %d (expected: %d)",
            p_resp->header.f_status, STT_SUCCESS);
      return CHRONY_RC_FAIL;
    }

    /* Good result */
    return CHRONY_RC_OK;
  } while (0);

  /* Some error occured */
  return CHRONY_RC_FAIL;
}

static void chrony_init_req(tChrony_Request *p_req) {
  memset(p_req, 0, sizeof(*p_req));
  p_req->header.f_version = PROTO_VERSION_NUMBER;
  p_req->header.f_type = PKT_TYPE_CMD_REQUEST;
  p_req->header.f_dummy0 = 0;
  p_req->header.f_dummy1 = 0;
  p_req->header.f_dummy2 = 0;
  p_req->header.f_dummy3 = 0;
}

/* ntohf code originally from:
 * git://git.tuxfamily.org/gitroot/chrony/chrony.git:util.c */
/* Original code licensed as GPLv2, by Richard P. Purnow, Miroslav Lichvar */
/* Original name: double UTI_tFloatNetworkToHost(tFloat f) */
static double ntohf(tFloat p_float) {
/* Convert tFloat in Network-bit-order to double in host-bit-order */

#define FLOAT_EXP_BITS 7
#define FLOAT_EXP_MIN (-(1 << (FLOAT_EXP_BITS - 1)))
#define FLOAT_EXP_MAX (-FLOAT_EXP_MIN - 1)
#define FLOAT_COEF_BITS ((int)sizeof(int32_t) * 8 - FLOAT_EXP_BITS)
#define FLOAT_COEF_MIN (-(1 << (FLOAT_COEF_BITS - 1)))
#define FLOAT_COEF_MAX (-FLOAT_COEF_MIN - 1)

  int32_t exp, coef;
  uint32_t uval;

  uval = ntohl(p_float.value);
  exp = (uval >> FLOAT_COEF_BITS) - FLOAT_COEF_BITS;
  if (exp >= 1 << (FLOAT_EXP_BITS - 1))
    exp -= 1 << FLOAT_EXP_BITS;

  /* coef = (x << FLOAT_EXP_BITS) >> FLOAT_EXP_BITS; */
  coef = uval % (1U << FLOAT_COEF_BITS);
  if (coef >= 1 << (FLOAT_COEF_BITS - 1))
    coef -= 1 << FLOAT_COEF_BITS;

  return coef * pow(2.0, exp);
}

static void chrony_push_data(const char *p_type, const char *p_type_inst,
                             double p_value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = p_value};
  vl.values_len = 1;

  /* XXX: Shall g_chrony_host/g_chrony_port be reflected in the plugin's output?
   */
  sstrncpy(vl.plugin, PLUGIN_NAME_SHORT, sizeof(vl.plugin));
  if (g_chrony_plugin_instance != NULL) {
    sstrncpy(vl.plugin_instance, g_chrony_plugin_instance,
             sizeof(vl.plugin_instance));
  }
  if (p_type != NULL)
    sstrncpy(vl.type, p_type, sizeof(vl.type));

  if (p_type_inst != NULL)
    sstrncpy(vl.type_instance, p_type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void chrony_push_data_valid(const char *p_type, const char *p_type_inst,
                                   const int p_is_valid, double p_value) {
  /* Push real value if p_is_valid is true, push NAN if p_is_valid is not true
   * (idea from ntp plugin) */
  if (p_is_valid == 0)
    p_value = NAN;

  chrony_push_data(p_type, p_type_inst, p_value);
}

static int chrony_init_seq(void) {
  /* Initialize the sequence number generator from /dev/urandom */
  /* Fallbacks: /dev/random and time(NULL) */

  int fh;

  /* Try urandom */
  fh = open(URAND_DEVICE_PATH, O_RDONLY);
  if (fh >= 0) {
    ssize_t rc = read(fh, &g_chrony_rand, sizeof(g_chrony_rand));
    if (rc != sizeof(g_chrony_rand)) {
      ERROR(PLUGIN_NAME ": Reading from random source \'%s\'failed: %s (%d)",
            URAND_DEVICE_PATH, strerror(errno), errno);
      close(fh);
      return CHRONY_RC_FAIL;
    }
    close(fh);
    DEBUG(PLUGIN_NAME ": Seeding RNG from " URAND_DEVICE_PATH);
  } else {
    if (errno == ENOENT) {
      /* URAND_DEVICE_PATH device not found. Try RAND_DEVICE_PATH as fall-back
       */
      fh = open(RAND_DEVICE_PATH, O_RDONLY);
      if (fh >= 0) {
        ssize_t rc = read(fh, &g_chrony_rand, sizeof(g_chrony_rand));
        if (rc != sizeof(g_chrony_rand)) {
          ERROR(PLUGIN_NAME
                ": Reading from random source \'%s\'failed: %s (%d)",
                RAND_DEVICE_PATH, strerror(errno), errno);
          close(fh);
          return CHRONY_RC_FAIL;
        }
        close(fh);
        DEBUG(PLUGIN_NAME ": Seeding RNG from " RAND_DEVICE_PATH);
      } else {
        /* Error opening RAND_DEVICE_PATH. Try time(NULL) as fall-back */
        DEBUG(PLUGIN_NAME ": Seeding RNG from time(NULL)");
        g_chrony_rand = time(NULL) ^ getpid();
      }
    } else {
      ERROR(PLUGIN_NAME ": Opening random source \'%s\' failed: %s (%d)",
            URAND_DEVICE_PATH, strerror(errno), errno);
      return CHRONY_RC_FAIL;
    }
  }

  return CHRONY_RC_OK;
}

/*****************************************************************************/
/* Exported functions */
/*****************************************************************************/
static int chrony_config(const char *p_key, const char *p_value) {
  assert(p_key);
  assert(p_value);

  /* Parse config variables */
  if (strcasecmp(p_key, CONFIG_KEY_HOST) == 0) {
    if (g_chrony_host != NULL)
      free(g_chrony_host);

    if ((g_chrony_host = strdup(p_value)) == NULL) {
      ERROR(PLUGIN_NAME ": Error duplicating host name");
      return CHRONY_RC_FAIL;
    }
  } else {
    if (strcasecmp(p_key, CONFIG_KEY_PORT) == 0) {
      if (g_chrony_port != NULL)
        free(g_chrony_port);

      if ((g_chrony_port = strdup(p_value)) == NULL) {
        ERROR(PLUGIN_NAME ": Error duplicating port name");
        return CHRONY_RC_FAIL;
      }
    } else {
      if (strcasecmp(p_key, CONFIG_KEY_TIMEOUT) == 0) {
        time_t tosec = strtol(p_value, NULL, 0);
        g_chrony_timeout = tosec;
      } else {
        WARNING(PLUGIN_NAME ": Unknown configuration variable: %s %s", p_key,
                p_value);
        return CHRONY_RC_FAIL;
      }
    }
  }
  /* XXX: We could set g_chrony_plugin_instance here to
   * "g_chrony_host-g_chrony_port", but as multiple instances aren't yet
   * supported, we skip this for now */

  return CHRONY_RC_OK;
}

static int chrony_request_daemon_stats(void) {
  /* Perform Tracking request */
  int rc;
  size_t chrony_resp_size;
  tChrony_Request chrony_req;
  tChrony_Response chrony_resp;

  chrony_init_req(&chrony_req);
  rc = chrony_query(REQ_TRACKING, &chrony_req, &chrony_resp, &chrony_resp_size);
  if (rc != 0) {
    ERROR(PLUGIN_NAME ": chrony_query (REQ_TRACKING) failed with status %i",
          rc);
    return rc;
  }
#if COLLECT_DEBUG
  {
    char src_addr[IPV6_STR_MAX_SIZE] = {0};
    niptoha(&chrony_resp.body.tracking.addr, src_addr, sizeof(src_addr));
    DEBUG(PLUGIN_NAME
          ": Daemon stat: .addr = %s, .ref_id= %u, .stratum = %u, .leap_status "
          "= %u, .ref_time = %u:%u:%u, .current_correction = %f, .last_offset "
          "= %f, .rms_offset = %f, .freq_ppm = %f, .skew_ppm = %f, .root_delay "
          "= %f, .root_dispersion = %f, .last_update_interval = %f",
          src_addr, ntohs(chrony_resp.body.tracking.f_ref_id),
          ntohs(chrony_resp.body.tracking.f_stratum),
          ntohs(chrony_resp.body.tracking.f_leap_status),
          ntohl(chrony_resp.body.tracking.f_ref_time.tv_sec_high),
          ntohl(chrony_resp.body.tracking.f_ref_time.tv_sec_low),
          ntohl(chrony_resp.body.tracking.f_ref_time.tv_nsec),
          ntohf(chrony_resp.body.tracking.f_current_correction),
          ntohf(chrony_resp.body.tracking.f_last_offset),
          ntohf(chrony_resp.body.tracking.f_rms_offset),
          ntohf(chrony_resp.body.tracking.f_freq_ppm),
          ntohf(chrony_resp.body.tracking.f_skew_ppm),
          ntohf(chrony_resp.body.tracking.f_root_delay),
          ntohf(chrony_resp.body.tracking.f_root_dispersion),
          ntohf(chrony_resp.body.tracking.f_last_update_interval));
  }
#endif

  double time_ref = ntohl(chrony_resp.body.tracking.f_ref_time.tv_nsec);
  time_ref /= 1000000000.0;
  time_ref += ntohl(chrony_resp.body.tracking.f_ref_time.tv_sec_low);
  if (chrony_resp.body.tracking.f_ref_time.tv_sec_high) {
    double secs_high = ntohl(chrony_resp.body.tracking.f_ref_time.tv_sec_high);
    secs_high *= 4294967296.0;
    time_ref += secs_high;
  }

  /* Forward results to collectd-daemon */
  /* Type_instance is always 'chrony' to tag daemon-wide data */
  /*                Type                Type_instan  Value */
  chrony_push_data("clock_stratum", DAEMON_NAME,
                   ntohs(chrony_resp.body.tracking.f_stratum));
  chrony_push_data("time_ref", DAEMON_NAME, time_ref); /* unit: s */
  chrony_push_data(
      "time_offset_ntp", DAEMON_NAME,
      ntohf(chrony_resp.body.tracking.f_current_correction)); /* Offset between
                                                                 system time and
                                                                 NTP, unit: s */
  chrony_push_data(
      "time_offset", DAEMON_NAME,
      ntohf(
          chrony_resp.body.tracking
              .f_last_offset)); /* Estimated Offset of the NTP time, unit: s */
  chrony_push_data(
      "time_offset_rms", DAEMON_NAME,
      ntohf(chrony_resp.body.tracking
                .f_rms_offset)); /* averaged value of the above, unit: s */
  chrony_push_data(
      "frequency_error", DAEMON_NAME,
      ntohf(chrony_resp.body.tracking
                .f_freq_ppm)); /* Frequency error of the local osc, unit: ppm */
  chrony_push_data("clock_skew_ppm", DAEMON_NAME,
                   ntohf(chrony_resp.body.tracking.f_skew_ppm));
  chrony_push_data(
      "root_delay", DAEMON_NAME,
      ntohf(chrony_resp.body.tracking.f_root_delay)); /* Network latency between
                                                         local daemon and the
                                                         current source */
  chrony_push_data("root_dispersion", DAEMON_NAME,
                   ntohf(chrony_resp.body.tracking.f_root_dispersion));
  chrony_push_data("clock_last_update", DAEMON_NAME,
                   ntohf(chrony_resp.body.tracking.f_last_update_interval));

  return CHRONY_RC_OK;
}

static int chrony_request_sources_count(unsigned int *p_count) {
  /* Requests the number of time sources from the chrony daemon */
  int rc;
  size_t chrony_resp_size;
  tChrony_Request chrony_req;
  tChrony_Response chrony_resp;

  DEBUG(PLUGIN_NAME ": Requesting data");
  chrony_init_req(&chrony_req);
  rc =
      chrony_query(REQ_N_SOURCES, &chrony_req, &chrony_resp, &chrony_resp_size);
  if (rc != 0) {
    ERROR(PLUGIN_NAME ": chrony_query (REQ_N_SOURCES) failed with status %i",
          rc);
    return rc;
  }

  *p_count = ntohl(chrony_resp.body.n_sources.f_n_sources);
  DEBUG(PLUGIN_NAME ": Getting data of %d clock sources", *p_count);

  return CHRONY_RC_OK;
}

static int chrony_request_source_data(int p_src_idx, int *p_is_reachable) {
  /* Perform Source data request for source #p_src_idx */
  int rc;
  size_t chrony_resp_size;
  tChrony_Request chrony_req;
  tChrony_Response chrony_resp;

  char src_addr[IPV6_STR_MAX_SIZE] = {0};

  chrony_init_req(&chrony_req);
  chrony_req.body.source_data.f_index = htonl(p_src_idx);
  rc = chrony_query(REQ_SOURCE_DATA, &chrony_req, &chrony_resp,
                    &chrony_resp_size);
  if (rc != 0) {
    ERROR(PLUGIN_NAME ": chrony_query (REQ_SOURCE_DATA) failed with status %i",
          rc);
    return rc;
  }

  niptoha(&chrony_resp.body.source_data.addr, src_addr, sizeof(src_addr));
  DEBUG(PLUGIN_NAME ": Source[%d] data: .addr = %s, .poll = %u, .stratum = %u, "
                    ".state = %u, .mode = %u, .flags = %u, .reach = %u, "
                    ".latest_meas_ago = %u, .orig_latest_meas = %f, "
                    ".latest_meas = %f, .latest_meas_err = %f",
        p_src_idx, src_addr, ntohs(chrony_resp.body.source_data.f_poll),
        ntohs(chrony_resp.body.source_data.f_stratum),
        ntohs(chrony_resp.body.source_data.f_state),
        ntohs(chrony_resp.body.source_data.f_mode),
        ntohs(chrony_resp.body.source_data.f_flags),
        ntohs(chrony_resp.body.source_data.f_reachability),
        ntohl(chrony_resp.body.source_data.f_since_sample),
        ntohf(chrony_resp.body.source_data.f_origin_latest_meas),
        ntohf(chrony_resp.body.source_data.f_latest_meas),
        ntohf(chrony_resp.body.source_data.f_latest_meas_err));

  /* Push NaN if source is currently not reachable */
  int is_reachable = ntohs(chrony_resp.body.source_data.f_reachability) & 0x01;
  *p_is_reachable = is_reachable;

  /* Forward results to collectd-daemon */
  chrony_push_data_valid("clock_stratum", src_addr, is_reachable,
                         ntohs(chrony_resp.body.source_data.f_stratum));
  chrony_push_data_valid("clock_state", src_addr, is_reachable,
                         ntohs(chrony_resp.body.source_data.f_state));
  chrony_push_data_valid("clock_mode", src_addr, is_reachable,
                         ntohs(chrony_resp.body.source_data.f_mode));
  chrony_push_data_valid("clock_reachability", src_addr, is_reachable,
                         ntohs(chrony_resp.body.source_data.f_reachability));
  chrony_push_data_valid("clock_last_meas", src_addr, is_reachable,
                         ntohs(chrony_resp.body.source_data.f_since_sample));

  return CHRONY_RC_OK;
}

static int chrony_request_source_stats(int p_src_idx,
                                       const int *p_is_reachable) {
  /* Perform Source stats request for source #p_src_idx */
  int rc;
  size_t chrony_resp_size;
  tChrony_Request chrony_req;
  tChrony_Response chrony_resp;
  double skew_ppm, frequency_error, time_offset;

  char src_addr[IPV6_STR_MAX_SIZE] = {0};

  if (*p_is_reachable == 0) {
    skew_ppm = 0;
    frequency_error = 0;
    time_offset = 0;
  } else {
    chrony_init_req(&chrony_req);
    chrony_req.body.source_stats.f_index = htonl(p_src_idx);
    rc = chrony_query(REQ_SOURCE_STATS, &chrony_req, &chrony_resp,
                      &chrony_resp_size);
    if (rc != 0) {
      ERROR(PLUGIN_NAME
            ": chrony_query (REQ_SOURCE_STATS) failed with status %i",
            rc);
      return rc;
    }

    skew_ppm = ntohf(chrony_resp.body.source_stats.f_skew_ppm);
    frequency_error = ntohf(chrony_resp.body.source_stats.f_rtc_gain_rate_ppm);
    time_offset = ntohf(chrony_resp.body.source_stats.f_est_offset);

    niptoha(&chrony_resp.body.source_stats.addr, src_addr, sizeof(src_addr));
    DEBUG(PLUGIN_NAME
          ": Source[%d] stat: .addr = %s, .ref_id= %u, .n_samples = %u, "
          ".n_runs = %u, .span_seconds = %u, .rtc_seconds_fast = %f, "
          ".rtc_gain_rate_ppm = %f, .skew_ppm= %f, .est_offset = %f, "
          ".est_offset_err = %f",
          p_src_idx, src_addr, ntohl(chrony_resp.body.source_stats.f_ref_id),
          ntohl(chrony_resp.body.source_stats.f_n_samples),
          ntohl(chrony_resp.body.source_stats.f_n_runs),
          ntohl(chrony_resp.body.source_stats.f_span_seconds),
          ntohf(chrony_resp.body.source_stats.f_rtc_seconds_fast),
          frequency_error, skew_ppm, time_offset,
          ntohf(chrony_resp.body.source_stats.f_est_offset_err));

  } /* if (*is_reachable) */

  /* Forward results to collectd-daemon */
  chrony_push_data_valid("clock_skew_ppm", src_addr, *p_is_reachable, skew_ppm);
  chrony_push_data_valid("frequency_error", src_addr, *p_is_reachable,
                         frequency_error); /* unit: ppm */
  chrony_push_data_valid("time_offset", src_addr, *p_is_reachable,
                         time_offset); /* unit: s */

  return CHRONY_RC_OK;
}

static int chrony_read(void) {
  /* collectd read callback: Perform data acquisition */
  int rc;
  unsigned int n_sources;

  if (g_chrony_seq_is_initialized == 0) {
    /* Seed RNG for sequence number generation */
    rc = chrony_init_seq();
    if (rc != CHRONY_RC_OK)
      return rc;

    g_chrony_seq_is_initialized = 1;
  }

  /* Get daemon stats */
  rc = chrony_request_daemon_stats();
  if (rc != CHRONY_RC_OK)
    return rc;

  /* Get number of time sources, then check every source for status */
  rc = chrony_request_sources_count(&n_sources);
  if (rc != CHRONY_RC_OK)
    return rc;

  for (unsigned int now_src = 0; now_src < n_sources; ++now_src) {
    int is_reachable;
    rc = chrony_request_source_data(now_src, &is_reachable);
    if (rc != CHRONY_RC_OK)
      return rc;

    rc = chrony_request_source_stats(now_src, &is_reachable);
    if (rc != CHRONY_RC_OK)
      return rc;
  }
  return CHRONY_RC_OK;
}

static int chrony_shutdown(void) {
  /* Collectd shutdown callback: Free mem */
  if (g_chrony_is_connected != 0) {
    close(g_chrony_socket);
    g_chrony_is_connected = 0;
  }
  if (g_chrony_host != NULL)
    sfree(g_chrony_host);

  if (g_chrony_port != NULL)
    sfree(g_chrony_port);

  if (g_chrony_plugin_instance != NULL)
    sfree(g_chrony_plugin_instance);

  return CHRONY_RC_OK;
}

void module_register(void) {
  plugin_register_config(PLUGIN_NAME_SHORT, chrony_config, g_config_keys,
                         g_config_keys_num);
  plugin_register_read(PLUGIN_NAME_SHORT, chrony_read);
  plugin_register_shutdown(PLUGIN_NAME_SHORT, chrony_shutdown);
}
