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
#include "utils_debug.h"

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
 * Removed packet count for now, should have config option if you want to save
 * them Although other collectd models don't seem to care much for options
 * eitherway for what to log
 */
/* Limit to ~125MByte/s (~1GBit/s) */
static char *ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:134217728",
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
#ifndef XT_TABLE_MAXNAMELEN
# define XT_TABLE_MAXNAMELEN 32
#endif
typedef struct {
    char table[XT_TABLE_MAXNAMELEN];
    char chain[XT_TABLE_MAXNAMELEN];
    union
    {
	int   num;
	char *comment;
    } rule;
    enum
    {
	RTYPE_NUM,
	RTYPE_COMMENT,
	RTYPE_COMMENT_ALL
    } rule_type;
    char name[64];
} ip_chain_t;

static ip_chain_t **chain_list = NULL;
static int chain_num = 0;

static int iptables_config (char *key, char *value)
{
	if (strcasecmp (key, "Chain") == 0)
	{
		ip_chain_t temp, *final, **list;
		char *table;
		int   table_len;
		char *chain;
		int   chain_len;

		char *value_copy;
		char *fields[4];
		int   fields_num;
		
		memset (&temp, 0, sizeof (temp));

		value_copy = strdup (value);
		if (value_copy == NULL)
		{
		    syslog (LOG_ERR, "strdup failed: %s", strerror (errno));
		    return (1);
		}

		/* Chain <table> <chain> [<comment|num> [name]] */
		fields_num = strsplit (value_copy, fields, 4);
		if (fields_num < 2)
		{
		    free (value_copy);
		    return (1);
		}

		table = fields[0];
		chain = fields[1];

		table_len = strlen (table);
		if (table_len >= sizeof(temp.table))
		{
			syslog (LOG_ERR, "Table `%s' too long.", table);
			free (value_copy);
			return (1);
		}
		strncpy (temp.table, table, table_len);
		temp.table[table_len] = '\0';

		chain_len = strlen (chain);
		if (chain_len >= sizeof(temp.chain))
		{
			syslog (LOG_ERR, "Chain `%s' too long.", chain);
			free (value_copy);
			return (1);
		}
		strncpy (temp.chain, chain, chain_len);
		temp.chain[chain_len] = '\0'; 

		if (fields_num >= 3)
		{
		    char *comment = fields[2];
		    int   rule = atoi (comment);

		    if (rule)
		    {
			temp.rule.num = rule;
			temp.rule_type = RTYPE_NUM;
		    }
		    else
		    {
			strncpy (temp.rule.comment, comment,
				sizeof (temp.rule.comment) - 1);
			temp.rule_type = RTYPE_COMMENT;
		    }
		}
		else
		{
		    temp.rule_type = RTYPE_COMMENT_ALL;
		}

		if (fields_num >= 4)
		    strncpy (temp.name, fields[3], sizeof (temp.name) - 1);

		free (value_copy);
		value_copy = NULL;
		table = NULL;
		chain = NULL;

		list = (ip_chain_t **) realloc (chain_list, (chain_num + 1) * sizeof (ip_chain_t *));
		if (list == NULL)
		{
			syslog (LOG_ERR, "realloc failed: %s", strerror (errno));
			return (1);
		}

		chain_list = list;
		final = (ip_chain_t *) malloc( sizeof(temp) );
		if (final == NULL) 
		{
			syslog (LOG_ERR, "malloc failed: %s", strerror (errno));
			return (1);
		}
		memcpy (final, &temp, sizeof (temp));
		chain_list[chain_num] = final;
		chain_num++;

		DBG ("Chain #%i: table = %s; chain = %s;", chain_num, final->table, final->chain);
	}
	else 
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

static void iptables_write (char *host, char *orig_inst, char *val, char *type) 
{
    char *table;
    char *inst;
    char file[256];
    int status;

    table = strdup (orig_inst);
    if (table == NULL)
	return;
    inst = strchr (table, ',');
    if (inst == NULL)
    {
	free (table);
	return;
    }

    *inst = '\0';
    inst++;
    if (*inst == '\0')
    {
	free (table);
	return;
    }

    status = snprintf (file, sizeof (file), "iptables-%s/%s-%s.rrd",
	    table, type, inst);
    free (table);
    if ((status >= sizeof (file)) || (status < 1))
	return;

    rrd_update_file (host, file, val, ds_def, ds_num);
} /* void iptables_write */

static void iptables_write_bytes (char *host, char *inst, char *val)
{
    iptables_write (host, inst, val, "ipt_bytes");
}

static void iptables_write_packets (char *host, char *inst, char *val)
{
    iptables_write (host, inst, val, "ipt_packets");
}

#if IPTABLES_HAVE_READ
static int submit_match (const struct ipt_entry_match *match,
		const struct ipt_entry *entry,
		const ip_chain_t *chain,
		int rule_num) 
{
    char inst[64];
    char value[64];
    int status;

    /* Only log rules that have a comment, although could probably also do
     * numerical targets sometime */
    if (chain->rule_type == RTYPE_NUM)
    {
	if (chain->rule.num != rule_num)
	    return (0);
    }
    else
    {
	if (strcmp (match->u.user.name, "comment") != 0)
	    return 0;
	if ((chain->rule_type == RTYPE_COMMENT)
		&& (strcmp (chain->rule.comment, (char *) match->data) != 0))
	    return (0);
    }

    if (chain->name[0] != '\0')
    {
	status = snprintf (inst, sizeof (inst), "%s-%s,%s",
		chain->table, chain->chain, chain->name);
    }
    else
    {
	if (chain->rule_type == RTYPE_NUM)
	    status = snprintf (inst, sizeof (inst), "%s-%s,%i",
		chain->table, chain->chain, chain->rule.num);
	else
	    status = snprintf (inst, sizeof (inst), "%s-%s,%s",
		chain->table, chain->chain, match->data);

	if ((status >= sizeof (inst)) || (status < 1))
	    return (0);
    }

    status = snprintf (value, sizeof (value), "%u:%lld",
	    (unsigned int) curtime,
	    entry->counters.bcnt);
    if ((status >= sizeof (value)) || (status < 1))
	return 0;
    plugin_submit ("ipt_bytes", inst, value);

    status = snprintf (value, sizeof (value), "%u:%lld",
	    (unsigned int) curtime,
	    entry->counters.pcnt);
    if ((status >= sizeof (value)) || (status < 1))
	return 0;
    plugin_submit ("ipt_packets", inst, value);

    return 0;
} /* int submit_match */

static void submit_chain( iptc_handle_t *handle, ip_chain_t *chain ) {
    const struct ipt_entry *entry;
    int rule_num;

    /* Find first rule for chain and use the iterate macro */    
    entry = iptc_first_rule( chain->chain, handle );
    if (entry == NULL)
    {
	DBG ("iptc_first_rule failed: %s", iptc_strerror (errno));
	return;
    }

    rule_num = 1;
    while (entry)
    {
	if (chain->rule_type == RTYPE_NUM)
	{
	    submit_match (NULL, entry, chain, rule_num);
	}
	else
	{
	    IPT_MATCH_ITERATE( entry, submit_match, entry, chain, rule_num );
	}

	entry = iptc_next_rule( entry, handle );
	rule_num++;
    } /* while (entry) */
}


static void iptables_read (void)
{
    int i;
    static complain_t complaint;

    /* Init the iptc handle structure and query the correct table */    
    for (i = 0; i < chain_num; i++)
    {
	iptc_handle_t handle;
	ip_chain_t *chain;
	
	chain = chain_list[i];
	if (!chain)
	{
	    DBG ("chain == NULL");
	    continue;
	}

	handle = iptc_init( chain->table );
	if (!handle)
	{
	    DBG ("iptc_init (%s) failed: %s", chain->table, iptc_strerror (errno));
	    plugin_complain (LOG_ERR, &complaint, "iptc_init (%s) failed: %s",
		    chain->table, iptc_strerror (errno));
	    continue;
	}
	plugin_relief (LOG_INFO, &complaint, "iptc_init (%s) succeeded",
		chain->table);

	submit_chain (&handle, chain);
	iptc_free (&handle);
    }
}
#else /* !IPTABLES_HAVE_READ */
# define iptables_read NULL
#endif

void module_register (void)
{
    plugin_register ("ipt_bytes", NULL, NULL, iptables_write_bytes);
    plugin_register ("ipt_packets", NULL, NULL, iptables_write_packets);
#if IPTABLES_HAVE_READ
    plugin_register (MODULE_NAME, iptables_init, iptables_read, NULL);
    cf_register (MODULE_NAME, iptables_config, config_keys, config_keys_num);
#endif
}

#undef BUFSIZE
#undef MODULE_NAME

/*
 * vim:shiftwidth=4:softtabstop=4:tabstop=8
 */
