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
};

static int g_config_keys_num = STATIC_ARRAY_SIZE (g_config_keys);

# define CHRONY_DEFAULT_HOST "localhost"
# define CHRONY_DEFAULT_PORT "323"

/* Copied from chrony/candm.h */
#define PROTO_VERSION_NUMBER 6

#define REQ_N_SOURCES 14
#define REQ_SOURCE_DATA 15

#define PKT_TYPE_CMD_REQUEST 1
#define PKT_TYPE_CMD_REPLY 2


static int g_is_connected = 0;
static int g_chrony_socket = -1;
static char *g_chrony_host = NULL;
static char *g_chrony_port = NULL;
static uint32_t g_chrony_seq = 0;
//static char  ntpd_port[16];

typedef struct
{
	uint32_t f_n_sources;
	int32_t EOR;
} tChrony_Req_N_Sources;

typedef struct
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
	} header;
	union
	{
		tChrony_Req_N_Sources n_sources;
	} body;
} tChrony_Request;

typedef struct
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
		uint16_t f_dummy5;
		uint16_t f_dummy6;
	} header;

	union
	{
	} data;
} tChrony_Response;

/*****************************************************************************/
/* Internal functions */
/*****************************************************************************/
/* Code from: http://long.ccaba.upc.edu/long/045Guidelines/eva/ipv6.html#daytimeClient6 */
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

static int chrony_connect()
{
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
		size_t req_size = sizeof(p_req->header);
		size_t resp_size = sizeof(p_resp->header);
		switch (p_command)
		{
		case REQ_N_SOURCES:
			req_size += sizeof(p_req->body.n_sources);
			valid_command = 1;
			break;
		default:
			break;
		}

		if (valid_command == 0)
		{
			break;
		}

		p_req->header.f_cmd     = p_command;
		p_req->header.f_cmd_try = 0;
		p_req->header.f_seq     = g_chrony_seq++;
		
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
		DEBUG("chrony plugin: Received response");

		return (0);
	} while (0);
	
	return (1);
}

static void chrony_init_req(tChrony_Request *p_req)
{
	p_req->header.f_version = PROTO_VERSION_NUMBER;
	p_req->header.f_type    = PKT_TYPE_CMD_REQUEST;
	p_req->header.f_dummy0  = 0;
	p_req->header.f_dummy1  = 0;
	p_req->header.f_dummy2  = 0;
	p_req->header.f_dummy3  = 0;
}


/*****************************************************************************/
/* Exported functions */
/*****************************************************************************/
static int chrony_config(const char *p_key, const char *p_value)
{
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
	}
	return (0);
}

static int chrony_read (void)
{
	//plugin_dispatch_values (&vl);
	int status;
	tChrony_Request  chrony_req;
	tChrony_Response chrony_resp;
	size_t chrony_resp_size;

	DEBUG("chrony plugin: Requesting data");
	chrony_init_req(&chrony_req);
	status = chrony_query (REQ_N_SOURCES, &chrony_req, &chrony_resp, &chrony_resp_size);
        if (status != 0)
        {
                ERROR ("chrony plugin: chrony_query (REQ_N_SOURCES) failed with status %i", status);
                return (status);
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
