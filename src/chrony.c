/* chrony plugin for collectd
   (c) 2015 by Claudius M Zingerli, ZSeng
   Internas roughly based on the ntpd plugin
   License: GPL2
*/

/* getaddrinfo */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "collectd.h"
#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */

static const char *g_config_keys[] =
{
        "Host",
        "Port",
	"Timeout"
};

static int g_config_keys_num = STATIC_ARRAY_SIZE (g_config_keys);

# define CHRONY_DEFAULT_HOST "localhost"
# define CHRONY_DEFAULT_PORT "323"
# define CHRONY_DEFAULT_TIMEOUT 2

/* Copied from chrony/candm.h */
/*BEGIN*/
#define PROTO_VERSION_NUMBER 6

#define REQ_N_SOURCES 14
#define REQ_SOURCE_DATA 15
#define REQ_SOURCE_STATS 34

#define PKT_TYPE_CMD_REQUEST 1
#define PKT_TYPE_CMD_REPLY 2

#define RPY_NULL 1
#define RPY_N_SOURCES 2
#define RPY_SOURCE_DATA 3
#define RPY_MANUAL_TIMESTAMP 4
#define RPY_TRACKING 5
#define RPY_SOURCE_STATS 6
#define RPY_RTC 7

#define IPADDR_UNSPEC 0
#define IPADDR_INET4 1
#define IPADDR_INET6 2

#define ATTRIB_PACKED __attribute__((packed))
typedef struct ATTRIB_PACKED
{
	int32_t f;
} Float;
/*END*/

typedef enum
{
	STT_SUCCESS = 0,
	STT_FAILED  = 1,
	STT_UNAUTH  = 2,
	STT_INVALID = 3,
	STT_NOSUCHSOURCE = 4,
	STT_INVALIDTS  = 5,
	STT_NOTENABLED = 6,
	STT_BADSUBNET  = 7,
	STT_ACCESSALLOWED = 8,
	STT_ACCESSDENIED  = 9,
	STT_NOHOSTACCESS  = 10,
	STT_SOURCEALREADYKNOWN = 11,
	STT_TOOMANYSOURCES = 12,
	STT_NORTC      = 13,
	STT_BADRTCFILE = 14,
	STT_INACTIVE   = 15,
	STT_BADSAMPLE  = 16,
	STT_INVALIDAF  = 17,
	STT_BADPKTVERSION = 18,
	STT_BADPKTLENGTH = 19,
} eChrony_Status;

static int g_is_connected = 0;
static int g_chrony_socket = -1;
static time_t g_chrony_timeout = 0;
static char *g_chrony_host = NULL;
static char *g_chrony_port = NULL;
static uint32_t g_chrony_seq = 0;
//static char  ntpd_port[16];

typedef struct ATTRIB_PACKED
{
	uint32_t f_n_sources;
} tChrony_N_Sources;

typedef struct ATTRIB_PACKED
{
	int32_t f_index;
	uint8_t f_dummy0[44];
} tChrony_Req_Source_data;

typedef struct ATTRIB_PACKED
{
	int32_t f_index;
	uint8_t f_dummy0[56];
} tChrony_Req_Source_stats;

#define IPV6_STR_MAX_SIZE 40
typedef struct ATTRIB_PACKED
{
	union
	{
		uint32_t ip4;
		uint8_t ip6[16];
	} addr;
	uint16_t f_family;
} tChrony_IPAddr;

typedef struct ATTRIB_PACKED
{
	tChrony_IPAddr addr;
	int16_t  f_poll;
	uint16_t f_stratum;
	uint16_t f_state;
	uint16_t f_mode;
	uint16_t f_flags;
	uint16_t dummy; //FIXME: Strange dummy space. Needed on gcc 4.8.3 on x86_64
	uint16_t f_reachability;

	uint32_t f_since_sample;
	Float f_origin_latest_meas;
	Float f_latest_meas;
	Float f_latest_meas_err;
} tChrony_Resp_Source_data;

typedef struct ATTRIB_PACKED
{
	uint32_t f_ref_id;
	tChrony_IPAddr addr;
	uint16_t dummy; //FIXME: Strange dummy space. Needed on gcc 4.8.3 on x86_64
	uint32_t f_n_samples; //Number of measurements done
	uint32_t f_n_runs; //How many measurements to come
	uint32_t f_span_seconds; //For how long we're measuring
	Float f_rtc_seconds_fast;
	Float f_rtc_gain_rate_ppm; //Estimated relative frequency error
	Float f_skew_ppm; //Clock skew
	Float f_est_offset; //Estimated offset of source
	Float f_est_offset_err; //Error of estimation
} tChrony_Resp_Source_stats;


typedef struct ATTRIB_PACKED
{
	struct
	{
		uint8_t f_version;
		uint8_t f_type;
		uint8_t f_dummy0;
		uint8_t f_dummy1;
		uint16_t f_cmd;
		uint16_t f_cmd_try;
		uint32_t f_seq;

		uint32_t f_dummy2;
		uint32_t f_dummy3;
	} header; //Packed: 20Bytes
	union
	{
		tChrony_N_Sources n_sources; //Packed: 8 Bytes
		tChrony_Req_Source_data source_data;
		tChrony_Req_Source_stats source_stats;
	} body;
	uint8_t padding[4+16]; //Padding to match minimal response size
} tChrony_Request;

typedef struct ATTRIB_PACKED
{
	struct
	{
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
	} header; //Packed: 24 Bytes

	/*uint32_t EOR;*/

	union
	{
		tChrony_N_Sources n_sources;
		tChrony_Resp_Source_data source_data;
		tChrony_Resp_Source_stats source_stats;
	} body;
	
	uint8_t padding[1024];
} tChrony_Response;

/*****************************************************************************/
/* Internal functions */
/*****************************************************************************/
/* Code from: http://long.ccaba.upc.edu/long/045Guidelines/eva/ipv6.html#daytimeClient6 */
/*BEGIN*/
static int
connect_client (const char *hostname,
                const char *service,
                int         family,
                int         socktype)
{
	struct addrinfo hints, *res, *ressave;
	int n, sockfd;

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = family;
	hints.ai_socktype = socktype;

	n = getaddrinfo(hostname, service, &hints, &res);

	if (n <0)
	{
		ERROR ("chrony plugin: getaddrinfo error:: [%s]", gai_strerror(n));
		return -1;
	}

	ressave = res;

	sockfd=-1;
	while (res)
	{
		sockfd = socket(res->ai_family,
		res->ai_socktype,
		res->ai_protocol);

		if (!(sockfd < 0))
		{
			if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0)
			{
				break;
			}

			close(sockfd);
			sockfd=-1;
		}
		res=res->ai_next;
	}

	freeaddrinfo(ressave);
	return sockfd;
}
/*Code originally from: https://github.com/mlichvar/chrony/blob/master/util.c */
/*char * UTI_IPToString(IPAddr *addr)*/
char * niptoha(const tChrony_IPAddr *addr,char *p_buf, size_t p_buf_size)
{
	unsigned long a, b, c, d, ip;
	const uint8_t *ip6;

	switch (ntohs(addr->f_family))
	{
	case IPADDR_UNSPEC:
		snprintf(p_buf, p_buf_size, "[UNSPEC]");
	break;
	case IPADDR_INET4:
		ip = ntohl(addr->addr.ip4);
		a = (ip>>24) & 0xff;
		b = (ip>>16) & 0xff;
		c = (ip>> 8) & 0xff;
		d = (ip>> 0) & 0xff;
		snprintf(p_buf, p_buf_size, "%ld.%ld.%ld.%ld", a, b, c, d);
	break;
	case IPADDR_INET6:
		ip6 = addr->addr.ip6;
#if 0
//FIXME: Detect little endian systems
		snprintf(p_buf, p_buf_size, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
			ip6[0], ip6[1], ip6[2], ip6[3], ip6[4], ip6[5], ip6[6], ip6[7],
			ip6[8], ip6[9], ip6[10], ip6[11], ip6[12], ip6[13], ip6[14], ip6[15]);
#else
		snprintf(p_buf, p_buf_size, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
			ip6[5], ip6[4], ip6[3], ip6[2], ip6[1], ip6[0], ip6[9], ip6[8],
			ip6[7], ip6[6], ip6[5], ip6[4], ip6[3], ip6[2], ip6[1], ip6[0]);
#endif
	break;
	default:
		snprintf(p_buf, p_buf_size, "[UNKNOWN]");
	}
	return p_buf;
}
/*END*/

static int chrony_set_timeout()
{
	struct timeval tv;
	tv.tv_sec  = g_chrony_timeout;
	tv.tv_usec = 0;

	assert(g_chrony_socket>=0);
	if (setsockopt(g_chrony_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval)) < 0)
	{
		return (1);
	}
	return (0);
}

static int chrony_connect()
{
	if (g_chrony_host == NULL)
	{
		g_chrony_host = strdup(CHRONY_DEFAULT_HOST);
	}
	if (g_chrony_port == NULL)
	{
		g_chrony_port = strdup(CHRONY_DEFAULT_PORT);
	}
	if (g_chrony_timeout <= 0)
	{
		g_chrony_timeout = CHRONY_DEFAULT_TIMEOUT;
	}
	

	DEBUG("chrony plugin: Connecting to %s:%s", g_chrony_host, g_chrony_port);
	int socket = connect_client(g_chrony_host, g_chrony_port,  AF_UNSPEC, SOCK_DGRAM);
	if (socket < 0)
	{
		ERROR ("chrony plugin: Error connecting to daemon. Errno = %d", errno);
		return (1);
	}
	//TODO: Set timeouts!
	DEBUG("chrony plugin: Connected");
	g_chrony_socket = socket;

	if (chrony_set_timeout())
	{
		ERROR ("chrony plugin: Error setting timeout to %lds. Errno = %d", g_chrony_timeout, errno);
		return (1);
	}
	return (0);
}

static int chrony_send_request(const tChrony_Request *p_req, size_t p_req_size)
{
	if (send(g_chrony_socket,p_req,p_req_size,0) < 0)
	{
		ERROR ("chrony plugin: Error sending packet. Errno = %d", errno);
		return (1);
	} else {
		return (0);
	}
}

static int chrony_recv_response(tChrony_Response *p_resp, size_t p_resp_max_size, size_t *p_resp_size)
{
	ssize_t rc = recv(g_chrony_socket,p_resp,p_resp_max_size,0);
	if (rc <= 0)
	{
		ERROR ("chrony plugin: Error receiving packet. Errno = %d", errno);
		return (1);
	} else {
#if 0
		if (rc < p_resp_max_size)
		{
			ERROR ("chrony plugin: Received too small response packet. (Should: %ld, was: %ld)", p_resp_max_size, rc);
			return (1);
		}
#endif
		*p_resp_size = rc;
		return (0);
	}
}

static int chrony_query(int p_command, tChrony_Request *p_req, tChrony_Response *p_resp, size_t *p_resp_size)
{
	/* Check connection. We simply perform one try as collectd already handles retries */
	assert(p_req);
	assert(p_resp);
	assert(p_resp_size);
	if (g_is_connected == 0)
	{
		if (chrony_connect() == 0)
		{
			g_is_connected = 1;
		} else {
			ERROR ("chrony plugin: Unable to connect. Errno = %d", errno);
			return 1;
		}
	}


	do
	{
		int valid_command = 0;
		size_t req_size = sizeof(p_req->header) + sizeof(p_req->padding);
		size_t resp_size = sizeof(p_resp->header);
		uint16_t resp_code = RPY_NULL;
		switch (p_command)
		{
		case REQ_N_SOURCES:
			req_size  += sizeof(p_req->body.n_sources);
			resp_size += sizeof(p_resp->body.n_sources); 
			resp_code = RPY_N_SOURCES;
			valid_command = 1;
			break;
		case REQ_SOURCE_DATA:
			req_size  += sizeof(p_req->body.source_data);
			resp_size += sizeof(p_resp->body.source_data); 
			resp_code = RPY_SOURCE_DATA;
			valid_command = 1;
			break;
		case REQ_SOURCE_STATS:
			req_size  += sizeof(p_req->body.source_stats);
			resp_size += sizeof(p_resp->body.source_stats); 
			resp_code = RPY_SOURCE_STATS;
			valid_command = 1;
			break;
		default:
			ERROR ("chrony plugin: Unknown request command (Was: %d)", p_command);
			break;
		}

		if (valid_command == 0)
		{
			break;
		}

		p_req->header.f_cmd     = htons(p_command);
		p_req->header.f_cmd_try = 0;
		p_req->header.f_seq     = htonl(g_chrony_seq++);
		
		DEBUG("chrony plugin: Sending request");
		if (chrony_send_request(p_req,req_size) != 0)
		{
			break;
		}

		DEBUG("chrony plugin: Waiting for response");
		if (chrony_recv_response(p_resp,resp_size,p_resp_size) != 0)
		{
			break;
		}
		DEBUG("chrony plugin: Received response: .version = %u, .type = %u, .cmd = %u, .reply = %u, .status = %u, .seq = %u",p_resp->header.f_version,p_resp->header.f_type,ntohs(p_resp->header.f_cmd),ntohs(p_resp->header.f_reply),ntohs(p_resp->header.f_status),ntohl(p_resp->header.f_seq));

		if (p_resp->header.f_version != p_req->header.f_version)
		{
			ERROR("chrony plugin: Wrong protocol version (Was: %d, expected: %d)", p_resp->header.f_version, p_req->header.f_version);
			return 1;
		}
		if (p_resp->header.f_type != PKT_TYPE_CMD_REPLY)
		{
			ERROR("chrony plugin: Wrong packet type (Was: %d, expected: %d)", p_resp->header.f_type, PKT_TYPE_CMD_REPLY);
			return 1;
		}
		if (p_resp->header.f_seq != p_req->header.f_seq)
		{
			//FIXME: Implement sequence number handling
			ERROR("chrony plugin: Unexpected sequence number (Was: %d, expected: %d)", p_resp->header.f_seq, p_req->header.f_seq);
			return 1;
		}
		if (p_resp->header.f_cmd != p_req->header.f_cmd)
		{
			ERROR("chrony plugin: Wrong reply command (Was: %d, expected: %d)", p_resp->header.f_cmd, p_req->header.f_cmd);
			return 1;
		}

		if (ntohs(p_resp->header.f_reply) !=  resp_code)
		{
			ERROR("chrony plugin: Wrong reply code (Was: %d, expected: %d)", ntohs(p_resp->header.f_reply), p_command);
			return 1;
		}

		switch (p_resp->header.f_status)
		{
		case STT_SUCCESS:
			DEBUG("chrony plugin: Reply packet status STT_SUCCESS");
			break;
		default:
			ERROR("chrony plugin: Reply packet contains error status: %d (expected: %d)", p_resp->header.f_status, STT_SUCCESS);
			return (1);
		}
		return (0);
	} while (0);
	
	return (1);
}

static void chrony_init_req(tChrony_Request *p_req)
{
	DEBUG("chrony plugin: Clearing %ld bytes",sizeof(*p_req));
	memset(p_req,0,sizeof(*p_req));
	p_req->header.f_version = PROTO_VERSION_NUMBER;
	p_req->header.f_type    = PKT_TYPE_CMD_REQUEST;
	p_req->header.f_dummy0  = 0;
	p_req->header.f_dummy1  = 0;
	p_req->header.f_dummy2  = 0;
	p_req->header.f_dummy3  = 0;
}

//Code from: https://github.com/mlichvar/chrony/blob/master/util.c (GPLv2)
/*BEGIN*/
#define FLOAT_EXP_BITS 7
#define FLOAT_EXP_MIN (-(1 << (FLOAT_EXP_BITS - 1)))
#define FLOAT_EXP_MAX (-FLOAT_EXP_MIN - 1)
#define FLOAT_COEF_BITS ((int)sizeof (int32_t) * 8 - FLOAT_EXP_BITS)
#define FLOAT_COEF_MIN (-(1 << (FLOAT_COEF_BITS - 1)))
#define FLOAT_COEF_MAX (-FLOAT_COEF_MIN - 1)

//double UTI_FloatNetworkToHost(Float f)
double ntohf(Float f)
{
  int32_t exp, coef, x;

  x = ntohl(f.f);
  exp = (x >> FLOAT_COEF_BITS) - FLOAT_COEF_BITS;
  coef = x << FLOAT_EXP_BITS >> FLOAT_EXP_BITS;
  return coef * pow(2.0, exp);
}
/*END*/

/* Code from: collectd/src/ntpd.c (MIT) */
/*BEGIN*/
static void chrony_push_data(char *type, char *type_inst, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "chrony", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}
/*END*/

/*****************************************************************************/
/* Exported functions */
/*****************************************************************************/
static int chrony_config(const char *p_key, const char *p_value)
{
	assert(p_key);
	assert(p_value);
	//Parse config variables
	if (strcasecmp(p_key, "Host") == 0)
	{
		if (g_chrony_host != NULL)
		{
			free (g_chrony_host);
		}
		if ((g_chrony_host = strdup (p_value)) == NULL)
		{
			ERROR ("chrony plugin: Error duplicating host name");
			return (1);
		}
	} else if (strcasecmp(p_key, "Port") == 0)
	{
		if (g_chrony_port != NULL)
		{
			free (g_chrony_port);
		}
		if ((g_chrony_port = strdup (p_value)) == NULL)
		{
			ERROR ("chrony plugin: Error duplicating port name");
			return (1);
		}
	} else if (strcasecmp(p_key, "Timeout") == 0)
	{
		time_t tosec = strtol(p_value,NULL,0);
		g_chrony_timeout = tosec;
	} else {
		WARNING("chrony plugin: Unknown configuration variable: %s %s",p_key,p_value);
		return (1);
	}
	return (0);
}

static int chrony_read (void)
{
	//plugin_dispatch_values (&vl);
	int status,now_src;
	char ip_addr_str0[ IPV6_STR_MAX_SIZE];
	char ip_addr_str1[IPV6_STR_MAX_SIZE];
	
	size_t chrony_resp_size;
	tChrony_Request  chrony_req,chrony_req1;
	tChrony_Response chrony_resp,chrony_resp1;

	DEBUG("chrony plugin: Requesting data");
	chrony_init_req(&chrony_req);
	status = chrony_query (REQ_N_SOURCES, &chrony_req, &chrony_resp, &chrony_resp_size);
        if (status != 0)
        {
                ERROR ("chrony plugin: chrony_query (REQ_N_SOURCES) failed with status %i", status);
                return (status);
        }
	
	int n_sources = ntohl(chrony_resp.body.n_sources.f_n_sources);
	DEBUG("chrony plugin: Getting data of %d clock sources", n_sources);

	for (now_src = 0; now_src < n_sources; ++now_src)
	{
		chrony_init_req(&chrony_req);
		chrony_init_req(&chrony_req1);
		chrony_req.body.source_data.f_index = htonl(now_src);
		chrony_req1.body.source_stats.f_index = htonl(now_src);
		status = chrony_query(REQ_SOURCE_DATA, &chrony_req, &chrony_resp, &chrony_resp_size);
		if (status != 0)
		{
			ERROR ("chrony plugin: chrony_query (REQ_SOURCE_DATA) failed with status %i", status);
			return (status);
		}
		status = chrony_query(REQ_SOURCE_STATS, &chrony_req1, &chrony_resp1, &chrony_resp_size);
		if (status != 0)
		{
			ERROR ("chrony plugin: chrony_query (REQ_SOURCE_STATS) failed with status %i", status);
			return (status);
		}
		memset(ip_addr_str0,0,sizeof(ip_addr_str0));
		niptoha(&chrony_resp.body.source_data.addr,ip_addr_str0,sizeof(ip_addr_str0));
		DEBUG("chrony plugin: Source[%d] data: .addr = %s, .poll = %u, .stratum = %u, .state = %u, .mode = %u, .flags = %u, .reach = %u, .latest_meas_ago = %u, .orig_latest_meas = %f, .latest_meas = %f, .latest_meas_err = %f",
			now_src,
			ip_addr_str0,
			ntohs(chrony_resp.body.source_data.f_poll),
			ntohs(chrony_resp.body.source_data.f_stratum),
			ntohs(chrony_resp.body.source_data.f_state),
			ntohs(chrony_resp.body.source_data.f_mode),
			ntohs(chrony_resp.body.source_data.f_flags),
			ntohs(chrony_resp.body.source_data.f_reachability),
			ntohl(chrony_resp.body.source_data.f_since_sample),
			ntohf(chrony_resp.body.source_data.f_origin_latest_meas),
			ntohf(chrony_resp.body.source_data.f_latest_meas),
			ntohf(chrony_resp.body.source_data.f_latest_meas_err));
#if 1
		memset(ip_addr_str1,0,sizeof(ip_addr_str1));
		niptoha(&chrony_resp1.body.source_stats.addr,ip_addr_str1,sizeof(ip_addr_str1));
		DEBUG("chrony plugin: Source[%d] stat: .addr = %s, .ref_id= %u, .n_samples = %u, .n_runs = %u, .span_seconds = %u, .rtc_seconds_fast = %f, .rtc_gain_rate_ppm = %f, .skew_ppm= %f, .est_offset = %f, .est_offset_err = %f",
			now_src,
			ip_addr_str1,
			ntohl(chrony_resp1.body.source_stats.f_ref_id),
			ntohl(chrony_resp1.body.source_stats.f_n_samples),
			ntohl(chrony_resp1.body.source_stats.f_n_runs),
			ntohl(chrony_resp1.body.source_stats.f_span_seconds),
			ntohf(chrony_resp1.body.source_stats.f_rtc_seconds_fast),
			ntohf(chrony_resp1.body.source_stats.f_rtc_gain_rate_ppm),
			ntohf(chrony_resp1.body.source_stats.f_skew_ppm),
			ntohf(chrony_resp1.body.source_stats.f_est_offset),
			ntohf(chrony_resp1.body.source_stats.f_est_offset_err));
#endif
#if 1
		chrony_push_data("clock_stratum",     ip_addr_str0,ntohs(chrony_resp.body.source_data.f_stratum));
		chrony_push_data("clock_state",       ip_addr_str0,ntohs(chrony_resp.body.source_data.f_state));
		chrony_push_data("clock_mode",        ip_addr_str0,ntohs(chrony_resp.body.source_data.f_mode));
		chrony_push_data("clock_reachability",ip_addr_str0,ntohs(chrony_resp.body.source_data.f_reachability));
		chrony_push_data("clock_last_meas",   ip_addr_str0,ntohs(chrony_resp.body.source_data.f_since_sample));
		chrony_push_data("clock_skew_ppm",    ip_addr_str1,ntohf(chrony_resp1.body.source_stats.f_skew_ppm));
		chrony_push_data("frequency_error",   ip_addr_str1,ntohf(chrony_resp1.body.source_stats.f_rtc_gain_rate_ppm)); //unit: ppm
		chrony_push_data("time_offset",       ip_addr_str1,ntohf(chrony_resp1.body.source_stats.f_est_offset)); //unit: s
#endif
	}
	return (0);
}

static int chrony_shutdown()
{
	if (g_is_connected != 0)
	{
		close(g_chrony_socket);
		g_is_connected = 0;
	}
	if (g_chrony_host != NULL)
	{
		free (g_chrony_host);
		g_chrony_host = NULL;
	}
	if (g_chrony_port != NULL)
	{
		free (g_chrony_port);
		g_chrony_port = NULL;
	}
	return (0);
}

void module_register (void)
{
	plugin_register_config ("chrony", chrony_config, g_config_keys, g_config_keys_num);
        plugin_register_read ("chrony", chrony_read);
	plugin_register_shutdown ("chrony", chrony_shutdown);
}
