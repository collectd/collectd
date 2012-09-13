/**
 * collectd - src/teamspeak2.c
 * Copyright (C) 2008  Stefan Hacker
 * Copyright (C) 2008  Florian Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Stefan Hacker <d0t at dbclan dot de>
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/*
 * Defines
 */
/* Default host and port */
#define DEFAULT_HOST	"127.0.0.1"
#define DEFAULT_PORT	"51234"

/*
 * Variables
 */
/* Server linked list structure */
typedef struct vserver_list_s
{
	int port;
	struct vserver_list_s *next;
} vserver_list_t;
static vserver_list_t *server_list = NULL;

/* Host data */
static char *config_host = NULL;
static char *config_port = NULL;

static FILE *global_read_fh = NULL;
static FILE *global_write_fh = NULL;

/* Config data */
static const char *config_keys[] =
{
	"Host",
	"Port",
	"Server"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/*
 * Functions
 */
static int tss2_add_vserver (int vserver_port)
{
	/*
	 * Adds a new vserver to the linked list
	 */
	vserver_list_t *entry;

	/* Check port range */
	if ((vserver_port <= 0) || (vserver_port > 65535))
	{
		ERROR ("teamspeak2 plugin: VServer port is invalid: %i",
				vserver_port);
		return (-1);
	}

	/* Allocate memory */
	entry = (vserver_list_t *) malloc (sizeof (vserver_list_t));
	if (entry == NULL)
	{
		ERROR ("teamspeak2 plugin: malloc failed.");
		return (-1);
	}
	memset (entry, 0, sizeof (vserver_list_t));

	/* Save data */
	entry->port = vserver_port;

	/* Insert to list */
	if(server_list == NULL) {
		/* Add the server as the first element */
		server_list = entry;
	}
	else {
		vserver_list_t *prev;

		/* Add the server to the end of the list */
		prev = server_list;
		while (prev->next != NULL)
			prev = prev->next;
		prev->next = entry;
	}

	INFO ("teamspeak2 plugin: Registered new vserver: %i", vserver_port);

	return (0);
} /* int tss2_add_vserver */

static void tss2_submit_gauge (const char *plugin_instance,
		const char *type, const char *type_instance,
		gauge_t value)
{
	/*
	 * Submits a gauge value to the collectd daemon
	 */
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values     = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "teamspeak2", sizeof (vl.plugin));

	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance,
				sizeof (vl.plugin_instance));

	sstrncpy (vl.type, type, sizeof (vl.type));

	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));
	
	plugin_dispatch_values (&vl);
} /* void tss2_submit_gauge */

static void tss2_submit_io (const char *plugin_instance, const char *type,
		derive_t rx, derive_t tx)
{
	/*
	 * Submits the io rx/tx tuple to the collectd daemon
	 */
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = rx;
	values[1].derive = tx;

	vl.values     = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "teamspeak2", sizeof (vl.plugin));

	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance,
				sizeof (vl.plugin_instance));

	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void tss2_submit_gauge */

static void tss2_close_socket (void)
{
	/*
	 * Closes all sockets
	 */
	if (global_write_fh != NULL)
	{
		fputs ("quit\r\n", global_write_fh);
	}

	if (global_read_fh != NULL)
	{
		fclose (global_read_fh);
		global_read_fh = NULL;
	}

	if (global_write_fh != NULL)
	{
		fclose (global_write_fh);
		global_write_fh = NULL;
	}
} /* void tss2_close_socket */

static int tss2_get_socket (FILE **ret_read_fh, FILE **ret_write_fh)
{
	/*
	 * Returns connected file objects or establishes the connection
	 * if it's not already present
	 */
	struct addrinfo ai_hints;
	struct addrinfo *ai_head;
	struct addrinfo *ai_ptr;
	int sd = -1;
	int status;

	/* Check if we already got opened connections */
	if ((global_read_fh != NULL) && (global_write_fh != NULL))
	{
		/* If so, use them */
		if (ret_read_fh != NULL)
			*ret_read_fh = global_read_fh;
		if (ret_write_fh != NULL)
			*ret_write_fh = global_write_fh;
		return (0);
	}

	/* Get all addrs for this hostname */
	memset (&ai_hints, 0, sizeof (ai_hints));
#ifdef AI_ADDRCONFIG
	ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;

	status = getaddrinfo ((config_host != NULL) ? config_host : DEFAULT_HOST,
			(config_port != NULL) ? config_port : DEFAULT_PORT,
			&ai_hints,
			&ai_head);
	if (status != 0)
	{
		ERROR ("teamspeak2 plugin: getaddrinfo failed: %s",
				gai_strerror (status));
		return (-1);
	}

	/* Try all given hosts until we can connect to one */
	for (ai_ptr = ai_head; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		/* Create socket */
		sd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
				ai_ptr->ai_protocol);
		if (sd < 0)
		{
			char errbuf[1024];
			WARNING ("teamspeak2 plugin: socket failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		/* Try to connect */
		status = connect (sd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		if (status != 0)
		{
			char errbuf[1024];
			WARNING ("teamspeak2 plugin: connect failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (sd);
			continue;
		}

		/*
		 * Success, we can break. Don't need more than one connection
		 */
		break;
	} /* for (ai_ptr) */

	freeaddrinfo (ai_head);

	/* Check if we really got connected */
	if (sd < 0)
		return (-1);

	/* Create file objects from sockets */
	global_read_fh = fdopen (sd, "r");
	if (global_read_fh == NULL)
	{
		char errbuf[1024];
		ERROR ("teamspeak2 plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (sd);
		return (-1);
	}

	global_write_fh = fdopen (sd, "w");
	if (global_write_fh == NULL)
	{
		char errbuf[1024];
		ERROR ("teamspeak2 plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		tss2_close_socket ();
		return (-1);
	}

	{ /* Check that the server correctly identifies itself. */
		char buffer[4096];
		char *buffer_ptr;

		buffer_ptr = fgets (buffer, sizeof (buffer), global_read_fh);
		if (buffer_ptr == NULL)
		{
			WARNING ("teamspeak2 plugin: Unexpected EOF received "
					"from remote host %s:%s.",
					config_host ? config_host : DEFAULT_HOST,
					config_port ? config_port : DEFAULT_PORT);
		}
		buffer[sizeof (buffer) - 1] = 0;

		if (memcmp ("[TS]\r\n", buffer, 6) != 0)
		{
			ERROR ("teamspeak2 plugin: Unexpected response when connecting "
					"to server. Expected ``[TS]'', got ``%s''.",
					buffer);
			tss2_close_socket ();
			return (-1);
		}
		DEBUG ("teamspeak2 plugin: Server send correct banner, connected!");
	}

	/* Copy the new filehandles to the given pointers */
	if (ret_read_fh != NULL)
		*ret_read_fh = global_read_fh;
	if (ret_write_fh != NULL)
		*ret_write_fh = global_write_fh;
	return (0);
} /* int tss2_get_socket */

static int tss2_send_request (FILE *fh, const char *request)
{
	/*
	 * This function puts a request to the server socket
	 */
	int status;

	status = fputs (request, fh);
	if (status < 0)
	{
		ERROR ("teamspeak2 plugin: fputs failed.");
		tss2_close_socket ();
		return (-1);
	}
	fflush (fh);

	return (0);
} /* int tss2_send_request */

static int tss2_receive_line (FILE *fh, char *buffer, int buffer_size)
{
	/*
	 * Receive a single line from the given file object
	 */
	char *temp;
	 
	/*
	 * fgets is blocking but much easier then doing anything else
	 * TODO: Non-blocking Version would be safer
	 */
	temp = fgets (buffer, buffer_size, fh);
	if (temp == NULL)
	{
		char errbuf[1024];
		ERROR ("teamspeak2 plugin: fgets failed: %s",
				sstrerror (errno, errbuf, sizeof(errbuf)));
		tss2_close_socket ();
		return (-1);
	}

	buffer[buffer_size - 1] = 0;
	return (0);
} /* int tss2_receive_line */

static int tss2_select_vserver (FILE *read_fh, FILE *write_fh, vserver_list_t *vserver)
{
	/*
	 * Tell the server to select the given vserver
	 */
	char command[128];
	char response[128];
	int status;

	/* Send request */
	ssnprintf (command, sizeof (command), "sel %i\r\n", vserver->port);

	status = tss2_send_request (write_fh, command);
	if (status != 0)
	{
		ERROR ("teamspeak2 plugin: tss2_send_request (%s) failed.", command);
		return (-1);
	}

	/* Get answer */
	status = tss2_receive_line (read_fh, response, sizeof (response));
	if (status != 0)
	{
		ERROR ("teamspeak2 plugin: tss2_receive_line failed.");
		return (-1);
	}
	response[sizeof (response) - 1] = 0;

	/* Check answer */
	if ((strncasecmp ("OK", response, 2) == 0)
			&& ((response[2] == 0)
				|| (response[2] == '\n')
				|| (response[2] == '\r')))
		return (0);

	ERROR ("teamspeak2 plugin: Command ``%s'' failed. "
			"Response received from server was: ``%s''.",
			command, response);
	return (-1);
} /* int tss2_select_vserver */

static int tss2_vserver_gapl (FILE *read_fh, FILE *write_fh,
		gauge_t *ret_value)
{
	/*
	 * Reads the vserver's average packet loss and submits it to collectd.
	 * Be sure to run the tss2_read_vserver function before calling this so
	 * the vserver is selected correctly.
	 */
	gauge_t packet_loss = NAN;
	int status;

	status = tss2_send_request (write_fh, "gapl\r\n");
	if (status != 0)
	{
		ERROR("teamspeak2 plugin: tss2_send_request (gapl) failed.");
		return (-1);
	}

	while (42)
	{
		char buffer[4096];
		char *value;
		char *endptr = NULL;
		
		status = tss2_receive_line (read_fh, buffer, sizeof (buffer));
		if (status != 0)
		{
			/* Set to NULL just to make sure noone uses these FHs anymore. */
			read_fh = NULL;
			write_fh = NULL;
			ERROR ("teamspeak2 plugin: tss2_receive_line failed.");
			return (-1);
		}
		buffer[sizeof (buffer) - 1] = 0;
		
		if (strncmp ("average_packet_loss=", buffer,
					strlen ("average_packet_loss=")) == 0)
		{
			/* Got average packet loss, now interpret it */
			value = &buffer[20];
			/* Replace , with . */
			while (*value != 0)
			{
				if (*value == ',')
				{
					*value = '.';
					break;
				}
				value++;
			}
			
			value = &buffer[20];
			
			packet_loss = strtod (value, &endptr);
			if (value == endptr)
			{
				/* Failed */
				WARNING ("teamspeak2 plugin: Could not read average package "
						"loss from string: %s", buffer);
				continue;
			}
		}
		else if (strncasecmp ("OK", buffer, 2) == 0)
		{
			break;
		}
		else if (strncasecmp ("ERROR", buffer, 5) == 0)
		{
			ERROR ("teamspeak2 plugin: Server returned an error: %s", buffer);
			return (-1);
		}
		else
		{
			WARNING ("teamspeak2 plugin: Server returned unexpected string: %s",
					buffer);
		}
	}
	
	*ret_value = packet_loss;
	return (0);
} /* int tss2_vserver_gapl */

static int tss2_read_vserver (vserver_list_t *vserver)
{
	/*
	 * Poll information for the given vserver and submit it to collect.
	 * If vserver is NULL the global server information will be queried.
	 */
	int status;

	gauge_t users = NAN;
	gauge_t channels = NAN;
	gauge_t servers = NAN;
	derive_t rx_octets = 0;
	derive_t tx_octets = 0;
	derive_t rx_packets = 0;
	derive_t tx_packets = 0;
	gauge_t packet_loss = NAN;
	int valid = 0;

	char plugin_instance[DATA_MAX_NAME_LEN];

	FILE *read_fh;
	FILE *write_fh;

	/* Get the send/receive sockets */
	status = tss2_get_socket (&read_fh, &write_fh);
	if (status != 0)
	{
		ERROR ("teamspeak2 plugin: tss2_get_socket failed.");
		return (-1);
	}

	if (vserver == NULL)
	{
		/* Request global information */
		memset (plugin_instance, 0, sizeof (plugin_instance));

		status = tss2_send_request (write_fh, "gi\r\n");
	}
	else
	{
		/* Request server information */
		ssnprintf (plugin_instance, sizeof (plugin_instance), "vserver%i",
				vserver->port);

		/* Select the server */
		status = tss2_select_vserver (read_fh, write_fh, vserver);
		if (status != 0)
			return (status);

		status = tss2_send_request (write_fh, "si\r\n");
	}

	if (status != 0)
	{
		ERROR ("teamspeak2 plugin: tss2_send_request failed.");
		return (-1);
	}

	/* Loop until break */
	while (42)
	{
		char buffer[4096];
		char *key;
		char *value;
		char *endptr = NULL;
		
		/* Read one line of the server's answer */
		status = tss2_receive_line (read_fh, buffer, sizeof (buffer));
		if (status != 0)
		{
			/* Set to NULL just to make sure noone uses these FHs anymore. */
			read_fh = NULL;
			write_fh = NULL;
			ERROR ("teamspeak2 plugin: tss2_receive_line failed.");
			break;
		}

		if (strncasecmp ("ERROR", buffer, 5) == 0)
		{
			ERROR ("teamspeak2 plugin: Server returned an error: %s",
					buffer);
			break;
		}
		else if (strncasecmp ("OK", buffer, 2) == 0)
		{
			break;
		}

		/* Split line into key and value */
		key = strchr (buffer, '_');
		if (key == NULL)
		{
			DEBUG ("teamspeak2 plugin: Cannot parse line: %s", buffer);
			continue;
		}
		key++;

		/* Evaluate assignment */
		value = strchr (key, '=');
		if (value == NULL)
		{
			DEBUG ("teamspeak2 plugin: Cannot parse line: %s", buffer);
			continue;
		}
		*value = 0;
		value++;

		/* Check for known key and save the given value */
		/* global info: users_online,
		 * server info: currentusers. */
		if ((strcmp ("currentusers", key) == 0)
				|| (strcmp ("users_online", key) == 0))
		{
			users = strtod (value, &endptr);
			if (value != endptr)
				valid |= 0x01;
		}
		/* global info: channels,
		 * server info: currentchannels. */
		else if ((strcmp ("currentchannels", key) == 0)
				|| (strcmp ("channels", key) == 0))
		{
			channels = strtod (value, &endptr);
			if (value != endptr)
				valid |= 0x40;
		}
		/* global only */
		else if (strcmp ("servers", key) == 0)
		{
			servers = strtod (value, &endptr);
			if (value != endptr)
				valid |= 0x80;
		}
		else if (strcmp ("bytesreceived", key) == 0)
		{
			rx_octets = strtoll (value, &endptr, 0);
			if (value != endptr)
				valid |= 0x02;
		}
		else if (strcmp ("bytessend", key) == 0)
		{
			tx_octets = strtoll (value, &endptr, 0);
			if (value != endptr)
				valid |= 0x04;
		}
		else if (strcmp ("packetsreceived", key) == 0)
		{
			rx_packets = strtoll (value, &endptr, 0);
			if (value != endptr)
				valid |= 0x08;
		}
		else if (strcmp ("packetssend", key) == 0)
		{
			tx_packets = strtoll (value, &endptr, 0);
			if (value != endptr)
				valid |= 0x10;
		}
		else if ((strncmp ("allow_codec_", key, strlen ("allow_codec_")) == 0)
				|| (strncmp ("bwinlast", key, strlen ("bwinlast")) == 0)
				|| (strncmp ("bwoutlast", key, strlen ("bwoutlast")) == 0)
				|| (strncmp ("webpost_", key, strlen ("webpost_")) == 0)
				|| (strcmp ("adminemail", key) == 0)
				|| (strcmp ("clan_server", key) == 0)
				|| (strcmp ("countrynumber", key) == 0)
				|| (strcmp ("id", key) == 0)
				|| (strcmp ("ispname", key) == 0)
				|| (strcmp ("linkurl", key) == 0)
				|| (strcmp ("maxusers", key) == 0)
				|| (strcmp ("name", key) == 0)
				|| (strcmp ("password", key) == 0)
				|| (strcmp ("platform", key) == 0)
				|| (strcmp ("server_platform", key) == 0)
				|| (strcmp ("server_uptime", key) == 0)
				|| (strcmp ("server_version", key) == 0)
				|| (strcmp ("udpport", key) == 0)
				|| (strcmp ("uptime", key) == 0)
				|| (strcmp ("users_maximal", key) == 0)
				|| (strcmp ("welcomemessage", key) == 0))
			/* ignore */;
		else
		{
			INFO ("teamspeak2 plugin: Unknown key-value-pair: "
					"key = %s; value = %s;", key, value);
		}
	} /* while (42) */

	/* Collect vserver packet loss rates only if the loop above did not exit
	 * with an error. */
	if ((status == 0) && (vserver != NULL))
	{
		status = tss2_vserver_gapl (read_fh, write_fh, &packet_loss);
		if (status == 0)
		{
			valid |= 0x20;
		}
		else
		{
			WARNING ("teamspeak2 plugin: Reading package loss "
					"for vserver %i failed.", vserver->port);
		}
	}

	if ((valid & 0x01) == 0x01)
		tss2_submit_gauge (plugin_instance, "users", NULL, users);

	if ((valid & 0x06) == 0x06)
		tss2_submit_io (plugin_instance, "io_octets", rx_octets, tx_octets);

	if ((valid & 0x18) == 0x18)
		tss2_submit_io (plugin_instance, "io_packets", rx_packets, tx_packets);

	if ((valid & 0x20) == 0x20)
		tss2_submit_gauge (plugin_instance, "percent", "packet_loss", packet_loss);

	if ((valid & 0x40) == 0x40)
		tss2_submit_gauge (plugin_instance, "gauge", "channels", channels);

	if ((valid & 0x80) == 0x80)
		tss2_submit_gauge (plugin_instance, "gauge", "servers", servers);

	if (valid == 0)
		return (-1);
	return (0);
} /* int tss2_read_vserver */

static int tss2_config (const char *key, const char *value)
{
	/*
	 * Interpret configuration values
	 */
    if (strcasecmp ("Host", key) == 0)
	{
		char *temp;

		temp = strdup (value);
		if (temp == NULL)
		{
			ERROR("teamspeak2 plugin: strdup failed.");
			return (1);
		}
		sfree (config_host);
		config_host = temp;
	}
	else if (strcasecmp ("Port", key) == 0)
	{
		char *temp;

		temp = strdup (value);
		if (temp == NULL)
		{
			ERROR("teamspeak2 plugin: strdup failed.");
			return (1);
		}
		sfree (config_port);
		config_port = temp;
	}
	else if (strcasecmp ("Server", key) == 0)
	{
		/* Server variable found */
		int status;
		
		status = tss2_add_vserver (atoi (value));
		if (status != 0)
			return (1);
	}
	else
	{
		/* Unknown variable found */
		return (-1);
	}

	return 0;
} /* int tss2_config */

static int tss2_read (void)
{
	/*
	 * Poll function which collects global and vserver information
	 * and submits it to collectd
	 */
	vserver_list_t *vserver;
	int success = 0;
	int status;

	/* Handle global server variables */
	status = tss2_read_vserver (NULL);
	if (status == 0)
	{
		success++;
	}
	else
	{
		WARNING ("teamspeak2 plugin: Reading global server variables failed.");
	}

	/* Handle vservers */
	for (vserver = server_list; vserver != NULL; vserver = vserver->next)
	{
		status = tss2_read_vserver (vserver);
		if (status == 0)
		{
			success++;
		}
		else
		{
			WARNING ("teamspeak2 plugin: Reading statistics "
					"for vserver %i failed.", vserver->port);
			continue;
		}
	}
	
	if (success == 0)
		return (-1);
    return (0);
} /* int tss2_read */

static int tss2_shutdown(void)
{
	/*
	 * Shutdown handler
	 */
	vserver_list_t *entry;

	tss2_close_socket ();

	entry = server_list;
	server_list = NULL;
	while (entry != NULL)
	{
		vserver_list_t *next;

		next = entry->next;
		sfree (entry);
		entry = next;
	}

	/* Get rid of the configuration */
	sfree (config_host);
	sfree (config_port);
	
    return (0);
} /* int tss2_shutdown */

void module_register(void)
{
	/*
	 * Mandatory module_register function
	 */
	plugin_register_config ("teamspeak2", tss2_config,
			config_keys, config_keys_num);
	plugin_register_read ("teamspeak2", tss2_read);
	plugin_register_shutdown ("teamspeak2", tss2_shutdown);
} /* void module_register */

/* vim: set sw=4 ts=4 : */
