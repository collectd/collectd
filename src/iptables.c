/**
 * collectd - src/iptables.c
 * Copyright (C) 2007 Sjoerd van der Berg
 * Copyright (C) 2007 Florian octo Forster
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
 *  Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#if OWN_LIBIPTC
# include "owniptc/libiptc.h"
#else
# include <libiptc/libiptc.h>
#endif

/*
 * (Module-)Global variables
 */

/*
 * Config format should be `Chain table chainname',
 * e. g. `Chain mangle incoming'
 */
static const char *config_keys[] =
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

static int iptables_config (const char *key, const char *value)
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
		    char errbuf[1024];
		    ERROR ("strdup failed: %s",
			    sstrerror (errno, errbuf, sizeof (errbuf)));
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

		table_len = strlen (table) + 1;
		if ((unsigned int)table_len > sizeof(temp.table))
		{
			ERROR ("Table `%s' too long.", table);
			free (value_copy);
			return (1);
		}
		sstrncpy (temp.table, table, table_len);

		chain_len = strlen (chain) + 1;
		if ((unsigned int)chain_len > sizeof(temp.chain))
		{
			ERROR ("Chain `%s' too long.", chain);
			free (value_copy);
			return (1);
		}
		sstrncpy (temp.chain, chain, chain_len);

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
			temp.rule.comment = strdup (comment);
			if (temp.rule.comment == NULL)
			{
			    free (value_copy);
			    return (1);
			}
			temp.rule_type = RTYPE_COMMENT;
		    }
		}
		else
		{
		    temp.rule_type = RTYPE_COMMENT_ALL;
		}

		if (fields_num >= 4)
		    sstrncpy (temp.name, fields[3], sizeof (temp.name));

		free (value_copy);
		value_copy = NULL;
		table = NULL;
		chain = NULL;

		list = (ip_chain_t **) realloc (chain_list, (chain_num + 1) * sizeof (ip_chain_t *));
		if (list == NULL)
		{
		    char errbuf[1024];
		    ERROR ("realloc failed: %s",
			    sstrerror (errno, errbuf, sizeof (errbuf)));
		    return (1);
		}

		chain_list = list;
		final = (ip_chain_t *) malloc( sizeof(temp) );
		if (final == NULL) 
		{
		    char errbuf[1024];
		    ERROR ("malloc failed: %s",
			    sstrerror (errno, errbuf, sizeof (errbuf)));
		    return (1);
		}
		memcpy (final, &temp, sizeof (temp));
		chain_list[chain_num] = final;
		chain_num++;

		DEBUG ("Chain #%i: table = %s; chain = %s;", chain_num, final->table, final->chain);
	}
	else 
	{
		return (-1);
	}

	return (0);
} /* int iptables_config */

/* This needs to return `int' for IPT_MATCH_ITERATE to work. */
static int submit_match (const struct ipt_entry_match *match,
		const struct ipt_entry *entry,
		const ip_chain_t *chain,
		int rule_num) 
{
    int status;
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    /* Select the rules to collect */
    if (chain->rule_type == RTYPE_NUM)
    {
	if (chain->rule.num != rule_num)
	    return (0);
    }
    else
    {
	if (strcmp (match->u.user.name, "comment") != 0)
	    return (0);
	if ((chain->rule_type == RTYPE_COMMENT)
		&& (strcmp (chain->rule.comment, (char *) match->data) != 0))
	    return (0);
    }

    vl.values = values;
    vl.values_len = 1;
    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "iptables", sizeof (vl.plugin));

    status = ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),
	    "%s-%s", chain->table, chain->chain);
    if ((status < 1) || ((unsigned int)status >= sizeof (vl.plugin_instance)))
	return (0);

    if (chain->name[0] != '\0')
    {
	sstrncpy (vl.type_instance, chain->name, sizeof (vl.type_instance));
    }
    else
    {
	if (chain->rule_type == RTYPE_NUM)
	    ssnprintf (vl.type_instance, sizeof (vl.type_instance),
		    "%i", chain->rule.num);
	else
	    sstrncpy (vl.type_instance, (char *) match->data,
		    sizeof (vl.type_instance));
    }

    sstrncpy (vl.type, "ipt_bytes", sizeof (vl.type));
    values[0].counter = (counter_t) entry->counters.bcnt;
    plugin_dispatch_values (&vl);

    sstrncpy (vl.type, "ipt_packets", sizeof (vl.type));
    values[0].counter = (counter_t) entry->counters.pcnt;
    plugin_dispatch_values (&vl);

    return (0);
} /* void submit_match */

static void submit_chain( iptc_handle_t *handle, ip_chain_t *chain ) {
    const struct ipt_entry *entry;
    int rule_num;

    /* Find first rule for chain and use the iterate macro */    
    entry = iptc_first_rule( chain->chain, handle );
    if (entry == NULL)
    {
	DEBUG ("iptc_first_rule failed: %s", iptc_strerror (errno));
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


static int iptables_read (void)
{
    int i;
    int num_failures = 0;

    /* Init the iptc handle structure and query the correct table */    
    for (i = 0; i < chain_num; i++)
    {
	iptc_handle_t handle;
	ip_chain_t *chain;
	
	chain = chain_list[i];
	if (!chain)
	{
	    DEBUG ("iptables plugin: chain == NULL");
	    continue;
	}

	handle = iptc_init (chain->table);
	if (!handle)
	{
	    ERROR ("iptables plugin: iptc_init (%s) failed: %s",
		    chain->table, iptc_strerror (errno));
	    num_failures++;
	    continue;
	}

	submit_chain (&handle, chain);
	iptc_free (&handle);
    } /* for (i = 0 .. chain_num) */

    return ((num_failures < chain_num) ? 0 : -1);
} /* int iptables_read */

static int iptables_shutdown (void)
{
    int i;

    for (i = 0; i < chain_num; i++)
    {
	if ((chain_list[i] != NULL) && (chain_list[i]->rule_type == RTYPE_COMMENT))
	{
	    sfree (chain_list[i]->rule.comment);
	}
	sfree (chain_list[i]);
    }
    sfree (chain_list);

    return (0);
} /* int iptables_shutdown */

void module_register (void)
{
    plugin_register_config ("iptables", iptables_config,
	    config_keys, config_keys_num);
    plugin_register_read ("iptables", iptables_read);
    plugin_register_shutdown ("iptables", iptables_shutdown);
} /* void module_register */

/*
 * vim:shiftwidth=4:softtabstop=4:tabstop=8
 */
