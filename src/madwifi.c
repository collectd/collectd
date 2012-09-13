/**
 * collectd - src/madwifi.c
 * Copyright (C) 2009  Ondrej 'SanTiago' Zajicek
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
 * Author:
 *   Ondrej 'SanTiago' Zajicek <santiago@crfreenet.org>
 *
 *   based on some code from interfaces.c (collectd) and Madwifi driver
 **/


/**
 * There are several data streams provided by Madwifi plugin, some are 
 * connected to network interface, some are connected to each node
 * associated to that interface. Nodes represents other sides in
 * wireless communication, for example on network interface in AP mode,
 * there is one node for each associated station. Node data streams
 * contain MAC address of the node as the last part  of the type_instance
 * field.
 *
 * Inteface data streams:
 *	ath_nodes	The number of associated nodes
 *	ath_stat	Device statistic counters
 *
 * Node data streams:
 *	node_octets	RX and TX data count (octets/bytes)
 *	node_rssi	Received RSSI of the node
 *	node_tx_rate	Reported TX rate to that node
 *	node_stat	Node statistic counters
 *
 * Both statistic counters have type instances for each counter returned
 * by Madwifi. See madwifi.h for content of ieee80211_nodestats, 
 * ieee80211_stats and ath_stats structures. Type instances use the same
 * name as fields in these structures (like ns_rx_dup). Some fields are
 * not reported, because they are not counters (like ns_tx_deauth_code
 * or ast_tx_rssi). Fields ns_rx_bytes and ns_tx_bytes are reported as
 * node_octets data stream instead of type instance of node_stat.
 * Statistics are not logged when they are zero.
 * 
 * There are two sets of these counters - the first 'WatchList' is a
 * set of counters that are individually logged. The second 'MiscList'
 * is a set of counters that are summed together and the sum is logged.
 * By default, the most important statistics are in the WatchList and 
 * many error statistics are in MiscList. There are also many statistics
 * that are not in any of these sets, so they are not monitored by default.
 * It is possible to alter these lists using configuration options:
 *
 *	WatchAdd X	Adds X to WachList
 *	WatchRemove X	Removes X from WachList
 *	WatchSet All	Adds all statistics to WatchList
 *	WatchSet None	Removes all statistics from WachList
 *
 * There are also Misc* variants fo these options, they modifies MiscList
 * instead of WatchList.
 *
 * Example:
 *
 *	WatchSet None
 *	WatchAdd node_octets
 *	WatchAdd node_rssi
 *	WatchAdd is_rx_acl
 *	WatchAdd is_scan_active
 *
 * That causes that just the four mentioned data streams are logged.
 *
 *
 * By default, madwifi plugin enumerates network interfaces using /sys
 * filesystem. Configuration option `Source' can change this to use
 * /proc filesystem (which is useful for example when running on Linux
 * 2.4). But without /sys filesystem, Madwifi plugin cannot check whether
 * given interface is madwifi interface and there are private ioctls used,
 * which may do something completely different on non-madwifi devices.
 * Therefore, the /proc filesystem should always be used together with option
 * `Interface', to limit found interfaces to madwifi interfaces only.
 **/


#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"

#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#include <linux/wireless.h>
#include "madwifi.h"



struct stat_spec {
	uint16_t flags;
	uint16_t offset;
	const char *name;
};


#define OFFSETOF(s, i) ((size_t)&((s *)0)->i)

#define FLAG(i)  (((uint32_t) 1) << ((i) % 32))

#define SPC_STAT 0
#define NOD_STAT 1
#define IFA_STAT 2
#define ATH_STAT 3
#define SRC_MASK 3

/* By default, the item is disabled */
#define D 0

/* By default, the item is logged */
#define LOG 4

/* By default, the item is summed with other such items and logged together */
#define SU 8

#define SS_STAT(flags, name) { flags | SPC_STAT, 0, #name }
#define NS_STAT(flags, name) { flags | NOD_STAT, OFFSETOF(struct ieee80211_nodestats, name), #name }
#define IS_STAT(flags, name) { flags | IFA_STAT, OFFSETOF(struct ieee80211_stats, name), #name }
#define AS_STAT(flags, name) { flags | ATH_STAT, OFFSETOF(struct ath_stats, name), #name }


/*
 * (Module-)Global variables
 */

/* Indices of special stats in specs array */
#define STAT_NODE_OCTETS	0
#define STAT_NODE_RSSI		1
#define STAT_NODE_TX_RATE	2
#define STAT_ATH_NODES		3
#define STAT_NS_RX_BEACONS	4
#define STAT_AST_ANT_RX		5
#define STAT_AST_ANT_TX		6

static struct stat_spec specs[] = {

/* Special statistics */
SS_STAT(LOG, node_octets),		/* rx and tx data count (bytes) */
SS_STAT(LOG, node_rssi),		/* received RSSI of the node */
SS_STAT(LOG, node_tx_rate),		/* used tx rate to the node */
SS_STAT(LOG, ath_nodes),		/* the number of associated nodes */
SS_STAT(D,   ns_rx_beacons),		/* rx beacon frames */
SS_STAT(LOG, ast_ant_rx),		/* rx frames with antenna */
SS_STAT(LOG, ast_ant_tx),		/* tx frames with antenna */

/* Node statistics */
NS_STAT(LOG, ns_rx_data),		/* rx data frames */
NS_STAT(LOG, ns_rx_mgmt),		/* rx management frames */
NS_STAT(LOG, ns_rx_ctrl),		/* rx control frames */
NS_STAT(D,   ns_rx_ucast),		/* rx unicast frames */
NS_STAT(D,   ns_rx_mcast),		/* rx multi/broadcast frames */
NS_STAT(D,   ns_rx_proberesp),		/* rx probe response frames */
NS_STAT(LOG, ns_rx_dup),		/* rx discard because it's a dup */
NS_STAT(SU,  ns_rx_noprivacy),		/* rx w/ wep but privacy off */
NS_STAT(SU,  ns_rx_wepfail),		/* rx wep processing failed */
NS_STAT(SU,  ns_rx_demicfail),		/* rx demic failed */
NS_STAT(SU,  ns_rx_decap),		/* rx decapsulation failed */
NS_STAT(SU,  ns_rx_defrag),		/* rx defragmentation failed */
NS_STAT(D,   ns_rx_disassoc),		/* rx disassociation */
NS_STAT(D,   ns_rx_deauth),		/* rx deauthentication */
NS_STAT(SU,  ns_rx_decryptcrc),		/* rx decrypt failed on crc */
NS_STAT(SU,  ns_rx_unauth),		/* rx on unauthorized port */
NS_STAT(SU,  ns_rx_unencrypted),	/* rx unecrypted w/ privacy */
NS_STAT(LOG, ns_tx_data),		/* tx data frames */
NS_STAT(LOG, ns_tx_mgmt),		/* tx management frames */
NS_STAT(D,   ns_tx_ucast),		/* tx unicast frames */
NS_STAT(D,   ns_tx_mcast),		/* tx multi/broadcast frames */
NS_STAT(D,   ns_tx_probereq),		/* tx probe request frames */
NS_STAT(D,   ns_tx_uapsd),		/* tx on uapsd queue */
NS_STAT(SU,  ns_tx_novlantag),		/* tx discard due to no tag */
NS_STAT(SU,  ns_tx_vlanmismatch),	/* tx discard due to of bad tag */
NS_STAT(D,   ns_tx_eosplost),		/* uapsd EOSP retried out */
NS_STAT(D,   ns_ps_discard),		/* ps discard due to of age */
NS_STAT(D,   ns_uapsd_triggers),	/* uapsd triggers */
NS_STAT(LOG, ns_tx_assoc),		/* [re]associations */
NS_STAT(LOG, ns_tx_auth),		/* [re]authentications */
NS_STAT(D,   ns_tx_deauth),		/* deauthentications */
NS_STAT(D,   ns_tx_disassoc),		/* disassociations */
NS_STAT(D,   ns_psq_drops),		/* power save queue drops */

/* Iface statistics */
IS_STAT(SU,  is_rx_badversion),		/* rx frame with bad version */
IS_STAT(SU,  is_rx_tooshort),		/* rx frame too short */
IS_STAT(LOG, is_rx_wrongbss),		/* rx from wrong bssid */
IS_STAT(LOG, is_rx_dup),		/* rx discard due to it's a dup */
IS_STAT(SU,  is_rx_wrongdir),		/* rx w/ wrong direction */
IS_STAT(D,   is_rx_mcastecho),		/* rx discard due to of mcast echo */
IS_STAT(SU,  is_rx_notassoc),		/* rx discard due to sta !assoc */
IS_STAT(SU,  is_rx_noprivacy),		/* rx w/ wep but privacy off */
IS_STAT(SU,  is_rx_unencrypted),	/* rx w/o wep and privacy on */
IS_STAT(SU,  is_rx_wepfail),		/* rx wep processing failed */
IS_STAT(SU,  is_rx_decap),		/* rx decapsulation failed */
IS_STAT(D,   is_rx_mgtdiscard),		/* rx discard mgt frames */
IS_STAT(D,   is_rx_ctl),		/* rx discard ctrl frames */
IS_STAT(D,   is_rx_beacon),		/* rx beacon frames */
IS_STAT(D,   is_rx_rstoobig),		/* rx rate set truncated */
IS_STAT(SU,  is_rx_elem_missing),	/* rx required element missing*/
IS_STAT(SU,  is_rx_elem_toobig),	/* rx element too big */
IS_STAT(SU,  is_rx_elem_toosmall),	/* rx element too small */
IS_STAT(LOG, is_rx_elem_unknown),	/* rx element unknown */
IS_STAT(SU,  is_rx_badchan),		/* rx frame w/ invalid chan */
IS_STAT(SU,  is_rx_chanmismatch),	/* rx frame chan mismatch */
IS_STAT(SU,  is_rx_nodealloc),		/* rx frame dropped */
IS_STAT(LOG, is_rx_ssidmismatch),	/* rx frame ssid mismatch  */
IS_STAT(SU,  is_rx_auth_unsupported),	/* rx w/ unsupported auth alg */
IS_STAT(SU,  is_rx_auth_fail),		/* rx sta auth failure */
IS_STAT(SU,  is_rx_auth_countermeasures),/* rx auth discard due to CM */
IS_STAT(SU,  is_rx_assoc_bss),		/* rx assoc from wrong bssid */
IS_STAT(SU,  is_rx_assoc_notauth),	/* rx assoc w/o auth */
IS_STAT(SU,  is_rx_assoc_capmismatch),	/* rx assoc w/ cap mismatch */
IS_STAT(SU,  is_rx_assoc_norate),	/* rx assoc w/ no rate match */
IS_STAT(SU,  is_rx_assoc_badwpaie),	/* rx assoc w/ bad WPA IE */
IS_STAT(LOG, is_rx_deauth),		/* rx deauthentication */
IS_STAT(LOG, is_rx_disassoc),		/* rx disassociation */
IS_STAT(SU,  is_rx_badsubtype),		/* rx frame w/ unknown subtype*/
IS_STAT(SU,  is_rx_nobuf),		/* rx failed for lack of buf */
IS_STAT(SU,  is_rx_decryptcrc),		/* rx decrypt failed on crc */
IS_STAT(D,   is_rx_ahdemo_mgt),		/* rx discard ahdemo mgt frame*/
IS_STAT(SU,  is_rx_bad_auth),		/* rx bad auth request */
IS_STAT(SU,  is_rx_unauth),		/* rx on unauthorized port */
IS_STAT(SU,  is_rx_badkeyid),		/* rx w/ incorrect keyid */
IS_STAT(D,   is_rx_ccmpreplay),		/* rx seq# violation (CCMP), */
IS_STAT(D,   is_rx_ccmpformat),		/* rx format bad (CCMP), */
IS_STAT(D,   is_rx_ccmpmic),		/* rx MIC check failed (CCMP), */
IS_STAT(D,   is_rx_tkipreplay),		/* rx seq# violation (TKIP), */
IS_STAT(D,   is_rx_tkipformat),		/* rx format bad (TKIP), */
IS_STAT(D,   is_rx_tkipmic),		/* rx MIC check failed (TKIP), */
IS_STAT(D,   is_rx_tkipicv),		/* rx ICV check failed (TKIP), */
IS_STAT(D,   is_rx_badcipher),		/* rx failed due to of key type */
IS_STAT(D,   is_rx_nocipherctx),	/* rx failed due to key !setup */
IS_STAT(D,   is_rx_acl),		/* rx discard due to of acl policy */
IS_STAT(D,   is_rx_ffcnt),		/* rx fast frames */
IS_STAT(SU,  is_rx_badathtnl),		/* driver key alloc failed */
IS_STAT(SU,  is_tx_nobuf),		/* tx failed for lack of buf */
IS_STAT(SU,  is_tx_nonode),		/* tx failed for no node */
IS_STAT(SU,  is_tx_unknownmgt),		/* tx of unknown mgt frame */
IS_STAT(SU,  is_tx_badcipher),		/* tx failed due to of key type */
IS_STAT(SU,  is_tx_nodefkey),		/* tx failed due to no defkey */
IS_STAT(SU,  is_tx_noheadroom),		/* tx failed due to no space */
IS_STAT(D,   is_tx_ffokcnt),		/* tx fast frames sent success */
IS_STAT(D,   is_tx_fferrcnt),		/* tx fast frames sent success */
IS_STAT(D,   is_scan_active),		/* active scans started */
IS_STAT(D,   is_scan_passive),		/* passive scans started */
IS_STAT(D,   is_node_timeout),		/* nodes timed out inactivity */
IS_STAT(D,   is_crypto_nomem),		/* no memory for crypto ctx */
IS_STAT(D,   is_crypto_tkip),		/* tkip crypto done in s/w */
IS_STAT(D,   is_crypto_tkipenmic),	/* tkip en-MIC done in s/w */
IS_STAT(D,   is_crypto_tkipdemic),	/* tkip de-MIC done in s/w */
IS_STAT(D,   is_crypto_tkipcm),		/* tkip counter measures */
IS_STAT(D,   is_crypto_ccmp),		/* ccmp crypto done in s/w */
IS_STAT(D,   is_crypto_wep),		/* wep crypto done in s/w */
IS_STAT(D,   is_crypto_setkey_cipher),	/* cipher rejected key */
IS_STAT(D,   is_crypto_setkey_nokey),	/* no key index for setkey */
IS_STAT(D,   is_crypto_delkey),		/* driver key delete failed */
IS_STAT(D,   is_crypto_badcipher),	/* unknown cipher */
IS_STAT(D,   is_crypto_nocipher),	/* cipher not available */
IS_STAT(D,   is_crypto_attachfail),	/* cipher attach failed */
IS_STAT(D,   is_crypto_swfallback),	/* cipher fallback to s/w */
IS_STAT(D,   is_crypto_keyfail),	/* driver key alloc failed */
IS_STAT(D,   is_crypto_enmicfail),	/* en-MIC failed */
IS_STAT(SU,  is_ibss_capmismatch),	/* merge failed-cap mismatch */
IS_STAT(SU,  is_ibss_norate),		/* merge failed-rate mismatch */
IS_STAT(D,   is_ps_unassoc),		/* ps-poll for unassoc. sta */
IS_STAT(D,   is_ps_badaid),		/* ps-poll w/ incorrect aid */
IS_STAT(D,   is_ps_qempty),		/* ps-poll w/ nothing to send */

/* Atheros statistics */
AS_STAT(D,   ast_watchdog),		/* device reset by watchdog */
AS_STAT(D,   ast_hardware),		/* fatal hardware error interrupts */
AS_STAT(D,   ast_bmiss),		/* beacon miss interrupts */
AS_STAT(D,   ast_rxorn),		/* rx overrun interrupts */
AS_STAT(D,   ast_rxeol),		/* rx eol interrupts */
AS_STAT(D,   ast_txurn),		/* tx underrun interrupts */
AS_STAT(D,   ast_mib),			/* mib interrupts */
AS_STAT(D,   ast_tx_packets),		/* packet sent on the interface */
AS_STAT(D,   ast_tx_mgmt),		/* management frames transmitted */
AS_STAT(LOG, ast_tx_discard),		/* frames discarded prior to assoc */
AS_STAT(SU,  ast_tx_invalid),		/* frames discarded due to is device gone */
AS_STAT(SU,  ast_tx_qstop),		/* tx queue stopped because it's full */
AS_STAT(SU,  ast_tx_encap),		/* tx encapsulation failed */
AS_STAT(SU,  ast_tx_nonode),		/* tx failed due to of no node */
AS_STAT(SU,  ast_tx_nobuf),		/* tx failed due to of no tx buffer (data), */
AS_STAT(SU,  ast_tx_nobufmgt),		/* tx failed due to of no tx buffer (mgmt),*/
AS_STAT(LOG, ast_tx_xretries),		/* tx failed due to of too many retries */
AS_STAT(SU,  ast_tx_fifoerr),		/* tx failed due to of FIFO underrun */
AS_STAT(SU,  ast_tx_filtered),		/* tx failed due to xmit filtered */
AS_STAT(LOG, ast_tx_shortretry),	/* tx on-chip retries (short), */
AS_STAT(LOG, ast_tx_longretry),		/* tx on-chip retries (long), */
AS_STAT(SU,  ast_tx_badrate),		/* tx failed due to of bogus xmit rate */
AS_STAT(D,   ast_tx_noack),		/* tx frames with no ack marked */
AS_STAT(D,   ast_tx_rts),		/* tx frames with rts enabled */
AS_STAT(D,   ast_tx_cts),		/* tx frames with cts enabled */
AS_STAT(D,   ast_tx_shortpre),		/* tx frames with short preamble */
AS_STAT(LOG, ast_tx_altrate),		/* tx frames with alternate rate */
AS_STAT(D,   ast_tx_protect),		/* tx frames with protection */
AS_STAT(SU,  ast_rx_orn),		/* rx failed due to of desc overrun */
AS_STAT(LOG, ast_rx_crcerr),		/* rx failed due to of bad CRC */
AS_STAT(SU,  ast_rx_fifoerr),		/* rx failed due to of FIFO overrun */
AS_STAT(SU,  ast_rx_badcrypt),		/* rx failed due to of decryption */
AS_STAT(SU,  ast_rx_badmic),		/* rx failed due to of MIC failure */
AS_STAT(LOG, ast_rx_phyerr),		/* rx PHY error summary count */
AS_STAT(SU,  ast_rx_tooshort),		/* rx discarded due to frame too short */
AS_STAT(SU,  ast_rx_toobig),		/* rx discarded due to frame too large */
AS_STAT(SU,  ast_rx_nobuf),		/* rx setup failed due to of no skbuff */
AS_STAT(D,   ast_rx_packets),		/* packet recv on the interface */
AS_STAT(D,   ast_rx_mgt),		/* management frames received */
AS_STAT(D,   ast_rx_ctl),		/* control frames received */
AS_STAT(D,   ast_be_xmit),		/* beacons transmitted */
AS_STAT(SU,  ast_be_nobuf),		/* no skbuff available for beacon */
AS_STAT(D,   ast_per_cal),		/* periodic calibration calls */
AS_STAT(D,   ast_per_calfail),		/* periodic calibration failed */
AS_STAT(D,   ast_per_rfgain),		/* periodic calibration rfgain reset */
AS_STAT(D,   ast_rate_calls),		/* rate control checks */
AS_STAT(D,   ast_rate_raise),		/* rate control raised xmit rate */
AS_STAT(D,   ast_rate_drop),		/* rate control dropped xmit rate */
AS_STAT(D,   ast_ant_defswitch),	/* rx/default antenna switches */
AS_STAT(D,   ast_ant_txswitch)		/* tx antenna switches */
};

/* Bounds between SS, NS, IS and AS stats in stats array */
static int bounds[4];

#define WL_LEN 6
/* Bitmasks for logged and error items */
static uint32_t watch_items[WL_LEN];
static uint32_t misc_items[WL_LEN];


static const char *config_keys[] =
{
	"Interface",
	"IgnoreSelected",
	"Source",
	"WatchAdd",
	"WatchRemove",
	"WatchSet",
	"MiscAdd",
	"MiscRemove",
	"MiscSet"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;

static int use_sysfs = 1;
static int init_state = 0;

static inline int item_watched(int i)
{
	assert (i >= 0);
	assert (i < ((STATIC_ARRAY_SIZE (watch_items) + 1) * 32));
	return watch_items[i / 32] & FLAG (i);
}

static inline int item_summed(int i)
{
	assert (i >= 0);
	assert (i < ((STATIC_ARRAY_SIZE (misc_items) + 1) * 32));
	return misc_items[i / 32] & FLAG (i);
}

static inline void watchlist_add (uint32_t *wl, int item)
{
	assert (item >= 0);
	assert (item < ((WL_LEN + 1) * 32));
	wl[item / 32] |= FLAG (item);
}

static inline void watchlist_remove (uint32_t *wl, int item)
{
	assert (item >= 0);
	assert (item < ((WL_LEN + 1) * 32));
	wl[item / 32] &= ~FLAG (item);
}

static inline void watchlist_set (uint32_t *wl, uint32_t val)
{
	int i;
	for (i = 0; i < WL_LEN; i++)
		wl[i] = val;
}

/* This is horribly inefficient, but it is called only during configuration */
static int watchitem_find (const char *name)
{
	int max = STATIC_ARRAY_SIZE (specs);
	int i;

	for (i = 0; i < max; i++)
		if (strcasecmp (name, specs[i].name) == 0)
			return i;

	return -1;
}


/* Collectd hooks */

/* We need init function called before madwifi_config */

static int madwifi_real_init (void)
{
	int max = STATIC_ARRAY_SIZE (specs);
	int i;

	for (i = 0; i < STATIC_ARRAY_SIZE (bounds); i++)
		bounds[i] = 0;

	watchlist_set(watch_items, 0);
	watchlist_set(misc_items, 0);

	for (i = 0; i < max; i++)
	{
		bounds[specs[i].flags & SRC_MASK] = i;

		if (specs[i].flags & LOG)
			watch_items[i / 32] |= FLAG (i);

		if (specs[i].flags & SU)
			misc_items[i / 32] |= FLAG (i);
	}

	for (i = 0; i < STATIC_ARRAY_SIZE (bounds); i++)
		bounds[i]++;

	return (0);
}

static int madwifi_config (const char *key, const char *value)
{
	if (init_state != 1)
		madwifi_real_init();
	init_state = 1;

	if (ignorelist == NULL)
		ignorelist = ignorelist_create (/* invert = */ 1);

	if (strcasecmp (key, "Interface") == 0)
		ignorelist_add (ignorelist, value);

	else if (strcasecmp (key, "IgnoreSelected") == 0)
		ignorelist_set_invert (ignorelist, IS_TRUE (value) ? 0 : 1);

	else if (strcasecmp (key, "Source") == 0)
	{
		if (strcasecmp (value, "ProcFS") == 0)
			use_sysfs = 0;
		else if (strcasecmp (value, "SysFS") == 0)
			use_sysfs = 1;
		else
		{
			ERROR ("madwifi plugin: The argument of the `Source' "
					"option must either be `SysFS' or "
					"`ProcFS'.");
			return -1;
		}
	}

	else if (strcasecmp (key, "WatchSet") == 0)
	{
		if (strcasecmp (value, "All") == 0)
			watchlist_set (watch_items, 0xFFFFFFFF);
		else if (strcasecmp (value, "None") == 0)
			watchlist_set (watch_items, 0);
		else return -1;
	}

	else if (strcasecmp (key, "WatchAdd") == 0)
	{
		int id = watchitem_find (value);

		if (id < 0)
			return (-1);
		else
			watchlist_add (watch_items, id);
	}

	else if (strcasecmp (key, "WatchRemove") == 0)
	{
		int id = watchitem_find (value);

		if (id < 0)
			return (-1);
		else
			watchlist_remove (watch_items, id);
	}

	else if (strcasecmp (key, "MiscSet") == 0)
	{
		if (strcasecmp (value, "All") == 0)
			watchlist_set (misc_items, 0xFFFFFFFF);
		else if (strcasecmp (value, "None") == 0)
			watchlist_set (misc_items, 0);
		else return -1;
	}

	else if (strcasecmp (key, "MiscAdd") == 0)
	{
		int id = watchitem_find (value);

		if (id < 0)
			return (-1);
		else
			watchlist_add (misc_items, id);
	}

	else if (strcasecmp (key, "MiscRemove") == 0)
	{
		int id = watchitem_find (value);

		if (id < 0)
			return (-1);
		else
			watchlist_remove (misc_items, id);
	}

	else
		return (-1);

	return (0);
}


static void submit (const char *dev, const char *type, const char *ti1,
			const char *ti2, value_t *val, int len)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = val;
	vl.values_len = len;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "madwifi", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	if ((ti1 != NULL) && (ti2 != NULL))
		ssnprintf (vl.type_instance, sizeof (vl.type_instance), "%s-%s", ti1, ti2);
	else if ((ti1 != NULL) && (ti2 == NULL))
		sstrncpy (vl.type_instance, ti1, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static void submit_derive (const char *dev, const char *type, const char *ti1,
				const char *ti2, derive_t val)
{
	value_t item;
	item.derive = val;
	submit (dev, type, ti1, ti2, &item, 1);
}

static void submit_derive2 (const char *dev, const char *type, const char *ti1,
				const char *ti2, derive_t val1, derive_t val2)
{
	value_t items[2];
	items[0].derive = val1;
	items[1].derive = val2;
	submit (dev, type, ti1, ti2, items, 2);
}

static void submit_gauge (const char *dev, const char *type, const char *ti1,
				const char *ti2, gauge_t val)
{
	value_t item;
	item.gauge = val;
	submit (dev, type, ti1, ti2, &item, 1);
}

static void submit_antx (const char *dev, const char *name,
		u_int32_t *vals, int vals_num)
{
	char ti2[16];
	int i;

	for (i = 0; i < vals_num; i++)
	{
		if (vals[i] == 0)
			continue;

		ssnprintf (ti2, sizeof (ti2), "%i", i);
		submit_derive (dev, "ath_stat", name, ti2,
				(derive_t) vals[i]);
	}
}

static inline void
macaddr_to_str (char *buf, size_t bufsize, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	ssnprintf (buf, bufsize, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void
process_stat_struct (int which, const void *ptr, const char *dev, const char *mac,
			 const char *type_name, const char *misc_name)
{
	uint32_t misc = 0;
	int i;

	assert (which >= 1);
	assert (which < STATIC_ARRAY_SIZE (bounds));

	for (i = bounds[which - 1]; i < bounds[which]; i++)
	{
		uint32_t val = *(uint32_t *)(((char *) ptr) + specs[i].offset) ;

		if (item_watched (i) && (val != 0))
			submit_derive (dev, type_name, specs[i].name, mac, val);

		if (item_summed (i))
			misc += val;
	}
	
	if (misc != 0)
		submit_derive (dev, type_name, misc_name, mac, misc);

}

static int
process_athstats (int sk, const char *dev)
{
	struct ifreq ifr;
	struct ath_stats stats;
	int status;

	sstrncpy (ifr.ifr_name, dev, sizeof (ifr.ifr_name));
	ifr.ifr_data = (void *) &stats;
	status = ioctl (sk, SIOCGATHSTATS, &ifr);
	if (status < 0)
	{
		/* Silent, because not all interfaces support all ioctls. */
		DEBUG ("madwifi plugin: Sending IO-control "
				"SIOCGATHSTATS to device %s "
				"failed with status %i.",
				dev, status);
		return (status);
	}

	/* These stats are handled as a special case, because they are
	   eight values each */

	if (item_watched (STAT_AST_ANT_RX))
		submit_antx (dev, "ast_ant_rx", stats.ast_ant_rx,
				STATIC_ARRAY_SIZE (stats.ast_ant_rx));

	if (item_watched (STAT_AST_ANT_TX))
		submit_antx (dev, "ast_ant_tx", stats.ast_ant_tx,
				STATIC_ARRAY_SIZE (stats.ast_ant_tx));

	/* All other ath statistics */
	process_stat_struct (ATH_STAT, &stats, dev, NULL, "ath_stat", "ast_misc");
	return (0);
}

static int
process_80211stats (int sk, const char *dev)
{
	struct ifreq ifr;
	struct ieee80211_stats stats;
	int status;

	sstrncpy (ifr.ifr_name, dev, sizeof (ifr.ifr_name));
	ifr.ifr_data = (void *) &stats;
	status = ioctl(sk, SIOCG80211STATS, &ifr);
	if (status < 0)
	{
		/* Silent, because not all interfaces support all ioctls. */
		DEBUG ("madwifi plugin: Sending IO-control "
				"SIOCG80211STATS to device %s "
				"failed with status %i.",
				dev, status);
		return (status);
	}

	process_stat_struct (IFA_STAT, &stats, dev, NULL, "ath_stat", "is_misc");
	return (0);
}


static int
process_station (int sk, const char *dev, struct ieee80211req_sta_info *si)
{
	struct iwreq iwr;
	static char mac[DATA_MAX_NAME_LEN];
	struct ieee80211req_sta_stats stats;
	const struct ieee80211_nodestats *ns = &stats.is_stats;
	int status;

	macaddr_to_str (mac, sizeof (mac), si->isi_macaddr);

	if (item_watched (STAT_NODE_TX_RATE))
		submit_gauge (dev, "node_tx_rate", mac, NULL,
			(si->isi_rates[si->isi_txrate] & IEEE80211_RATE_VAL) / 2);

	if (item_watched (STAT_NODE_RSSI))
		submit_gauge (dev, "node_rssi", mac, NULL, si->isi_rssi);

	memset (&iwr, 0, sizeof (iwr));
	sstrncpy(iwr.ifr_name, dev, sizeof (iwr.ifr_name));
	iwr.u.data.pointer = (void *) &stats;
	iwr.u.data.length = sizeof (stats);
	memcpy(stats.is_u.macaddr, si->isi_macaddr, IEEE80211_ADDR_LEN);
	status = ioctl(sk, IEEE80211_IOCTL_STA_STATS, &iwr);
	if (status < 0)
	{
		/* Silent, because not all interfaces support all ioctls. */
		DEBUG ("madwifi plugin: Sending IO-control "
				"IEEE80211_IOCTL_STA_STATS to device %s "
				"failed with status %i.",
				dev, status);
		return (status);
	}

	/* These two stats are handled as a special case as they are
	   a pair of 64bit values */
	if (item_watched (STAT_NODE_OCTETS))
		submit_derive2 (dev, "node_octets", mac, NULL,
			ns->ns_rx_bytes, ns->ns_tx_bytes);

	/* This stat is handled as a special case, because it is stored
	   as uin64_t, but we will ignore upper half */
	if (item_watched (STAT_NS_RX_BEACONS))
		submit_derive (dev, "node_stat", "ns_rx_beacons", mac,
			(ns->ns_rx_beacons & 0xFFFFFFFF));

	/* All other node statistics */
	process_stat_struct (NOD_STAT, ns, dev, mac, "node_stat", "ns_misc");
	return (0);
}

static int
process_stations (int sk, const char *dev)
{
	uint8_t buf[24*1024];
	struct iwreq iwr;
	uint8_t *cp;
	int len, nodes;
	int status;

	memset (&iwr, 0, sizeof (iwr));
	sstrncpy (iwr.ifr_name, dev, sizeof (iwr.ifr_name));
	iwr.u.data.pointer = (void *) buf;
	iwr.u.data.length = sizeof (buf);

	status = ioctl (sk, IEEE80211_IOCTL_STA_INFO, &iwr);
	if (status < 0)
	{
		/* Silent, because not all interfaces support all ioctls. */
		DEBUG ("madwifi plugin: Sending IO-control "
				"IEEE80211_IOCTL_STA_INFO to device %s "
				"failed with status %i.",
				dev, status);
		return (status);
	}

	len = iwr.u.data.length;

	cp = buf;
	nodes = 0;
	while (len >= sizeof (struct ieee80211req_sta_info))
	{
		struct ieee80211req_sta_info *si = (void *) cp;
		process_station(sk, dev, si);
		cp += si->isi_len;
		len -= si->isi_len;
		nodes++;
	}

	if (item_watched (STAT_ATH_NODES))
		submit_gauge (dev, "ath_nodes", NULL, NULL, nodes);
	return (0);
}

static int
process_device (int sk, const char *dev)
{
	int num_success = 0;
	int status;

	status = process_athstats (sk, dev);
	if (status == 0)
		num_success++;

	status = process_80211stats (sk, dev);
	if (status == 0)
		num_success++;

	status = process_stations (sk, dev);
	if (status == 0)
		num_success++;

	return ((num_success == 0) ? -1 : 0);
}

static int
check_devname (const char *dev)
{
	char buf[PATH_MAX];
	char buf2[PATH_MAX];
	int i;

	if (dev[0] == '.')
		return 0;
	
	ssnprintf (buf, sizeof (buf), "/sys/class/net/%s/device/driver", dev);
	buf[sizeof (buf) - 1] = 0;

	memset (buf2, 0, sizeof (buf2));
	i = readlink (buf, buf2, sizeof (buf2) - 1);
	if (i < 0)
		return 0;

	if (strstr (buf2, "/drivers/ath_") == NULL)
		return 0;
	return 1;
}

static int
sysfs_iterate(int sk)
{
	struct dirent *de;
	DIR *nets;
	int status;
	int num_success;
	int num_fail;

	nets = opendir ("/sys/class/net/");
	if (nets == NULL)
	{
		WARNING ("madwifi plugin: opening /sys/class/net failed");
		return (-1);
	}

	num_success = 0;
	num_fail = 0;
	while ((de = readdir (nets)))
	{
		if (check_devname (de->d_name) == 0)
			continue;

		if (ignorelist_match (ignorelist, de->d_name) != 0)
			continue;

		status = process_device (sk, de->d_name);
		if (status != 0)
		{
			ERROR ("madwifi plugin: Processing interface "
					"%s failed.", de->d_name);
			num_fail++;
		}
		else
		{
			num_success++;
		}
	} /* while (readdir) */

	closedir(nets);

	if ((num_success == 0) && (num_fail != 0))
		return (-1);
	return (0);
}

static int
procfs_iterate(int sk)
{
	char buffer[1024];
	char *device, *dummy;
	FILE *fh;
	int status;
	int num_success;
	int num_fail;
	
	if ((fh = fopen ("/proc/net/dev", "r")) == NULL)
	{
		WARNING ("madwifi plugin: opening /proc/net/dev failed");
		return (-1);
	}

	num_success = 0;
	num_fail = 0;
	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		dummy = strchr(buffer, ':');
		if (dummy == NULL)
			continue;
		dummy[0] = 0;

		device = buffer;
		while (device[0] == ' ')
			device++;

		if (device[0] == 0)
			continue;

		if (ignorelist_match (ignorelist, device) != 0)
			continue;

		status = process_device (sk, device);
		if (status != 0)
		{
			ERROR ("madwifi plugin: Processing interface "
					"%s failed.", device);
			num_fail++;
		}
		else
		{
			num_success++;
		}
	} /* while (fgets) */

	fclose(fh);

	if ((num_success == 0) && (num_fail != 0))
		return (-1);
	return 0;
}

static int madwifi_read (void)
{
	int rv;
	int sk;

	if (init_state == 0)
		madwifi_real_init();
	init_state = 2;

	sk = socket(AF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return (-1);


/* procfs iteration is not safe because it does not check whether given
   interface is madwifi interface and there are private ioctls used, which
   may do something completely different on non-madwifi devices.   
   Therefore, it is not used unless explicitly enabled (and should be used
   together with ignorelist). */

	if (use_sysfs)
		rv = sysfs_iterate(sk);
	else
		rv = procfs_iterate(sk);

	close(sk);

	return rv;
}

void module_register (void)
{
	plugin_register_config ("madwifi", madwifi_config,
			config_keys, config_keys_num);

	plugin_register_read ("madwifi", madwifi_read);
}
