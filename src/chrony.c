/* chrony plugin for collectd
   (c) 2015 by Claudius M Zingerli, ZSeng
   Internas roughly based on the ntpd plugin
   License: GPL2
*/
/* TODO:
 *	- More robust udp parsing (using offsets instead of structs?)
 *	- Plausibility checks on values received
 *
 *
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

enum
{
	REQ_N_SOURCES    = 14,
	REQ_SOURCE_DATA  = 15,
	REQ_TRACKING     = 33,
	REQ_SOURCE_STATS = 34
} eDaemonRequests;

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
	int32_t value;
} tFloat;

typedef struct ATTRIB_PACKED
{
	uint32_t tv_sec_high;
	uint32_t tv_sec_low;
	uint32_t tv_nsec;
} tTimeval;

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

typedef struct ATTRIB_PACKED
{
	uint8_t f_dummy0[80];
} tChrony_Req_Tracking;

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
	uint16_t dummy; /* FIXME: Strange dummy space. Needed on gcc 4.8.3 on x86_64 */
	int16_t  f_poll;    // 2^f_poll = Time between polls (s)
	uint16_t f_stratum; //Remote clock stratum
	uint16_t f_state;   //0 = RPY_SD_ST_SYNC,    1 = RPY_SD_ST_UNREACH,   2 = RPY_SD_ST_FALSETICKER
       	                    //3 = RPY_SD_ST_JITTERY, 4 = RPY_SD_ST_CANDIDATE, 5 = RPY_SD_ST_OUTLIER
	uint16_t f_mode;    //0 = RPY_SD_MD_CLIENT,  1 = RPY_SD_MD_PEER,      2 = RPY_SD_MD_REF
	uint16_t f_flags;   //unused
	uint16_t f_reachability; //???

	uint32_t f_since_sample; //Time since last sample (s)
	tFloat   f_origin_latest_meas; //
	tFloat   f_latest_meas;        //
	tFloat   f_latest_meas_err;    //
} tChrony_Resp_Source_data;

typedef struct ATTRIB_PACKED
{
	uint32_t f_ref_id;
	tChrony_IPAddr addr;
	uint16_t dummy; /* FIXME: Strange dummy space. Needed on gcc 4.8.3 on x86_64 */
	uint32_t f_n_samples;     //Number of measurements done
	uint32_t f_n_runs;        //How many measurements to come
	uint32_t f_span_seconds;  //For how long we're measuring
	tFloat   f_rtc_seconds_fast;  //???
	tFloat   f_rtc_gain_rate_ppm; //Estimated relative frequency error
	tFloat   f_skew_ppm;          //Clock skew (ppm) (worst case freq est error (skew: peak2peak))
	tFloat   f_est_offset;        //Estimated offset of source
	tFloat   f_est_offset_err;    //Error of estimation
} tChrony_Resp_Source_stats;

typedef struct ATTRIB_PACKED
{
	uint32_t f_ref_id;
	tChrony_IPAddr addr;
	uint16_t dummy; /* FIXME: Strange dummy space. Needed on gcc 4.8.3 on x86_64 */
	uint16_t f_stratum;
	uint16_t f_leap_status;
	tTimeval f_ref_time;
	tFloat   f_current_correction;
	tFloat   f_last_offset;
	tFloat   f_rms_offset;
	tFloat   f_freq_ppm;
	tFloat   f_resid_freq_ppm;
	tFloat   f_skew_ppm;
	tFloat   f_root_delay;
	tFloat   f_root_dispersion;
	tFloat   f_last_update_interval;
} tChrony_Resp_Tracking;

typedef struct ATTRIB_PACKED
{
	struct
	{
		uint8_t  f_version;
		uint8_t  f_type;
		uint8_t  f_dummy0;
		uint8_t  f_dummy1;
		uint16_t f_cmd;
		uint16_t f_cmd_try;
		uint32_t f_seq;

		uint32_t f_dummy2;
		uint32_t f_dummy3;
	} header; /* Packed: 20Bytes */
	union
	{
		tChrony_N_Sources        n_sources;
		tChrony_Req_Source_data  source_data;
		tChrony_Req_Source_stats source_stats;
		tChrony_Req_Tracking     tracking;
	} body;
	uint8_t padding[4+16]; /* Padding to match minimal response size */
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
	} header; /* Packed: 28 Bytes */

	union
	{
		tChrony_N_Sources         n_sources;
		tChrony_Resp_Source_data  source_data;
		tChrony_Resp_Source_stats source_stats;
		tChrony_Resp_Tracking     tracking;
	} body;
	
	uint8_t padding[1024];
} tChrony_Response;

static int g_is_connected = 0;
static int g_chrony_socket = -1;
static time_t g_chrony_timeout = 0;
static char *g_chrony_host = NULL;
static char *g_chrony_port = NULL;
static uint32_t g_chrony_seq = 0;

/*****************************************************************************/
/* Internal functions */
/*****************************************************************************/
/* Code from: http://long.ccaba.upc.edu/long/045Guidelines/eva/ipv6.html#daytimeClient6 */
/*BEGIN*/
static int
connect_client (const char *p_hostname,
                const char *service,
                int         family,
                int         socktype)
{
	struct addrinfo hints, *res, *ressave;
	int n, sockfd;

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = family;
	hints.ai_socktype = socktype;

	n = getaddrinfo(p_hostname, service, &hints, &res);

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
/*Code originally from: git://git.tuxfamily.org/gitroot/chrony/chrony.git:util.c */
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

#ifdef FEAT_IPV6
		inet_ntop(AF_INET6, ip6, p_buf, p_bug_size);
#else
#if 0
/* FIXME: Detect little endian systems */
		snprintf(p_buf, p_buf_size, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
			ip6[0], ip6[1], ip6[2], ip6[3], ip6[4], ip6[5], ip6[6], ip6[7],
			ip6[8], ip6[9], ip6[10], ip6[11], ip6[12], ip6[13], ip6[14], ip6[15]);
#else
		snprintf(p_buf, p_buf_size, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
			ip6[5], ip6[4], ip6[3], ip6[2], ip6[1], ip6[0], ip6[9], ip6[8],
			ip6[7], ip6[6], ip6[5], ip6[4], ip6[3], ip6[2], ip6[1], ip6[0]);
#endif
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
		return 1;
	}
	return 0;
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
		return 1;
	}
	DEBUG("chrony plugin: Connected");
	g_chrony_socket = socket;

	if (chrony_set_timeout())
	{
		ERROR ("chrony plugin: Error setting timeout to %lds. Errno = %d", g_chrony_timeout, errno);
		return 1;
	}
	return 0;
}

static int chrony_send_request(const tChrony_Request *p_req, size_t p_req_size)
{
	if (send(g_chrony_socket,p_req,p_req_size,0) < 0)
	{
		ERROR ("chrony plugin: Error sending packet. Errno = %d", errno);
		return 1;
	} else {
		return 0;
	}
}

static int chrony_recv_response(tChrony_Response *p_resp, size_t p_resp_max_size, size_t *p_resp_size)
{
	ssize_t rc = recv(g_chrony_socket,p_resp,p_resp_max_size,0);
	if (rc <= 0)
	{
		ERROR ("chrony plugin: Error receiving packet. Errno = %d", errno);
		return 1;
	} else {
		*p_resp_size = rc;
		return 0;
	}
}

static int chrony_query(const int p_command, tChrony_Request *p_req, tChrony_Response *p_resp, size_t *p_resp_size)
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
		int valid_command  = 0;
		size_t req_size    = sizeof(p_req->header) + sizeof(p_req->padding);
		size_t resp_size   = sizeof(p_resp->header);
		uint16_t resp_code = RPY_NULL;
		switch (p_command)
		{
		case REQ_TRACKING:
			req_size  += sizeof(p_req->body.tracking);
			resp_size += sizeof(p_resp->body.tracking); 
			resp_code  = RPY_TRACKING;
			valid_command = 1;
		break;
		case REQ_N_SOURCES:
			req_size  += sizeof(p_req->body.n_sources);
			resp_size += sizeof(p_resp->body.n_sources); 
			resp_code  = RPY_N_SOURCES;
			valid_command = 1;
		break;
		case REQ_SOURCE_DATA:
			req_size  += sizeof(p_req->body.source_data);
			resp_size += sizeof(p_resp->body.source_data); 
			resp_code  = RPY_SOURCE_DATA;
			valid_command = 1;
		break;
		case REQ_SOURCE_STATS:
			req_size  += sizeof(p_req->body.source_stats);
			resp_size += sizeof(p_resp->body.source_stats); 
			resp_code  = RPY_SOURCE_STATS;
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
		
		DEBUG("chrony plugin: Sending request (.cmd = %d, .seq = %d)",p_command,g_chrony_seq-1);
		if (chrony_send_request(p_req,req_size) != 0)
		{
			break;
		}

		DEBUG("chrony plugin: Waiting for response");
		if (chrony_recv_response(p_resp,resp_size,p_resp_size) != 0)
		{
			break;
		}
		DEBUG("chrony plugin: Received response: .version = %u, .type = %u, .cmd = %u, .reply = %u, .status = %u, .seq = %u",
				p_resp->header.f_version,p_resp->header.f_type,ntohs(p_resp->header.f_cmd),
				ntohs(p_resp->header.f_reply),ntohs(p_resp->header.f_status),ntohl(p_resp->header.f_seq));

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
			/* FIXME: Implement sequence number handling */
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
			ERROR("chrony plugin: Wrong reply code (Was: %d, expected: %d)", ntohs(p_resp->header.f_reply), resp_code);
			return 1;
		}

		switch (p_resp->header.f_status)
		{
		case STT_SUCCESS:
			DEBUG("chrony plugin: Reply packet status STT_SUCCESS");
			break;
		default:
			ERROR("chrony plugin: Reply packet contains error status: %d (expected: %d)", p_resp->header.f_status, STT_SUCCESS);
			return 1;
		}

		//Good result
		return 0;
	} while (0);
	
	//Some error occured
	return 1;
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

/* Code from: git://git.tuxfamily.org/gitroot/chrony/chrony.git:util.c (GPLv2) */
/*BEGIN*/
#define FLOAT_EXP_BITS 7
#define FLOAT_EXP_MIN (-(1 << (FLOAT_EXP_BITS - 1)))
#define FLOAT_EXP_MAX (-FLOAT_EXP_MIN - 1)
#define FLOAT_COEF_BITS ((int)sizeof (int32_t) * 8 - FLOAT_EXP_BITS)
#define FLOAT_COEF_MIN (-(1 << (FLOAT_COEF_BITS - 1)))
#define FLOAT_COEF_MAX (-FLOAT_COEF_MIN - 1)

/* double UTI_tFloatNetworkToHost(tFloat f) */
double ntohf(tFloat p_float)
{
	int32_t exp, coef;
	uint32_t uval;

	uval = ntohl(p_float.value);
	exp = (uval >> FLOAT_COEF_BITS) - FLOAT_COEF_BITS;
	if (exp >= 1 << (FLOAT_EXP_BITS - 1))
	{
		exp -= 1 << FLOAT_EXP_BITS;
	}

	//coef = (x << FLOAT_EXP_BITS) >> FLOAT_EXP_BITS;
	coef = uval % (1U << FLOAT_COEF_BITS);
	if (coef >= 1 << (FLOAT_COEF_BITS - 1))
	{
		coef -= 1 << FLOAT_COEF_BITS; 
	}
	return coef * pow(2.0, exp);
}
/*END*/

/* Code from: collectd/src/ntpd.c (MIT) */
/*BEGIN*/
static void chrony_push_data(char *type, char *type_inst, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value; //TODO: Check type??? (counter, gauge, derive, absolute)

	vl.values     = values;
	vl.values_len = 1;
	sstrncpy (vl.host,            hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin,          "chrony",   sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "",         sizeof (vl.plugin_instance));
	sstrncpy (vl.type,            type,       sizeof (vl.type));
	sstrncpy (vl.type_instance,   type_inst,  sizeof (vl.type_instance));

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
	/* Parse config variables */
	if (strcasecmp(p_key, "Host") == 0)
	{
		if (g_chrony_host != NULL)
		{
			free (g_chrony_host);
		}
		if ((g_chrony_host = strdup (p_value)) == NULL)
		{
			ERROR ("chrony plugin: Error duplicating host name");
			return 1;
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
			return 1;
		}
	} else if (strcasecmp(p_key, "Timeout") == 0)
	{
		time_t tosec = strtol(p_value,NULL,0);
		g_chrony_timeout = tosec;
	} else {
		WARNING("chrony plugin: Unknown configuration variable: %s %s",p_key,p_value);
		return 1;
	}

	return 0;
}


static int chrony_request_daemon_stats()
{
	//Tracking request
	size_t chrony_resp_size;
	tChrony_Request  chrony_req;
	tChrony_Response chrony_resp;
	char src_addr[IPV6_STR_MAX_SIZE];

	chrony_init_req(&chrony_req);
	int rc = chrony_query(REQ_TRACKING, &chrony_req, &chrony_resp, &chrony_resp_size);
	if (rc != 0)
	{
		ERROR ("chrony plugin: chrony_query (REQ_TRACKING) failed with status %i", rc);
		return rc;
	}
	
	memset(src_addr, 0, sizeof(src_addr));
	niptoha(&chrony_resp.body.tracking.addr, src_addr, sizeof(src_addr));
	DEBUG("chrony plugin: Daemon stat: .addr = %s, .ref_id= %u, .stratum = %u, .leap_status = %u, .ref_time = %u, .current_correction = %f, .last_offset = %f, .rms_offset = %f, .freq_ppm = %f, .skew_ppm = %f, .root_delay = %f, .root_dispersion = %f, .last_update_interval = %f",
		src_addr,
		ntohs(chrony_resp.body.tracking.f_ref_id), //FIXME: 16bit
		ntohs(chrony_resp.body.tracking.f_stratum),
		ntohs(chrony_resp.body.tracking.f_leap_status),
		ntohl(chrony_resp.body.tracking.f_ref_time.tv_sec_high), //tTimeval
		ntohf(chrony_resp.body.tracking.f_current_correction),
		ntohf(chrony_resp.body.tracking.f_last_offset),
		ntohf(chrony_resp.body.tracking.f_rms_offset),
		ntohf(chrony_resp.body.tracking.f_freq_ppm),
		ntohf(chrony_resp.body.tracking.f_skew_ppm),
		ntohf(chrony_resp.body.tracking.f_root_delay),
		ntohf(chrony_resp.body.tracking.f_root_dispersion),
		ntohf(chrony_resp.body.tracking.f_last_update_interval)
	);
#if 0
	chrony_push_data("clock_skew_ppm",    src_addr,ntohf(chrony_resp.body.source_stats.f_skew_ppm));
	chrony_push_data("frequency_error",   src_addr,ntohf(chrony_resp.body.source_stats.f_rtc_gain_rate_ppm)); /* unit: ppm */
	chrony_push_data("time_offset",       src_addr,ntohf(chrony_resp.body.source_stats.f_est_offset)); /* unit: s */
#endif
	return 0;
}


static int chrony_request_sources_count(unsigned int *p_count)
{
	int rc;
	size_t chrony_resp_size;
	tChrony_Request  chrony_req;
	tChrony_Response chrony_resp;

	DEBUG("chrony plugin: Requesting data");
	chrony_init_req(&chrony_req);
	rc = chrony_query (REQ_N_SOURCES, &chrony_req, &chrony_resp, &chrony_resp_size);
        if (rc != 0)
        {
                ERROR ("chrony plugin: chrony_query (REQ_N_SOURCES) failed with status %i", rc);
                return rc;
        }
	
	*p_count = ntohl(chrony_resp.body.n_sources.f_n_sources);
	DEBUG("chrony plugin: Getting data of %d clock sources", *p_count);

	return 0;
}


static int chrony_request_source_data(int p_src_idx)
{
	//Source data request
	size_t chrony_resp_size;
	tChrony_Request  chrony_req;
	tChrony_Response chrony_resp;
	char src_addr[IPV6_STR_MAX_SIZE];

	chrony_init_req(&chrony_req);
	chrony_req.body.source_data.f_index  = htonl(p_src_idx);
	int rc = chrony_query(REQ_SOURCE_DATA, &chrony_req, &chrony_resp, &chrony_resp_size);
	if (rc != 0)
	{
		ERROR ("chrony plugin: chrony_query (REQ_SOURCE_DATA) failed with status %i", rc);
		return rc;
	}
	memset(src_addr, 0, sizeof(src_addr));
	niptoha(&chrony_resp.body.source_data.addr, src_addr, sizeof(src_addr));
	DEBUG("chrony plugin: Source[%d] data: .addr = %s, .poll = %u, .stratum = %u, .state = %u, .mode = %u, .flags = %u, .reach = %u, .latest_meas_ago = %u, .orig_latest_meas = %f, .latest_meas = %f, .latest_meas_err = %f",
		p_src_idx,
		src_addr,
		ntohs(chrony_resp.body.source_data.f_poll),
		ntohs(chrony_resp.body.source_data.f_stratum),
		ntohs(chrony_resp.body.source_data.f_state),
		ntohs(chrony_resp.body.source_data.f_mode),
		ntohs(chrony_resp.body.source_data.f_flags),
		ntohs(chrony_resp.body.source_data.f_reachability),
		ntohl(chrony_resp.body.source_data.f_since_sample),
		ntohf(chrony_resp.body.source_data.f_origin_latest_meas),
		ntohf(chrony_resp.body.source_data.f_latest_meas),
		ntohf(chrony_resp.body.source_data.f_latest_meas_err)
	);
	chrony_push_data("clock_stratum",     src_addr,ntohs(chrony_resp.body.source_data.f_stratum));
	chrony_push_data("clock_state",       src_addr,ntohs(chrony_resp.body.source_data.f_state));
	chrony_push_data("clock_mode",        src_addr,ntohs(chrony_resp.body.source_data.f_mode));
	chrony_push_data("clock_reachability",src_addr,ntohs(chrony_resp.body.source_data.f_reachability));
	chrony_push_data("clock_last_meas",   src_addr,ntohs(chrony_resp.body.source_data.f_since_sample));

	return 0;
}


static int chrony_request_source_stats(int p_src_idx)
{
	//Source stats request
	size_t chrony_resp_size;
	tChrony_Request  chrony_req;
	tChrony_Response chrony_resp;
	char src_addr[IPV6_STR_MAX_SIZE];

	chrony_init_req(&chrony_req);
	chrony_req.body.source_stats.f_index = htonl(p_src_idx);
	int rc = chrony_query(REQ_SOURCE_STATS, &chrony_req, &chrony_resp, &chrony_resp_size);
	if (rc != 0)
	{
		ERROR ("chrony plugin: chrony_query (REQ_SOURCE_STATS) failed with status %i", rc);
		return rc;
	}

	memset(src_addr, 0, sizeof(src_addr));
	niptoha(&chrony_resp.body.source_stats.addr, src_addr, sizeof(src_addr));
	DEBUG("chrony plugin: Source[%d] stat: .addr = %s, .ref_id= %u, .n_samples = %u, .n_runs = %u, .span_seconds = %u, .rtc_seconds_fast = %f, .rtc_gain_rate_ppm = %f, .skew_ppm= %f, .est_offset = %f, .est_offset_err = %f",
		p_src_idx,
		src_addr,
		ntohl(chrony_resp.body.source_stats.f_ref_id),
		ntohl(chrony_resp.body.source_stats.f_n_samples),
		ntohl(chrony_resp.body.source_stats.f_n_runs),
		ntohl(chrony_resp.body.source_stats.f_span_seconds),
		ntohf(chrony_resp.body.source_stats.f_rtc_seconds_fast),
		ntohf(chrony_resp.body.source_stats.f_rtc_gain_rate_ppm),
		ntohf(chrony_resp.body.source_stats.f_skew_ppm),
		ntohf(chrony_resp.body.source_stats.f_est_offset),
		ntohf(chrony_resp.body.source_stats.f_est_offset_err)
	);
	chrony_push_data("clock_skew_ppm",    src_addr,ntohf(chrony_resp.body.source_stats.f_skew_ppm));
	chrony_push_data("frequency_error",   src_addr,ntohf(chrony_resp.body.source_stats.f_rtc_gain_rate_ppm)); /* unit: ppm */
	chrony_push_data("time_offset",       src_addr,ntohf(chrony_resp.body.source_stats.f_est_offset)); /* unit: s */
	return 0;
}


static int chrony_read()
{
	int  rc;

	//Get daemon stats
	rc = chrony_request_daemon_stats();
	if (rc != 0)
	{
		return rc;
	}

	//Get number of time sources, then check every source for status
	unsigned int now_src, n_sources;
       	rc = chrony_request_sources_count(&n_sources);
	if (rc != 0)
	{
		return rc;
	}

	for (now_src = 0; now_src < n_sources; ++now_src)
	{
		rc = chrony_request_source_data(now_src);
		if (rc != 0)
		{
			return rc;
		}

		rc = chrony_request_source_stats(now_src);
		if (rc != 0)
		{
			return rc;
		}
	}
	return 0;
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
	return 0;
}


void module_register (void)
{
	plugin_register_config(  "chrony", chrony_config, g_config_keys, g_config_keys_num);
        plugin_register_read(    "chrony", chrony_read);
	plugin_register_shutdown("chrony", chrony_shutdown);
}

