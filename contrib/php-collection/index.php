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

function dhtml_response_graphs(&$graphs, $method) {
	header("Content-Type: text/xml");

	print('<?xml version="1.0" encoding="utf-8" ?>'."\n");
	print("<response>\n");
	printf(" <method>%s</method>\n", htmlspecialchars($method));
	print(" <result>\n");
	foreach ($graphs as &$graph)
		printf('  <graph host="%s" plugin="%s" plugin_instance="%s" type="%s" type_instance="%s" timespan="%s" logarithmic="%d" tinyLegend="%d" />'."\n",
		       htmlspecialchars($graph['host']), htmlspecialchars($graph['plugin']), htmlspecialchars($graph['pinst']),
		       htmlspecialchars($graph['type']), htmlspecialchars($graph['tinst']), htmlspecialchars($graph['timespan']),
		       htmlspecialchars($graph['logarithmic']), htmlspecialchars($graph['tinyLegend']));
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
	$url_base = (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] != 'off' ? 'https://' : 'http://').$_SERVER['HTTP_HOST'].dirname($_SERVER['PHP_SELF']).'/';
?>
	<head>
		<title>Collectd graph viewer</title>
		<link rel="icon" href="favicon.png" type="image/png" />
		<style type="text/css">
			body, html { background-color: #EEEEEE; color: #000000; }
			h1 { text-align: center; }
			div.body { margin: auto; width: <?php echo isset($config['rrd_width']) ? 110+(int)$config['rrd_width'] : 600; ?>px; background: #FFFFFF; border: 1px solid #DDDDDD; }
			p.error { color: #CC0000; margin: 0em; }
			div.selector { margin: 0.5em 2em; }
			div.selectorbox { padding: 5px; border: 1px solid #CCCCCC; background-color: #F8F8F8; }
			div.selectorbox table { border: none; }
			div.selectorbox table td.s1 { border-bottom: 1px dashed #F0F0F0; padding-right: 1em; vertical-align: middle; }
			div.selectorbox table td.s2 { border-bottom: 1px dashed #F0F0F0; vertical-align: middle; }
			div.selectorbox table td.s3 { vertical-align: middle; }
			div.selectorbox table td.sc { padding: 0.5em 2em; text-align: center; }
			a img { border: none; }
			div.graphs { border-top: 1px solid #DDDDDD; padding: 5px; overflow: auto; }
			div.graphs_t { position: relative; }
			div.graph { text-align: right; }
			div.selector select { width: 100%; }
			table.toolbox { border: 1px solid #5500dd; padding: 0px; margin: 0px; background: #ffffff;}
			table.toolbox td.c1 { vertical-align: middle; text-align: left; padding-left: 0.3em; padding-right: 1em; border-right: 1px solid #5500dd; }
			table.toolbox td.c2 { vertical-align: middle; text-align: center; padding-left: 5px; padding-right: 5px; }
		</style>
		<script type="text/javascript">// <![CDATA[
var dhtml_url = '<?php echo addslashes($url_base.basename($_SERVER['PHP_SELF'])); ?>';
var graph_url = '<?php echo addslashes($url_base.'graph.php'); ?>';
//		]]></script>
		<script type="text/javascript" src="<?php echo htmlspecialchars($url_base.'browser.js'); ?>"></script>
	</head>

	<body onload="ListRefreshHost(); GraphListRefresh(); "><div class="body">
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
					<input id="btnRefresh" name="btnRefresh" type="button" disabled="disabled" onclick="GraphRefreshAll()" value="Refresh all graphs" /></td>
				</tr>
				<tr>
					<td class="s1" rowspan="2">Graph list favorites:</td>
					<td class="s3"><input type="text" style="width: 100%" maxlength="30" id="GraphListName" name="GraphListName" value="default" onchange="GraphListCheckName(false)" /></td>
					<td class="s3"><a href="javascript:GraphSave()"><img src="save.png" width="16" height="16" alt="S" title="Save graph list to cookie" /></a></td>
				</tr>
				<tr>
					<td class="s2"><select id="GraphList" name="GraphList" onfocus="GraphListRefresh()">
						<option value="default">default</option>
					</select></td>
					<td class="s3"><a href="javascript:GraphLoad()"><img src="load.png" width="16" height="16" alt="L" title="Load graph list from cookie" /></a><a href="javascript:GraphDrop()"><img src="delete.png" width="16" height="16" alt="D" title="Delete graph list from cookie" /></a></td>
				</tr>
			</table>
		</div></div>
		<div class="graphs"><div id="graphs" class="graphs_t">
			<div id="nograph">Please use above graph selection tool to add graphs to this area.<?php
			// Config checking
			if (!isset($config['datadirs']))
				echo '<p class="error">Config error: $config["datadirs"] is not set</p>';
			else if (!is_array($config['datadirs']))
				echo '<p class="error">Config error: $config["datadirs"] is not an array</p>';
			else if (count($config['datadirs']) == 0)
				echo '<p class="error">Config error: $config["datadirs"] is empty</p>';
			else foreach ($config['datadirs'] as $datadir)
				if (!is_dir($datadir))
					echo '<p class="error">Config error: $config["datadirs"], '.htmlspecialchars($datadir).' does not exist</p>';
			if (!isset($config['rrd_width']))
				echo '<p class="error">Config error: $config["rrd_width"] is not set</p>';
			else if (10 > (int)$config['rrd_width'])
				echo '<p class="error">Config error: $config["rrd_width"] is invalid. Integer >= 10 expected</p>';
			if (!isset($config['rrd_height']))
				echo '<p class="error">Config error: $config["rrd_height"] is not set</p>';
			else if (10 > (int)$config['rrd_height'])
				echo '<p class="error">Config error: $config["rrd_height"] is invalid. Integer >= 10 expected</p>';
			if (!isset($config['rrd_opts']))
				echo '<p class="error">Config error: $config["rrd_opts"] is not set</p>';
			else if (!is_array($config['rrd_opts']))
				echo '<p class="error">Config error: $config["rrd_opts"] is not an array</p>';
			if (!isset($config['timespan']))
				echo '<p class="error">Config error: $config["timespan"] is not set</p>';
			else if (!is_array($config['timespan']))
				echo '<p class="error">Config error: $config["timespan"] is not an array</p>';
			else if (count($config['timespan']) == 0)
				echo '<p class="error">Config error: $config["timespan"] is empty</p>';
			else foreach ($config['timespan'] as &$timespan)
				if (!is_array($timespan) || !isset($timespan['name']) || !isset($timespan['label']) || !isset($timespan['seconds']) || 10 > (int)$timespan['seconds'])
					echo '<p class="error">Config error: $config["timespan"], invalid entry found</p>';
			if (!is_null($config['collectd_sock']) && strncmp('unix://', $config['collectd_sock'], 7) != 0)
				echo '<p class="error">Config error: $config["collectd_sock"] is not valid</p>';
			if (!defined('RRDTOOL'))
				echo '<p class="error">Config error: RRDTOOL is not defined</p>';
			else if (!is_executable(RRDTOOL))
				echo '<p class="error">Config error: RRDTOOL ('.htmlspecialchars(RRDTOOL).') is not executable</p>';
			?></div>
		</div></div>
		<input type="hidden" name="ge_graphid" id="ge_graphid" value="" />
		<table id="ge" class="toolbox" style="position: absolute; display: none; " cellspacing="1" cellpadding="0">
			<tr>
				<td class="c1" rowspan="3"><select id="ge_timespan" name="ge_timespan" onchange="GraphAdjust(null)"><?php
				foreach ($config['timespan'] as &$timespan)
					printf("\t\t\t\t\t\t<option value=\"%s\">%s</option>\n", htmlspecialchars($timespan['name']), htmlspecialchars($timespan['label']));
				?></select><br />
				<label><input id="ge_logarithmic"  name="ge_logarithmic" type="checkbox" value="1" onchange="GraphAdjust(null)" /> Logarithmic scale</label><br />
				<label><input id="ge_tinylegend"  name="ge_tinylegend" type="checkbox" value="1" onchange="GraphAdjust(null)" /> Minimal legend</label></td>
				<td class="c2"><a href="javascript:GraphMoveUp(null)"><img src="move-up.png" alt="UP" title="Move graph up"/></a></td>
			</tr>
			<tr>
				<td class="c2"><a href="javascript:GraphRefresh(null)"><img src="refresh.png" alt="R" title="Refresh graph"/></a>&nbsp;<a href="javascript:GraphRemove(null)"><img src="delete.png" alt="RM" title="Remove graph"/></a></td>
			</tr>
			<tr>
				<td class="c2"><a href="javascript:GraphMoveDown(null)"><img src="move-down.png" alt="DN" title="Move graph down"/></a></td>
			</tr>
		</table>
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
		if (count($hosts) > 1)
			array_unshift($hosts, '@all');
		return dhtml_response_list($hosts, 'ListOfHost');

	case 'list_plugins':
		// Generate list of plugins for selected hosts
		$arg_hosts = read_var('host', $_POST, '');
		if (is_array($arg_hosts))
			$arg_hosts = reset($arg_hosts);
		$plugins = collectd_list_plugins($arg_hosts);
		if (count($plugins) > 1)
			array_unshift($plugins, '@all');
		return dhtml_response_list($plugins, 'ListOfPlugin');

	case 'list_pinsts':
		// Generate list of plugin_instances for selected hosts and plugin
		$arg_hosts = read_var('host', $_POST, '');
		if (is_array($arg_hosts))
			$arg_hosts = reset($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$pinsts = collectd_list_plugins($arg_hosts, $arg_plugin);
		if (count($pinsts) > 1)
			array_unshift($pinsts, '@all' /* , '@merge_sum', '@merge_avg', '@merge_stack', '@merge_line' */);
		return dhtml_response_list($pinsts, 'ListOfPluginInstance');

	case 'list_types':
		// Generate list of types for selected hosts, plugin and plugin-instance
		$arg_hosts  = read_var('host', $_POST, '');
		if (is_array($arg_hosts))
			$arg_hosts = reset($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$arg_pinst  = read_var('plugin_instance', $_POST, '');
		$types = collectd_list_types($arg_hosts, $arg_plugin, $arg_pinst);
		if (count($types) > 1)
			array_unshift($types, '@all');
		return dhtml_response_list($types, 'ListOfType');

	case 'list_tinsts':
		// Generate list of types for selected hosts, plugin and plugin-instance
		$arg_hosts  = read_var('host', $_POST, '');
		if (is_array($arg_hosts))
			$arg_hosts = reset($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$arg_pinst  = read_var('plugin_instance', $_POST, '');
		$arg_type   = read_var('type', $_POST, '');
		$tinsts = collectd_list_types($arg_hosts, $arg_plugin, $arg_pinst, $arg_type);
		if (count($tinsts))
			if ($arg_type != '@all') {
				require('definitions.php');
				load_graph_definitions();
				if (isset($MetaGraphDefs[$arg_type]))
					array_unshift($tinsts, '@merge');
				if (count($tinsts) > 1)
					array_unshift($tinsts, '@all');
			} else {
				array_unshift($tinsts, /* '@merge_sum', '@merge_avg', '@merge_stack', '@merge_line', */ '@merge');
				if (count($tinsts) > 1)
					array_unshift($tinsts, '@all');
			}
		return dhtml_response_list($tinsts, 'ListOfTypeInstance');

	case 'list_graphs':
		// Generate list of types for selected hosts, plugin and plugin-instance
		$arg_hosts  = read_var('host', $_POST, '');
		if (is_array($arg_hosts))
			$arg_hosts = reset($arg_hosts);
		$arg_plugin = read_var('plugin', $_POST, '');
		$arg_pinst  = read_var('plugin_instance', $_POST, '');
		$arg_type   = read_var('type', $_POST, '');
		$arg_tinst  = read_var('type_instance', $_POST, '');
		$arg_log    = (int)read_var('logarithmic', $_POST, '0');
		$arg_legend = (int)read_var('tinyLegend', $_POST, '0');
		$arg_period = read_var('timespan', $_POST, '');
		$graphs = collectd_list_graphs($arg_hosts, $arg_plugin, $arg_pinst, $arg_type, $arg_tinst);
		foreach ($graphs as &$graph) {
			$graph['logarithmic'] = $arg_log;
			$graph['tinyLegend']  = $arg_legend;
			$graph['timespan']    = $arg_period;
		}
		return dhtml_response_graphs($graphs, 'ListOfGraph');

	case 'overview':
	default:
		return build_page();
		break;
}
?>
