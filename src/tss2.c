/**
 * collectd - src/tss2.c
 * Copyright (C) 2008  Stefan Hacker
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
 **/


/*
 * Defines
 */
 
/* Teamspeak query protocol defines */
#define TELNET_BANNER   "[TS]\r\n"
#define TELNET_BANNER_LENGTH 5
#define TELNET_ERROR   "ERROR"
#define TELNET_OK	   "OK"
#define TELNET_QUIT	   "quit\r\n"

/* Predefined settings */
#define TELNET_BUFFSIZE 512
#define DEFAULT_HOST	"127.0.0.1"
#define DEFAULT_PORT	51234

/* VServer request defines */
#define S_REQUEST	   "si\r\n"
#define S_USERS_ONLINE "server_currentusers="
#define S_PACKETS_SEND "server_packetssend="
#define S_PACKETS_REC  "server_packetsreceived="
#define S_BYTES_SEND   "server_bytessend="
#define S_BYTES_REC	   "server_bytesreceived="

/* Global request defines */
#define T_REQUEST	   "gi\r\n"
#define T_USERS_ONLINE "total_users_online="
#define T_PACKETS_SEND "total_packetssend="
#define T_PACKETS_REC  "total_packetsreceived="
#define T_BYTES_SEND   "total_bytessend="
#define T_BYTES_REC	   "total_bytesreceived="

/* Convinience defines */
#define SOCKET			int
#define INVALID_SOCKET 0


/*
 * Includes
 */
 
#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * Variables
 */
 
/* Server linked list structure */
typedef struct server_s {
	int port;
	struct server_s *next;
} server_t;
static server_t *pserver = NULL;


/* Host data */
static char *host		= DEFAULT_HOST;
static int   port	   	= DEFAULT_PORT;

static SOCKET telnet	= INVALID_SOCKET;
static FILE *telnet_in	= NULL;


/* Config data */
static const char *config_keys[] =
{
    "Host",
	"Port",
    "Server",
    NULL
};
static int config_keys_num = 3;


/*
 * Functions
 */

static void add_server(server_t *new_server)
{
	/*
	 * Adds a new server to the linked list 
	 */
	server_t *tmp	 = NULL;
	new_server->next = NULL;

	if(pserver == NULL) {
		/* Add the server as the first element */
		pserver = new_server;
	}
	else {
		/* Add the server to the end of the list */
		tmp = pserver;
		while(tmp->next != NULL) {
			tmp = tmp->next;
		}
		tmp->next = new_server;
	}

	DEBUG("Registered new server '%d'", new_server->port); 
} /* void add_server */


static int do_connect(void)
{
	/*
	 * Tries to establish a connection to the server
	 */
	struct sockaddr_in addr;
	
	/* Establish telnet connection */
	telnet = socket(AF_INET, SOCK_STREAM, 0);
	
	addr.sin_family 		= AF_INET;
	addr.sin_addr.s_addr 	= inet_addr(host);
	addr.sin_port 			= htons(port);

	if(connect(telnet, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		/* Connection failed */
		return -1;
	}
	return 0;
} /* int do_connect */


static int do_request(char *request)
{
	/*
	 * Pushes a request
	 */
	int ret = 0;
	DEBUG("Send Request: '%s'", request);
	
	/* Send the request */
	if((ret = send(telnet, request, strlen(request), 0))==-1) {
		/* Send data failed */
		if (telnet!=INVALID_SOCKET) {
			close(telnet);
			telnet = INVALID_SOCKET;
		}
		char errbuf[1024];
		ERROR("tss2 plugin: send data to host '%s' failed: %s",
				host,
				sstrerror(errno, errbuf,
						  sizeof(errbuf)));
		return -1;
	}
	return ret;
} /* int do_request */


static int do_recv(char *buffer, int buffer_size, long int usecs)
{
	/*
	 * Tries to receive from the connection 'timeout' seconds
	 */
	int	   ret = 0;
	fd_set rset;
	struct timeval timeout;

	timeout.tv_sec     = 0;
	timeout.tv_usec    = usecs;

	FD_ZERO(&rset);
	FD_SET(telnet, &rset);

	if (select(FD_SETSIZE, &rset, NULL, NULL, &timeout) == -1) {
		/* Select failed */
		if (telnet!=INVALID_SOCKET) {
			close(telnet);
			telnet = INVALID_SOCKET;
		}
		
		char errbuf[1024];
		ERROR("tss2 plugin: select failed: %s",
				sstrerror(errno, errbuf,
				sizeof(errbuf)));
		return -1;
	}
	if (!FD_ISSET(telnet, &rset)) {
		/* Timeout for answer reached --> disconnect */
		if (telnet!=INVALID_SOCKET) {
			close(telnet);
			telnet = INVALID_SOCKET;
		}
		WARNING("tss2 plugin: request timed out (closed connection)");
		return -1;
	}
	if ((ret = recv(telnet, buffer, buffer_size, 0)) == -1) {
		/* Recv failed */
		if (telnet!=INVALID_SOCKET) {
			close(telnet);
			telnet = INVALID_SOCKET;
		}
		
		char errbuf[1024];
		ERROR("tss2 plugin: recv failed: %s",
			  sstrerror(errno, errbuf,
			  sizeof(errbuf)));
		return -1;
	}
	return ret;
} /* int do_recv */


static int is_eq(char *eq, char *str) {
	/*
	 * Checks if the given str starts with eq
	*/
	if (strlen(eq) > strlen(str)) return -1;
	return strncmp(eq, str, strlen(eq));
}


static long int eval_eq(char *eq, char *str) {
	/*
	 * Returns the value written behind the eq string in str as a long int
	 */
	return strtol((char*)&str[strlen(eq)], NULL, 10);
}


static int do_recv_line(char *buffer, int buffer_size, long int usecs)
{
	/*
	 * Receives a line from the socket
	 */
	 
	/*
	 * fgets is blocking but much easier then doing anything else
	 * TODO: Non-blocking Version would be safer
	 */
	if ((fgets(buffer, buffer_size, telnet_in)) == NULL) {
		/* Receive line failed */
		if (telnet != INVALID_SOCKET) {
			close(telnet);
			telnet = INVALID_SOCKET;
		}
		
		char errbuf[1024];
		ERROR("tss2 plugin: fgets failed: %s",
			  sstrerror(errno, errbuf,
			  sizeof(errbuf)));
		return -1;
	}
	DEBUG("Line: %s", buffer);
	return 0;
}


static int tss2_config(const char *key, const char *value)
{
	/*
	 * Configuration interpreter function
	 */
	char *phost = NULL;
	
    if (strcasecmp(key, "host") == 0) {
    	/* Host variable found*/
		if ((phost = strdup(value)) == NULL) {
			char errbuf[1024];
			ERROR("tss2 plugin: strdup failed: %s",
				sstrerror(errno, errbuf,
						  sizeof(errbuf)));
			return 1;
		}
		host = (char*)phost;
	}
	else if (strcasecmp(key, "port") == 0) {
		/* Port variable found */
		port = atoi(value);
	}
	else if (strcasecmp(key, "server") == 0) {
		/* Server variable found */
		server_t *new_server = NULL;

		if ((new_server = (server_t *)malloc(sizeof(server_t))) == NULL) {
			char errbuf[1024];
			ERROR("tss2 plugin: malloc failed: %s",
				  sstrerror (errno, errbuf,
				  sizeof (errbuf)));
			return 1;
		}

		new_server->port = atoi(value);
		add_server((struct server_s*)new_server);
	}
	else {
		/* Unknow variable found */
		return 1;
	}

	return 0;
}


static int tss2_init(void)
{
	/*
	 * Function to initialize the plugin
	 */
	char buff[TELNET_BANNER_LENGTH + 1]; /*Prepare banner buffer*/
	
	/*Connect to telnet*/
	DEBUG("tss2 plugin: Connecting to '%s:%d'", host, port);
	if (do_connect()!=0) {
		/* Failed */
		char errbuf[1024];
		ERROR("tss2 plugin: connect to %s:%d failed: %s",
			host,
			port,
			sstrerror(errno, errbuf,
					  sizeof(errbuf)));
		return 1;
	}
	else {
		DEBUG("tss2 plugin: connection established!")
	}
	
	/*Check if this is the real thing*/
	if (do_recv(buff, sizeof(buff), 1) == -1) {
		/* Failed */
		return 1;
	}
	DEBUG("tss2 plugin: received banner '%s'", buff);
	
	if (strcmp(buff, TELNET_BANNER)!=0) {
		/* Received unexpected banner string */
		ERROR("tss2 plugin: host %s:%d is no teamspeak2 query port",
			host, port);
		return 1;
	}
	
	/*Alright, we are connected now get a file descriptor*/
	if ((telnet_in = fdopen(telnet, "r")) == NULL) {
		/* Failed */
		char errbuf[1024];
		ERROR("tss2 plugin: fdopen failed",
				sstrerror(errno, errbuf,
				sizeof(errbuf)));
		return 1;
	}
	DEBUG("tss2 plugin: Connection established", host, port);
    return 0;
} /* int tss2_init */


static void tss2_submit (gauge_t users,
					   counter_t bytes_send, counter_t bytes_received,
					   counter_t packets_send, counter_t packets_received,
					   char *server)
{
	/*
	 * Function to submit values to collectd
	 */
	value_t v_users[1];
	value_t v_octets[2];
	value_t v_packets[2];
	
	value_list_t vl_users   = VALUE_LIST_INIT;
	value_list_t vl_octets  = VALUE_LIST_INIT;
	value_list_t vl_packets = VALUE_LIST_INIT;
	
	/* 
	 * Dispatch users gauge
	 */
	v_users[0].gauge    = users;
	
	vl_users.values     = v_users;
	vl_users.values_len = STATIC_ARRAY_SIZE (v_users);
	vl_users.time       = time (NULL);

	
	strcpy(vl_users.host, hostname_g);
	strcpy(vl_users.plugin, "tss2");
	
	if (server != NULL) {
		/* VServer values */
		strcpy(vl_users.plugin_instance, "");
		strncpy(vl_users.type_instance, server, sizeof(vl_users.type_instance));
	}
	
	plugin_dispatch_values ("users", &vl_users);
	
	/* 
	 * Dispatch octets counter
	 */
	v_octets[0].counter  = bytes_send;
	v_octets[1].counter  = bytes_received;
	
	vl_octets.values     = v_octets;
	vl_octets.values_len = STATIC_ARRAY_SIZE (v_octets);
	vl_octets.time       = time (NULL);

	strcpy(vl_octets.host, hostname_g);
	strcpy(vl_octets.plugin, "tss2");
	
	if (server != NULL) {
		/* VServer values */
		strcpy(vl_octets.plugin_instance, "");
		strncpy(vl_octets.type_instance, server, sizeof(vl_octets.type_instance));
	}
	
	plugin_dispatch_values ("octets", &vl_octets);

	/* 
	 * Dispatch packets counter
	 */
	v_packets[0].counter  = packets_send;
	v_packets[1].counter  = packets_send;
	
	vl_packets.values     = v_packets;
	vl_packets.values_len = STATIC_ARRAY_SIZE (v_packets);
	vl_packets.time       = time (NULL);
	
	strcpy(vl_packets.host, hostname_g);
	strcpy(vl_packets.plugin, "tss2");
	
	if (server != NULL) {
		/* VServer values */
		strcpy(vl_packets.plugin_instance, "");
		strncpy(vl_packets.type_instance, server, sizeof(vl_packets.type_instance));
	}
	
	plugin_dispatch_values("packets", &vl_packets);
} /* void tss2_submit */


static int tss2_read(void)
{
	/*
	 * Tries to read the current values from all servers and to submit them
	 */
	char buff[TELNET_BUFFSIZE];
	server_t *tmp;
    
	/* Variables for received values */
	int collected			    = 0;
	int users_online			= 0;
	
	long int bytes_received		= 0;
	long int bytes_send			= 0;
	long int packets_received	= 0;
	long int packets_send		= 0;
	
	/*Check if we are connected*/
	if ((telnet == INVALID_SOCKET) && (do_connect() != 0)) {
		/* Disconnected and reconnect failed */
		char errbuf[1024];
		ERROR("tss2 plugin: reconnect to %s:%d failed: %s",
			host,
			port,
			sstrerror(errno, errbuf,
					  sizeof(errbuf)));
		return -1;
	}
	
	/* Request global server variables */
	if (do_request(T_REQUEST) == -1) {
		/* Collect global info failed */
		ERROR("tss2 plugin: Collect global information request failed");
		return -1;
	}

	collected = 0; /* Counts the number of variables found in the reply */
	
	for(;;) {
		/* Request a line with a timeout of 200ms */
		if (do_recv_line(buff, TELNET_BUFFSIZE, 200000) != 0) {
			/* Failed */
			ERROR("tss2 plugin: Collect global information failed");
			return -1;
		}
		
		/*
		 * Collect the received data
		 */
		if (is_eq(T_USERS_ONLINE, buff) == 0) {
			/* Number of users online */
			users_online = (int)eval_eq(T_USERS_ONLINE, buff);
			DEBUG("users_online: %d", users_online);
			collected += 1;
		}
		else if (is_eq(T_PACKETS_SEND, buff) == 0) {
			/* Number of packets send */
			packets_send = eval_eq(T_PACKETS_SEND, buff);
			DEBUG("packets_send: %ld", packets_send);
			collected += 1;
		}
		else if (is_eq(T_PACKETS_REC, buff) == 0) {
			/* Number of packets received */
			packets_received = eval_eq(T_PACKETS_REC, buff);
			DEBUG("packets_received: %ld", packets_received);
			collected += 1;
		}
		else if (is_eq(T_BYTES_SEND, buff) == 0) {
			/* Number of bytes send */
			bytes_send = eval_eq(T_BYTES_SEND, buff);
			DEBUG("bytes_send: %ld", bytes_send);
			collected += 1;
		}
		else if (is_eq(T_BYTES_REC, buff) == 0) {
			/* Number of bytes received */
			bytes_received = eval_eq(T_BYTES_REC, buff);
			DEBUG("byte_received: %ld", bytes_received);
			collected += 1;
		}
		else if (is_eq(TELNET_OK, buff) == 0) {
			/* Received end of transmission flag */
			if (collected < 5) {
				/* Not all expected values were received */
				ERROR("tss2 plugin: Couldn't collect all values (%d)", collected);
				return -1;
			}
			/*
			 * Everything is fine, let's break out of the loop
			 */
			break;
		}
		else if (is_eq(TELNET_ERROR, buff) == 0) {
			/* An error occured on the servers' side */
			ERROR("tss2 plugin: host reported error '%s'", buff);
			return -1;
		}
	}
	
	/* Forward values to collectd */
	DEBUG("Full global dataset received");
	tss2_submit(users_online, bytes_send, bytes_received, packets_send, packets_received, NULL);

	/* Collect values of servers */
	tmp = pserver;
	while(tmp != NULL) {
		/* Try to select server */
		sprintf(buff, "sel %d\r\n", tmp->port);
		
		if (do_request(buff) == -1) return -1; /* Send the request */
		if (do_recv_line(buff, TELNET_BUFFSIZE, 200000)!=0) return -1; /* Receive the first line */
		
		if (is_eq(buff,TELNET_ERROR) == 0) {
			/*Could not select server, go to the next one*/
			WARNING("tss2 plugin: Could not select server '%d'", tmp->port);
			tmp = tmp->next;
			continue;
		}
		else if (is_eq(TELNET_OK, buff) == 0) {
			/*
			 * VServer selected, now request its information
			 */
			collected = 0; /* Counts the number of variables found in the reply */
			
			if (do_request(S_REQUEST) == -1) {
				/* Failed */
				WARNING("tss2 plugin: Collect info about server '%d' failed", tmp->port);
				tmp = tmp->next;
				continue;
			}

			for(;;) {
				/* Request a line with a timeout of 200ms */
				if (do_recv_line(buff, TELNET_BUFFSIZE, 200000) !=0 ) {
					ERROR("tss2 plugin: Connection error");
					return -1;
				}
				
				/*
				 * Collect the received data
				 */
				if (is_eq(S_USERS_ONLINE, buff) == 0) {
					/* Number of users online */
					users_online = (int)eval_eq(S_USERS_ONLINE, buff);
					collected += 1;
				}
				else if (is_eq(S_PACKETS_SEND, buff) == 0) {
					/* Number of packets send */
					packets_send = eval_eq(S_PACKETS_SEND, buff);
					collected += 1;
				}
				else if (is_eq(S_PACKETS_REC, buff) == 0) {
					/* Number of packets received */
					packets_received = eval_eq(S_PACKETS_REC, buff);
					collected += 1;
				}
				else if (is_eq(S_BYTES_SEND, buff) == 0) {
					/* Number of bytes send */
					bytes_send = eval_eq(S_BYTES_SEND, buff);
					collected += 1;
				}
				else if (is_eq(S_BYTES_REC, buff) == 0) {
					/* Number of bytes received */
					bytes_received = eval_eq(S_BYTES_REC, buff);
					collected += 1;
				}
				else if (is_eq(TELNET_OK, buff) == 0) {
					/*
					 * Received end of transmission flag, break the loop
					 */
					break;
				}
				else if (is_eq(TELNET_ERROR, buff) == 0) {
					/* Error, not good */
					ERROR("tss2 plugin: server '%d' reported error '%s'", tmp->port, buff);
					return -1;
				}
			}
			
			if (collected < 5) {
				/* Not all expected values were received */
				ERROR("tss2 plugin: Couldn't collect all values of server '%d' (%d)", tmp->port, collected);
				tmp = tmp->next;
				continue; /* Continue with the next VServer */
			}
			
			/* Forward values to connectd */
			sprintf(buff,"%d",tmp->port);
			tss2_submit(users_online, bytes_send, bytes_received, packets_send, packets_received, buff);

		}
		else {
			/*The server send us garbage? wtf???*/
			ERROR("tss2 plugin: Server send garbage");
			return -1;
		}
		tmp = tmp->next;
	}

    return 0;
} /* int tss2_read */


static int tss2_shutdown(void)
{
	/*
	 * Shutdown handler
	 */
	DEBUG("tss2 plugin: Shutdown");
	server_t *tmp = NULL;
	
	/* Close our telnet socket */
	if (telnet != INVALID_SOCKET) {
		do_request(TELNET_QUIT);
		fclose(telnet_in);
		telnet_in = NULL;
		telnet = INVALID_SOCKET;
	}
	
	/* Release all allocated memory */
	while(pserver != NULL) {
		tmp 	= pserver;
		pserver = pserver->next;
		free(tmp);
	}
	
	/* Get rid of the rest */
	if (host != DEFAULT_HOST) {
		free(host);
		host = (char*)DEFAULT_HOST;
	}
	
    return 0;
} /* int tss2_shutdown */


void module_register(void)
{
	/*
	 * Module registrator
	 */
	plugin_register_config("tss2",
                            tss2_config,
                            config_keys,
                            config_keys_num);

	plugin_register_init("tss2", tss2_init);
	plugin_register_read("tss2", tss2_read);
	plugin_register_shutdown("tss2", tss2_shutdown);
} /* void module_register */
