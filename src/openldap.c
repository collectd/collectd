/**
 * collectd - src/openldap.c
 * Copyright (C) 2011       Kimo Rosenbaum
 * Copyright (C) 2014       Marc Fournier
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Kimo Rosenbaum <kimor79 at yahoo.com>
 *   Marc Fournier <marc.fournier at camptocamp.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <lber.h>
#include <ldap.h>

struct cldap_s /* {{{ */
{
	char *name;

	char *cacert;
	char *host;
	int   state;
	_Bool starttls;
	int   timeout;
	char *url;
	_Bool verifyhost;
	int   version;

	LDAP *ld;
};
typedef struct cldap_s cldap_t; /* }}} */

static void cldap_free (cldap_t *st) /* {{{ */
{
	if (st == NULL)
		return;

	sfree (st->cacert);
	sfree (st->host);
	sfree (st->name);
	sfree (st->url);
	if (st->ld)
		ldap_memfree (st->ld);
	sfree (st);
} /* }}} void cldap_free */

/* initialize ldap for each host */
static int cldap_init_host (cldap_t *st) /* {{{ */
{
	LDAP *ld;
	int rc;
	rc = ldap_initialize (&ld, st->url);
	if (rc != LDAP_SUCCESS)
	{
		ERROR ("openldap plugin: ldap_initialize failed: %s",
			ldap_err2string (rc));
		st->state = 0;
		ldap_unbind_ext_s (ld, NULL, NULL);
		return (-1);
	}

	st->ld = ld;

	ldap_set_option (st->ld, LDAP_OPT_PROTOCOL_VERSION, &st->version);

	ldap_set_option (st->ld, LDAP_OPT_TIMEOUT,
		&(const struct timeval){st->timeout, 0});

	if (st->cacert != NULL)
		ldap_set_option (st->ld, LDAP_OPT_X_TLS_CACERTFILE, st->cacert);

	if (st->verifyhost == 0)
	{
		int never = LDAP_OPT_X_TLS_NEVER;
		ldap_set_option (st->ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &never);
	}

	if (st->starttls != 0)
	{
		rc = ldap_start_tls_s (ld, NULL, NULL);
		if (rc != LDAP_SUCCESS)
		{
			ERROR ("openldap plugin: Failed to start tls on %s: %s",
					st->url, ldap_err2string (rc));
			st->state = 0;
			ldap_unbind_ext_s (st->ld, NULL, NULL);
			return (-1);
		}
	}

	struct berval cred;
	cred.bv_val = "";
	cred.bv_len = 0;

	rc = ldap_sasl_bind_s (st->ld, NULL, NULL, &cred, NULL, NULL, NULL);
	if (rc != LDAP_SUCCESS)
	{
		ERROR ("openldap plugin: Failed to bind to %s: %s",
				st->url, ldap_err2string (rc));
		st->state = 0;
		ldap_unbind_ext_s (st->ld, NULL, NULL);
		return (-1);
	}
	else
	{
		DEBUG ("openldap plugin: Successfully connected to %s",
				st->url);
		st->state = 1;
		return (0);
	}
} /* }}} static cldap_init_host */

static void cldap_submit_value (const char *type, const char *type_instance, /* {{{ */
		value_t value, cldap_t *st)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values     = &value;
	vl.values_len = 1;

	if ((st->host == NULL)
			|| (strcmp ("", st->host) == 0)
			|| (strcmp ("localhost", st->host) == 0))
	{
		sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	}
	else
	{
		sstrncpy (vl.host, st->host, sizeof (vl.host));
	}

	sstrncpy (vl.plugin, "openldap", sizeof (vl.plugin));
	if (st->name != NULL)
		sstrncpy (vl.plugin_instance, st->name,
				sizeof (vl.plugin_instance));

	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void cldap_submit_value */

static void cldap_submit_derive (const char *type, const char *type_instance, /* {{{ */
		derive_t d, cldap_t *st)
{
	value_t v;
	v.derive = d;
	cldap_submit_value (type, type_instance, v, st);
} /* }}} void cldap_submit_derive */

static void cldap_submit_gauge (const char *type, const char *type_instance, /* {{{ */
		gauge_t g, cldap_t *st)
{
	value_t v;
	v.gauge = g;
	cldap_submit_value (type, type_instance, v, st);
} /* }}} void cldap_submit_gauge */

static int cldap_read_host (user_data_t *ud) /* {{{ */
{
	cldap_t *st;
	LDAPMessage *e, *result;
	char *dn;
	int rc;
	int status;

	char *attrs[9] = { "monitorCounter",
				"monitorOpCompleted",
				"monitorOpInitiated",
				"monitoredInfo",
				"olmBDBEntryCache",
				"olmBDBDNCache",
				"olmBDBIDLCache",
				"namingContexts",
				NULL };

	if ((ud == NULL) || (ud->data == NULL))
	{
		ERROR ("openldap plugin: cldap_read_host: Invalid user data.");
		return (-1);
	}

	st = (cldap_t *) ud->data;

	status = cldap_init_host (st);
	if (status != 0)
		return (-1);

	rc = ldap_search_ext_s (st->ld, "cn=Monitor", LDAP_SCOPE_SUBTREE,
		"(|(!(cn=* *))(cn=Database*))", attrs, 0,
		NULL, NULL, NULL, 0, &result);

	if (rc != LDAP_SUCCESS)
	{
		ERROR ("openldap plugin: Failed to execute search: %s",
				ldap_err2string (rc));
		ldap_msgfree (result);
		ldap_unbind_ext_s (st->ld, NULL, NULL);
		return (-1);
	}

	for (e = ldap_first_entry (st->ld, result); e != NULL;
		e = ldap_next_entry (st->ld, e))
	{
		if ((dn = ldap_get_dn (st->ld, e)) != NULL)
		{
			unsigned long long counter = 0;
			unsigned long long opc = 0;
			unsigned long long opi = 0;
			unsigned long long info = 0;

			struct berval counter_data;
			struct berval opc_data;
			struct berval opi_data;
			struct berval info_data;
			struct berval olmbdb_data;
			struct berval nc_data;

			struct berval **counter_list;
			struct berval **opc_list;
			struct berval **opi_list;
			struct berval **info_list;
			struct berval **olmbdb_list;
			struct berval **nc_list;

			if ((counter_list = ldap_get_values_len (st->ld, e,
				"monitorCounter")) != NULL)
			{
				counter_data = *counter_list[0];
				counter = atoll (counter_data.bv_val);
			}

			if ((opc_list = ldap_get_values_len (st->ld, e,
				"monitorOpCompleted")) != NULL)
			{
				opc_data = *opc_list[0];
				opc = atoll (opc_data.bv_val);
			}

			if ((opi_list = ldap_get_values_len (st->ld, e,
				"monitorOpInitiated")) != NULL)
			{
				opi_data = *opi_list[0];
				opi = atoll (opi_data.bv_val);
			}

			if ((info_list = ldap_get_values_len (st->ld, e,
				"monitoredInfo")) != NULL)
			{
				info_data = *info_list[0];
				info = atoll (info_data.bv_val);
			}

			if (strcmp (dn, "cn=Total,cn=Connections,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("total_connections", NULL,
					counter, st);
			}
			else if (strcmp (dn,
					"cn=Current,cn=Connections,cn=Monitor")
					== 0)
			{
				cldap_submit_gauge ("current_connections", NULL,
					counter, st);
			}
			else if (strcmp (dn,
					"cn=Operations,cn=Monitor") == 0)
			{
				cldap_submit_derive ("operations",
					"completed", opc, st);
				cldap_submit_derive ("operations",
					"initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Bind,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"bind-completed", opc, st);
				cldap_submit_derive ("operations",
					"bind-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=UnBind,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"unbind-completed", opc, st);
				cldap_submit_derive ("operations",
					"unbind-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Search,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"search-completed", opc, st);
				cldap_submit_derive ("operations",
					"search-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Compare,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"compare-completed", opc, st);
				cldap_submit_derive ("operations",
					"compare-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Modify,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"modify-completed", opc, st);
				cldap_submit_derive ("operations",
					"modify-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Modrdn,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"modrdn-completed", opc, st);
				cldap_submit_derive ("operations",
					"modrdn-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Add,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"add-completed", opc, st);
				cldap_submit_derive ("operations",
					"add-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Delete,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"delete-completed", opc, st);
				cldap_submit_derive ("operations",
					"delete-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Abandon,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"abandon-completed", opc, st);
				cldap_submit_derive ("operations",
					"abandon-initiated", opi, st);
			}
			else if (strcmp (dn,
					"cn=Extended,cn=Operations,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("operations",
					"extended-completed", opc, st);
				cldap_submit_derive ("operations",
					"extended-initiated", opi, st);
			}
			else if ((strncmp (dn, "cn=Database", 11) == 0)
				&& ((nc_list = ldap_get_values_len
						(st->ld, e, "namingContexts")) != NULL))
			{
				nc_data = *nc_list[0];
				char typeinst[DATA_MAX_NAME_LEN];

				if ((olmbdb_list = ldap_get_values_len (st->ld, e,
					"olmBDBEntryCache")) != NULL)
				{
					olmbdb_data = *olmbdb_list[0];
					ssnprintf (typeinst, sizeof (typeinst),
						"bdbentrycache-%s", nc_data.bv_val);
					cldap_submit_gauge ("cache_size", typeinst,
						atoll (olmbdb_data.bv_val), st);
					ldap_value_free_len (olmbdb_list);
				}

				if ((olmbdb_list = ldap_get_values_len (st->ld, e,
					"olmBDBDNCache")) != NULL)
				{
					olmbdb_data = *olmbdb_list[0];
					ssnprintf (typeinst, sizeof (typeinst),
						"bdbdncache-%s", nc_data.bv_val);
					cldap_submit_gauge ("cache_size", typeinst,
						atoll (olmbdb_data.bv_val), st);
					ldap_value_free_len (olmbdb_list);
				}

				if ((olmbdb_list = ldap_get_values_len (st->ld, e,
					"olmBDBIDLCache")) != NULL)
				{
					olmbdb_data = *olmbdb_list[0];
					ssnprintf (typeinst, sizeof (typeinst),
						"bdbidlcache-%s", nc_data.bv_val);
					cldap_submit_gauge ("cache_size", typeinst,
						atoll (olmbdb_data.bv_val), st);
					ldap_value_free_len (olmbdb_list);
				}

				ldap_value_free_len (nc_list);
			}
			else if (strcmp (dn,
					"cn=Bytes,cn=Statistics,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("derive", "statistics-bytes",
					counter, st);
			}
			else if (strcmp (dn,
					"cn=PDU,cn=Statistics,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("derive", "statistics-pdu",
					counter, st);
			}
			else if (strcmp (dn,
					"cn=Entries,cn=Statistics,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("derive", "statistics-entries",
					counter, st);
			}
			else if (strcmp (dn,
					"cn=Referrals,cn=Statistics,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("derive", "statistics-referrals",
					counter, st);
			}
			else if (strcmp (dn,
					"cn=Open,cn=Threads,cn=Monitor")
					== 0)
			{
				cldap_submit_gauge ("threads", "threads-open",
					info, st);
			}
			else if (strcmp (dn,
					"cn=Starting,cn=Threads,cn=Monitor")
					== 0)
			{
				cldap_submit_gauge ("threads", "threads-starting",
					info, st);
			}
			else if (strcmp (dn,
					"cn=Active,cn=Threads,cn=Monitor")
					== 0)
			{
				cldap_submit_gauge ("threads", "threads-active",
					info, st);
			}
			else if (strcmp (dn,
					"cn=Pending,cn=Threads,cn=Monitor")
					== 0)
			{
				cldap_submit_gauge ("threads", "threads-pending",
					info, st);
			}
			else if (strcmp (dn,
					"cn=Backload,cn=Threads,cn=Monitor")
					== 0)
			{
				cldap_submit_gauge ("threads", "threads-backload",
					info, st);
			}
			else if (strcmp (dn,
					"cn=Read,cn=Waiters,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("derive", "waiters-read",
					counter, st);
			}
			else if (strcmp (dn,
					"cn=Write,cn=Waiters,cn=Monitor")
					== 0)
			{
				cldap_submit_derive ("derive", "waiters-write",
					counter, st);
			}

			ldap_value_free_len (counter_list);
			ldap_value_free_len (opc_list);
			ldap_value_free_len (opi_list);
			ldap_value_free_len (info_list);
		}

		ldap_memfree (dn);
	}

	ldap_msgfree (result);
	ldap_unbind_ext_s (st->ld, NULL, NULL);
	return (0);
} /* }}} int cldap_read_host */

/* Configuration handling functions {{{
 *
 * <Plugin ldap>
 *   <Instance "plugin_instance1">
 *     URL "ldap://localhost"
 *     ...
 *   </Instance>
 * </Plugin>
 */

static int cldap_config_add (oconfig_item_t *ci) /* {{{ */
{
	cldap_t *st;
	int i;
	int status;

	st = malloc (sizeof (*st));
	if (st == NULL)
	{
		ERROR ("openldap plugin: malloc failed.");
		return (-1);
	}
	memset (st, 0, sizeof (*st));

	status = cf_util_get_string (ci, &st->name);
	if (status != 0)
	{
		sfree (st);
		return (status);
	}

	st->starttls = 0;
	st->timeout = -1;
	st->verifyhost = 1;
	st->version = LDAP_VERSION3;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("CACert", child->key) == 0)
			status = cf_util_get_string (child, &st->cacert);
		else if (strcasecmp ("StartTLS", child->key) == 0)
			status = cf_util_get_boolean (child, &st->starttls);
		else if (strcasecmp ("Timeout", child->key) == 0)
			status = cf_util_get_int (child, &st->timeout);
		else if (strcasecmp ("URL", child->key) == 0)
			status = cf_util_get_string (child, &st->url);
		else if (strcasecmp ("VerifyHost", child->key) == 0)
			status = cf_util_get_boolean (child, &st->verifyhost);
		else if (strcasecmp ("Version", child->key) == 0)
			status = cf_util_get_int (child, &st->version);
		else
		{
			WARNING ("openldap plugin: Option `%s' not allowed here.",
					child->key);
			status = -1;
		}

		if (status != 0)
			break;
	}

	/* Check if struct is complete.. */
	if ((status == 0) && (st->url == NULL))
	{
		ERROR ("openldap plugin: Instance `%s': "
				"No URL has been configured.",
				st->name);
		status = -1;
	}

	/* Check if URL is valid */
	if ((status == 0) && (st->url != NULL))
	{
		LDAPURLDesc *ludpp;
		int rc;

		if ((rc = ldap_url_parse (st->url, &ludpp)) != 0)
		{
			ERROR ("openldap plugin: Instance `%s': "
				"Invalid URL: `%s'",
				st->name, st->url);
			status = -1;
		}
		else
		{
			st->host = strdup (ludpp->lud_host);
		}

		ldap_free_urldesc (ludpp);
	}

	if (status == 0)
	{
		user_data_t ud;
		char callback_name[3*DATA_MAX_NAME_LEN];

		memset (&ud, 0, sizeof (ud));
		ud.data = st;

		memset (callback_name, 0, sizeof (callback_name));
		ssnprintf (callback_name, sizeof (callback_name),
				"openldap/%s/%s",
				(st->host != NULL) ? st->host : hostname_g,
				(st->name != NULL) ? st->name : "default"),

		status = plugin_register_complex_read (/* group = */ NULL,
				/* name      = */ callback_name,
				/* callback  = */ cldap_read_host,
				/* interval  = */ NULL,
				/* user_data = */ &ud);
	}

	if (status != 0)
	{
		cldap_free (st);
		return (-1);
	}

	return (0);
} /* }}} int cldap_config_add */

static int cldap_config (oconfig_item_t *ci) /* {{{ */
{
	int i;
	int status = 0;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Instance", child->key) == 0)
			cldap_config_add (child);
		else
			WARNING ("openldap plugin: The configuration option "
					"\"%s\" is not allowed here. Did you "
					"forget to add an <Instance /> block "
					"around the configuration?",
					child->key);
	} /* for (ci->children) */

	return (status);
} /* }}} int cldap_config */

/* }}} End of configuration handling functions */

static int cldap_init (void) /* {{{ */
{
	/* Initialize LDAP library while still single-threaded as recommended in
	 * ldap_initialize(3) */
	int debug_level;
	ldap_get_option (NULL, LDAP_OPT_DEBUG_LEVEL, &debug_level);
	return (0);
} /* }}} int cldap_init */

void module_register (void) /* {{{ */
{
	plugin_register_complex_config ("openldap", cldap_config);
	plugin_register_init ("openldap", cldap_init);
} /* }}} void module_register */
