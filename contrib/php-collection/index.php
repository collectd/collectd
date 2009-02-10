<?php // vim:fenc=utf-8:filetype=php:ts=4
/*
 * Copyright (C) 2009  Bruno Prémont <bonbons AT linux-vserver.org>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

error_reporting(E_ALL | E_NOTICE | E_WARNING);

require('config.php');
require('functions.php');

/**
 * Send back new list content
 * @items Array of options values to return to browser
 * @method Name of Javascript method that will be called to process data
 */
function dhtml_response_list(&$items, $method) {
	header("Content-Type: text/xml");

	print('<?xml version="1.0" encoding="utf-8" ?>'."\n");
	print("<response>\n");
	printf(" <method>%s</method>\n", htmlspecialchars($method));
	print(" <result>\n");
	foreach ($items as &$item)
		printf('  <option>%s</option>'."\n", htmlspecialchars($item));
	print(" </result>\n");
	print("</response>");
}

/**
 * Product page body with selection fields
 */
function build_page() {
	global $config;

	if (isset($_SERVER['HTTP_USER_AGENT']) && preg_match('/compatible; MSIE [0-9]+.[0-9];/', $_SERVER['HTTP_USER_AGENT'])) {
		// Internet Explorer does not support XHTML
		header("Content-Type: text/html");

		print('<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">');
		print('<html lang="en">'."\n");
	} else {
		header("Content-Type: application/xhtml+xml");

		print('<?xml version="1.0" encoding="utf-8" ?>'."\n");
		print('<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">'."\n");
		print('<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">'."\n");
	}
?>
	<head>
		<title>Collectd graph viewer</title>
		<link rel="icon" href="favicon.png" type="image/png" />
		<style type="text/css">
			body, html { background-color: #EEEEEE; color: #000000; }
			h1 { text-align: center; }
			div.body { margin: auto; width: <?php echo 125+$config['rrd_width'] ?>px; background: #FFFFFF; border: 1px solid #DDDDDD; }
			div.selector { margin: 0.5em 2em; }
			div.selectorbox { padding: 5px; border: 1px solid #CCCCCC; background-color: #F8F8F8; }
			div.selectorbox table { border: none; }
			div.selectorbox table td.s1 { border-bottom: 1px dashed #F0F0F0; padding-right: 1em; vertical-align: middle; }
			div.selectorbox table td.s2 { border-bottom: 1px dashed #F0F0F0; vertical-align: middle; }
			div.selectorbox table td.s3 { vertical-align: middle; }
			div.selectorbox table td.sc { padding: 0.5em 2em; text-align: center; }
			a img { border: none; }
			div.graphs { border-top: 1px solid #DDDDDD; padding: 5px; overflow: auto; }
			div.graphs_t { display: table; }
			div.graph { display: table-row; }
			div.graph_img { display: table-cell; vertical-align: middle; text-align: right; }
			div.graph_opts { display: table-cell; vertical-align: middle; text-align: center; line-height: 2em; }
			select { width: 100%; }
		</style>
		<script type="text/javascript">// <![CDATA[
var dhtml_url = '<?php echo addslashes('http://'.$_SERVER['HTTP_HOST'].$_SERVER['PHP_SELF']); ?>';
var graph_url = '<?php echo addslashes('http://'.$_SERVER['HTTP_HOST'].dirname($_SERVER['PHP_SELF']).'/graph.php'); ?>';
//		]]></script>
		<script type="text/javascript" src="browser.js"></script>
	</head>

	<body onload="ListRefreshHost()"><div class="body">
		<h1><img src="collectd-logo.png" align="bottom" alt="" /> Collectd browser for system statistics graphs</h1>
		<div class="selector"><a href="javascript:toggleDiv('selector')"><span id="selector_sw">Hide</span> graph selection tool</a><div id="selector" class="selectorbox">
			<table>
				<tr>
					<td class="s1">Host:</td>
					<td class="s2"><select id="host_list"   name="host"   disabled="disabled" onchange="ListRefreshPlugin()">
					</select></td>
					<td class="s3"><a href="javascript:ListRefreshHost()"><img src="refresh.png" width="16" height="16" alt="R" title="Refresh host list" /></a></td>
				</tr>
				<tr>
					<td class="s1">Plugin:</td>
					<td class="s2"><select id="plugin_list" name="plugin" disabled="disabled" onchange="ListRefreshPluginInstance()">
					</select></td>
					<td class="s3"><a href="javascript:ListRefreshPlugin()"><img src="refresh.png" width="16" height="16" alt="R" title="Refresh plugin list" /></a></td>
				</tr>
				<tr>
					<td class="s1">Plugin instance:</td>
					<td class="s2"><select id="pinst_list"  name="pinst"  disabled="disabled" onchange="ListRefreshType()">
					</select></td>
					<td class="s3"><a href="javascript:ListRefreshPluginInstance()"><img src="refresh.png" width="16" height="16" alt="R" title="Refresh plugin instance list" /></a></td>
				</tr>
				<tr>
					<td class="s1">Type:</td>
					<td class="s2"><select id="type_list"   name="type"   disabled="disabled" onchange="ListRefreshTypeInstance()">
					</select></td>
					<td class="s3"><a href="javascript:ListRefreshType()"><img src="refresh.png" width="16" height="16" alt="R" title="Refresh type list" /></a></td>
				</tr>
				<tr>
					<td class="s1">Type instance:</td>
					<td class="s2"><select id="tinst_list"  name="tinst"  disabled="disabled" onchange="RefreshButtons()">
					</select></td>
					<td class="s3"><a href="javascript:ListRefreshTypeInstance()"><img src="refresh.png" width="16" height="16" alt="R" title="Refresh type instance list" /></a></td>
				</tr>
				<tr>
					<td class="s1">Graph settings:</td>
					<td class="s2"><select id="timespan" name="timespan">
<?php				foreach ($config['timespan'] as &$timespan)
						printf("\t\t\t\t\t\t<option value=\"%s\">%s</option>\n", htmlspecialchars($timespan['name']), htmlspecialchars($timespan['label']));
?>					</select>
					<br /><label><input id="logarithmic"  name="logarithmic" type="checkbox" value="1" /> Logarithmic scale</label>
					<br /><label><input id="tinylegend"  name="tinylegend" type="checkbox" value="1" /> Minimal legend</label></td>
					<td class="s3"></td>
				</tr>
				<tr>
					<td class="sc" colspan="3"><input id="btnAdd"     name="btnAdd"     type="button" disabled="disabled" onclick="GraphAppend()" value="Add graph" />
					<input id="btnClear"   name="btnClear"   type="button" disabled="disabled" onclick="GraphDropAll()" value="Remove all graphs" />
					<input id="btnRefresh" name="btnRefresh" type="button" disabled="disabled" onclick="GraphRefresh(null)" value="Refresh all graphs" /></td>
				</tr>
			</table>
		</div></div>
		<div class="graphs"><div id="graphs" class="graphs_t">
			<div id="nograph">Please use above graph selection tool to add graphs to this area.</div>
		</div></div>
	</div></body>
</html><?php
}


/*
 * Select action based on user input
 */
$action = read_var('action', $_POST, 'overview');
switch ($action) {
	case 'list_hosts':
		// Generate a list of hosts
		$hosts = collectd_list_hosts();
		return dhtml_response_list($hosts, 'ListOfHost');

	case 'list_plugins':
		// Generate list of plugins for selected hosts
		$arg_hosts = read_var('host', $_POST, array());
		if (!is_array($arg_hosts))
			$arg_hosts = array($arg_hosts);
		$plugins = collectd_list_plugins(reset($arg_hosts));
		return dhtml_response_list($plugins, 'ListOfPlugin');

	case 'list_pinsts':
		// Generate list of plugin_instances for selected hosts and plugin
		$arg_hosts = read_var('host', $_POST, array());
		if (!is_array($arg_hosts))
			$arg_hosts = array($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$pinsts = collectd_list_pinsts(reset($arg_hosts), $arg_plugin);
		return dhtml_response_list($pinsts, 'ListOfPluginInstance');

	case 'list_types':
		// Generate list of types for selected hosts, plugin and plugin-instance
		$arg_hosts  = read_var('host', $_POST, array());
		if (!is_array($arg_hosts))
			$arg_hosts = array($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$arg_pinst  = read_var('plugin_instance', $_POST, '');
		$types = collectd_list_types(reset($arg_hosts), $arg_plugin, $arg_pinst);
		return dhtml_response_list($types, 'ListOfType');

	case 'list_tinsts':
		// Generate list of types for selected hosts, plugin and plugin-instance
		$arg_hosts  = read_var('host', $_POST, array());
		if (!is_array($arg_hosts))
			$arg_hosts = array($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$arg_pinst  = read_var('plugin_instance', $_POST, '');
		$arg_type   = read_var('type', $_POST, '');
		$tinsts = collectd_list_tinsts(reset($arg_hosts), $arg_plugin, $arg_pinst, $arg_type);
		if (count($tinsts)) {
			require('definitions.php');
			load_graph_definitions();
			if (isset($MetaGraphDefs[$arg_type])) {
				$meta_tinsts = array('@');
				return dhtml_response_list($meta_tinsts, 'ListOfTypeInstance');
			}
		}
		return dhtml_response_list($tinsts, 'ListOfTypeInstance');

	case 'overview':
	default:
		return build_page();
		break;
}
?>
