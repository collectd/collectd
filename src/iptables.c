/**
 * collectd - src/iptables.c
 * Copyright (C) 2007 Sjoerd van der Berg
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *  Sjoerd van der Berg <harekiet at users.sourceforge.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#if HAVE_LIBIPTC_LIBIPTC_H
# include <libiptc/libiptc.h>
#endif

#if HAVE_LIBIPTC_LIBIPTC_H
# define IPTABLES_HAVE_READ 1
#else
# define IPTABLES_HAVE_READ 0
#endif

#define MODULE_NAME "iptables"
#define BUFSIZE 512

/*
 * (Module-)Global variables
 */

/*
 *  Files will go into iptables-chain/comment.rrd files
 */
static char *file_template   = "iptables-%s.rrd";

/*
 * Removed packet count for now, should have config option if you want to save
 * them Although other collectd models don't seem to care much for options
 * eitherway for what to log
 */
static char *ds_def[] =
{
/*	"DS:packets:COUNTER:"COLLECTD_HEARTBEAT":0:U", */
	"DS:bytes:DERIVE:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int ds_num = 1;

#if IPTABLES_HAVE_READ
/*
 * Config format should be `Chain table chainname',
 * e. g. `Chain mangle incoming'
 */
static char *config_keys[] =
{
	"Chain",
	NULL
};
static int config_keys_num = 1;
/*
    Each table/chain combo that will be queried goes into this list
*/
typedef struct {
    char table[16];
    char name[32];
} ip_chain_t;

static ip_chain_t **chain_list = NULL;
static int chain_num = 0;

static int iptables_config (char *key, char *value)
{
	if (strcasecmp (key, "Chain") == 0)
	{
		ip_chain_t temp, *final, **list;
		char *chain;
		int tLen;
		
		memset( &temp, 0, sizeof( temp ));
	
		/* simple parsing, only allow a space... */
    		chain = rindex(value, ' ' );
		if (!chain) 
		{
			syslog (LOG_EMERG, "missing chain." );
			return (1);
		}
		tLen = (int)(chain - value);
		if ( tLen > sizeof( temp.table ))
		{
			syslog (LOG_EMERG, "table too long." );
			return (1);
		}
		memcpy( temp.table, value, tLen );
		temp.table[tLen] = 0; 
		chain++;
		strncpy( temp.name, chain, sizeof( temp.name ));
			
		list = (ip_chain_t **) realloc (chain_list, (chain_num + 1) * sizeof (ip_chain_t *));
		if ( list == NULL )
		{
			syslog (LOG_EMERG, "Cannot allocate more memory.");
			return (1);
		}
		chain_list = list;
		final = (ip_chain_t *) malloc( sizeof(temp) );
		if (final == NULL) 
		{
			syslog (LOG_EMERG, "Cannot allocate memory.");
			return (1);
		}
		*final = temp;
		chain_list[chain_num++] = final;
	} else 
	{
		return (-1);
	}

	return (0);
}
#endif /* IPTABLES_HAVE_READ */

static void iptables_init (void)
{	
    return;
}

static void iptables_write (char *host, char *inst, char *val) 
{
	char file[BUFSIZE];
	int status;

	status = snprintf (file, BUFSIZE, file_template, inst);
	if (status < 1)
		return;
	else if (status >= BUFSIZE)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if IPTABLES_HAVE_READ
static int submit_match (const struct ipt_entry_match *match,
		const struct ipt_entry *entry, const ip_chain_t *chain) 
{
    char name[BUFSIZE];
    char buf[BUFSIZE];
    int status;

    /* Only log rules that have a comment, although could probably also do numerical targets sometime */
    if ( strcmp( match->u.user.name, "comment" ) )
	return 0;

/*
    This would also add the table name to the name, but seems a bit overkill
    status = snprintf (name, BUFSIZE, "%s-%s/%s",
	table->table, table->chain, match->data );
*/
    status = snprintf (name, BUFSIZE, "%s/%s", chain->name, match->data );

    if ((status >= BUFSIZE) || (status < 1))
    	return 0;

    status = snprintf (buf, BUFSIZE, "%u:%lld", /* ":lld", */
				(unsigned int) curtime,
				/* entry->counters.pcnt, */
				 entry->counters.bcnt );
    if ((status >= BUFSIZE) || (status < 1))
    	return 0;

    plugin_submit (MODULE_NAME, name, buf);

    return 0;
} /* int submit_match */

static void submit_chain( iptc_handle_t *handle, ip_chain_t *chain ) {
    const struct ipt_entry *entry;

    /* Find first rule for chain and use the iterate macro */    
    entry = iptc_first_rule( chain->name, handle );
    while ( entry ) {
        IPT_MATCH_ITERATE( entry, submit_match, entry, chain );
	entry = iptc_next_rule( entry, handle );
    }
}


static void iptables_read (void) {
    int i;

    /* Init the iptc handle structure and query the correct table */    
    for( i = 0; i < chain_num; i++) {
	iptc_handle_t handle;
	ip_chain_t *chain;
	
	chain = chain_list[i];
	if (!chain)
	    continue;
	handle = iptc_init( chain->table );
	if (!handle)
	    continue;
	submit_chain( &handle, chain );
	iptc_free( &handle );
    }
}
#else /* !IPTABLES_HAVE_READ */
# define iptables_read NULL
#endif

void module_register (void)
{
	plugin_register (MODULE_NAME, iptables_init, iptables_read, iptables_write);
#if IPTABLES_HAVE_READ
	cf_register (MODULE_NAME, iptables_config, config_keys, config_keys_num);
#endif
}

#undef BUFSIZE
#undef MODULE_NAME

/*
 * vim:shiftwidth=4:softtabstop=4:tabstop=8
 */
