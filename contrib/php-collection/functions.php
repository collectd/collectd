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

define('REGEXP_HOST', '/^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/');
define('REGEXP_PLUGIN', '/^[a-zA-Z0-9_.-]+$/');

/**
 * Read input variable from GET, POST or COOKIE taking
 * care of magic quotes
 * @name Name of value to return
 * @array User-input array ($_GET, $_POST or $_COOKIE)
 * @default Default value
 * @return $default if name in unknown in $array, otherwise
 *         input value with magic quotes stripped off
 */
function read_var($name, &$array, $default = null) {
	if (isset($array[$name])) {
		if (is_array($array[$name])) {
			if (get_magic_quotes_gpc()) {
				$ret = array();
				while (list($k, $v) = each($array[$name]))
					$ret[stripslashes($k)] = stripslashes($v);
				return $ret;
			} else
				return $array[$name];
		} else if (is_string($array[$name]) && get_magic_quotes_gpc()) {
			return stripslashes($array[$name]);
		} else
			return $array[$name];
	} else
		return $default;
}

/**
 * Alphabetically compare host names, comparing label
 * from tld to node name
 */
function collectd_compare_host($a, $b) {
	$ea = explode('.', $a);
	$eb = explode('.', $b);
	$i = count($ea) - 1;
	$j = count($eb) - 1;
	while ($i >= 0 && $j >= 0)
		if (($r = strcmp($ea[$i--], $eb[$j--])) != 0)
			return $r;
	return 0;
}

function collectd_walk(&$options) {
	global $config;

	foreach($config['datadirs'] as $datadir)
		if ($dh = @opendir($datadir)) {
			while (($hdent = readdir($dh)) !== false) {
				if ($hdent == '.' || $hdent == '..' || !is_dir($datadir.'/'.$hdent))
					continue;
				if (!preg_match(REGEXP_HOST, $hdent))
					continue;
				if (isset($options['cb_host']) && ($options['cb_host'] === false || !$options['cb_host']($options, $hdent)))
					continue;

				if ($dp = @opendir($datadir.'/'.$hdent)) {
					while (($pdent = readdir($dp)) !== false) {
						if ($pdent == '.' || $pdent == '..' || !is_dir($datadir.'/'.$hdent.'/'.$pdent))
							continue;
						if ($i = strpos($pdent, '-')) {
							$plugin = substr($pdent, 0, $i);
							$pinst  = substr($pdent, $i+1);
						} else {
							$plugin = $pdent;
							$pinst  = '';
						}
						if (isset($options['cb_plugin']) && ($options['cb_plugin'] === false || !$options['cb_plugin']($options, $hdent, $plugin)))
							continue;
						if (isset($options['cb_pinst']) && ($options['cb_pinst'] === false || !$options['cb_pinst']($options, $hdent, $plugin, $pinst)))
							continue;

						if ($dt = @opendir($datadir.'/'.$hdent.'/'.$pdent)) {
							while (($tdent = readdir($dt)) !== false) {
								if ($tdent == '.' || $tdent == '..' || !is_file($datadir.'/'.$hdent.'/'.$pdent.'/'.$tdent))
									continue;
								if (substr($tdent, strlen($tdent)-4) != '.rrd')
									continue;
								$tdent = substr($tdent, 0, strlen($tdent)-4);
								if ($i = strpos($tdent, '-')) {
									$type  = substr($tdent, 0, $i);
									$tinst = substr($tdent, $i+1);
								} else {
									$type  = $tdent;
									$tinst = '';
								}
								if (isset($options['cb_type']) && ($options['cb_type'] === false || !$options['cb_type']($options, $hdent, $plugin, $pinst, $type)))
									continue;
								if (isset($options['cb_tinst']) && ($options['cb_tinst'] === false || !$options['cb_tinst']($options, $hdent, $plugin, $pinst, $type, $tinst)))
									continue;
							}
							closedir($dt);
						}
					}
					closedir($dp);
				}
			}
			closedir($dh);
		} else
			error_log('Failed to open datadir: '.$datadir);
		return true;
}

function _collectd_list_cb_host(&$options, $host) {
	if ($options['cb_plugin'] === false) {
		$options['result'][] = $host;
		return false;
	} else if (isset($options['filter_host'])) {
		if ($options['filter_host'] == '@all') {
			return true; // We take anything
		} else if (substr($options['filter_host'], 0, 2) == '@.') {
			if ($host == substr($options['filter_host'], 2) || substr($host, 0, 1-strlen($options['filter_host'])) == substr($options['filter_host'], 1))
				return true; // Host part of domain
			else
				return false;
		} else if ($options['filter_host'] == $host) {
			return true;
		} else
			return false;
	} else
		return true;
}

function _collectd_list_cb_plugin(&$options, $host, $plugin) {
	if ($options['cb_pinst'] === false) {
		$options['result'][] = $plugin;
		return false;
	} else if (isset($options['filter_plugin'])) {
		if ($options['filter_plugin'] == '@all')
			return true;
		else if ($options['filter_plugin'] == $plugin)
			return true;
		else
			return false;
	} else
		return true;
}

function _collectd_list_cb_pinst(&$options, $host, $plugin, $pinst) {
	if ($options['cb_type'] === false) {
		$options['result'][] = $pinst;
		return false;
	} else if (isset($options['filter_pinst'])) {
		if ($options['filter_pinst'] == '@all')
			return true;
		else if (strncmp($options['filter_pinst'], '@merge_', 7) == 0)
			return true;
		else if ($options['filter_pinst'] == $pinst)
			return true;
		else
			return false;
	} else
		return true;
}

function _collectd_list_cb_type(&$options, $host, $plugin, $pinst, $type) {
	if ($options['cb_tinst'] === false) {
		$options['result'][] = $type;
		return false;
	} else if (isset($options['filter_type'])) {
		if ($options['filter_type'] == '@all')
			return true;
		else if ($options['filter_type'] == $type)
			return true;
		else
			return false;
	} else
		return true;
}

function _collectd_list_cb_tinst(&$options, $host, $plugin, $pinst, $type, $tinst) {
	$options['result'][] = $tinst;
	return false;
}

function _collectd_list_cb_graph(&$options, $host, $plugin, $pinst, $type, $tinst) {
	if (isset($options['filter_tinst'])) {
		if ($options['filter_tinst'] == '@all') {
		} else if ($options['filter_tinst'] == $tinst) {
		} else if (strncmp($options['filter_tinst'], '@merge', 6) == 0) {
			// Need to exclude @merge with non-existent meta graph
		} else
			return false;
	}
	if (isset($options['filter_pinst']) && strncmp($options['filter_pinst'], '@merge', 6) == 0)
		$pinst = $options['filter_pinst'];
	if (isset($options['filter_tinst']) && strncmp($options['filter_tinst'], '@merge', 6) == 0)
		$tinst = $options['filter_tinst'];
	$ident = collectd_identifier($host, $plugin, $pinst, $type, $tinst);
	if (!in_array($ident, $options['ridentifiers'])) {
		$options['ridentifiers'][] = $ident;
		$options['result'][] = array('host'=>$host, 'plugin'=>$plugin, 'pinst'=>$pinst, 'type'=>$type, 'tinst'=>$tinst);
	}
}

/**
 * Fetch list of hosts found in collectd's datadirs.
 * @return Sorted list of hosts (sorted by label from rigth to left)
 */
function collectd_list_hosts() {
	$options = array(
		'result' => array(),
		'cb_host' => '_collectd_list_cb_host',
		'cb_plugin' => false,
		'cb_pinst' => false,
		'cb_type' => false,
		'cb_tinst' => false
	);
	collectd_walk($options);
	$hosts = array_unique($options['result']);
	usort($hosts, 'collectd_compare_host');
	return $hosts;
}

/**
 * Fetch list of plugins found in collectd's datadirs for given host.
 * @arg_host Name of host for which to return plugins
 * @return Sorted list of plugins (sorted alphabetically)
 */
function collectd_list_plugins($arg_host, $arg_plugin = null) {
	$options = array(
		'result' => array(),
		'cb_host' => '_collectd_list_cb_host',
		'cb_plugin' => '_collectd_list_cb_plugin',
		'cb_pinst' => is_null($arg_plugin) ? false : '_collectd_list_cb_pinst',
		'cb_type' => false,
		'cb_tinst' => false,
		'filter_host' => $arg_host,
		'filter_plugin' => $arg_plugin
	);
	collectd_walk($options);
	$plugins = array_unique($options['result']);
	sort($plugins);
	return $plugins;
}

/**
 * Fetch list of types found in collectd's datadirs for given host+plugin+instance
 * @arg_host Name of host
 * @arg_plugin Name of plugin
 * @arg_pinst Plugin instance
 * @return Sorted list of types (sorted alphabetically)
 */
function collectd_list_types($arg_host, $arg_plugin, $arg_pinst, $arg_type = null) {
	$options = array(
		'result' => array(),
		'cb_host' => '_collectd_list_cb_host',
		'cb_plugin' => '_collectd_list_cb_plugin',
		'cb_pinst' => '_collectd_list_cb_pinst',
		'cb_type' => '_collectd_list_cb_type',
		'cb_tinst' => is_null($arg_type) ? false : '_collectd_list_cb_tinst',
		'filter_host' => $arg_host,
		'filter_plugin' => $arg_plugin,
		'filter_pinst' => $arg_pinst,
		'filter_type' => $arg_type
	);
	collectd_walk($options);
	$types = array_unique($options['result']);
	sort($types);
	return $types;
}

function collectd_list_graphs($arg_host, $arg_plugin, $arg_pinst, $arg_type, $arg_tinst) {
	$options = array(
		'result' => array(),
		'ridentifiers' => array(),
		'cb_host' => '_collectd_list_cb_host',
		'cb_plugin' => '_collectd_list_cb_plugin',
		'cb_pinst' => '_collectd_list_cb_pinst',
		'cb_type' => '_collectd_list_cb_type',
		'cb_tinst' => '_collectd_list_cb_graph',
		'filter_host' => $arg_host,
		'filter_plugin' => $arg_plugin,
		'filter_pinst' => $arg_pinst,
		'filter_type' => $arg_type,
		'filter_tinst' => $arg_tinst == '@' ? '@merge' : $arg_tinst
	);
	collectd_walk($options);
	return $options['result'];
}

/**
 * Parse symlinks in order to get an identifier that collectd understands
 * (e.g. virtualisation is collected on host for individual VMs and can be
 *  symlinked to the VM's hostname, support FLUSH for these by flushing
 *  on the host-identifier instead of VM-identifier)
 * @host Host name
 * @plugin Plugin name
 * @pinst Plugin instance
 * @type Type name
 * @tinst Type instance
 * @return Identifier that collectd's FLUSH command understands
 */
function collectd_identifier($host, $plugin, $pinst, $type, $tinst) {
	global $config;
	$rrd_realpath    = null;
	$orig_identifier = sprintf('%s/%s%s%s/%s%s%s', $host, $plugin, strlen($pinst) ? '-' : '', $pinst, $type, strlen($tinst) ? '-' : '', $tinst);
	$identifier      = null;
	foreach ($config['datadirs'] as $datadir)
		if (is_file($datadir.'/'.$orig_identifier.'.rrd')) {
			$rrd_realpath = realpath($datadir.'/'.$orig_identifier.'.rrd');
			break;
		}
	if ($rrd_realpath) {
		$identifier   = basename($rrd_realpath);
		$identifier   = substr($identifier, 0, strlen($identifier)-4);
		$rrd_realpath = dirname($rrd_realpath);
		$identifier   = basename($rrd_realpath).'/'.$identifier;
		$rrd_realpath = dirname($rrd_realpath);
		$identifier   = basename($rrd_realpath).'/'.$identifier;
	}

	if (is_null($identifier))
		return $orig_identifier;
	else
		return $identifier;
}

/**
 * Tell collectd that it should FLUSH all data it has regarding the
 * graph we are about to generate.
 * @host Host name
 * @plugin Plugin name
 * @pinst Plugin instance
 * @type Type name
 * @tinst Type instance
 */
function collectd_flush($identifier) {
	global $config;

	if (!$config['collectd_sock'])
		return false;
	if (is_null($identifier) || (is_array($identifier) && count($identifier) == 0) || !(is_string($identifier) || is_array($identifier)))
		return false;

	$u_errno  = 0;
	$u_errmsg = '';
	if ($socket = @fsockopen($config['collectd_sock'], 0, $u_errno, $u_errmsg)) {
		$cmd = 'FLUSH plugin=rrdtool';
		if (is_array($identifier)) {
			foreach ($identifier as $val)
				$cmd .= sprintf(' identifier="%s"', $val);
		} else
			$cmd .= sprintf(' identifier="%s"', $identifier);
		$cmd .= "\n";

		$r = fwrite($socket, $cmd, strlen($cmd));
		if ($r === false || $r != strlen($cmd))
			error_log(sprintf("graph.php: Failed to write whole command to unix-socket: %d out of %d written", $r === false ? -1 : $r, strlen($cmd)));

		$resp = fgets($socket);
		if ($resp === false)
			error_log(sprintf("graph.php: Failed to read response from collectd for command: %s", trim($cmd)));

		$n = (int)$resp;
		while ($n-- > 0)
			fgets($socket);

		fclose($socket);
	} else
		error_log(sprintf("graph.php: Failed to open unix-socket to collectd: %d: %s", $u_errno, $u_errmsg));
}

class CollectdColor {
	private $r = 0;
	private $g = 0;
	private $b = 0;

	function __construct($value = null) {
		if (is_null($value)) {
		} else if (is_array($value)) {
			if (isset($value['r']))
				$this->r = $value['r'] > 0 ? ($value['r'] > 1 ? 1 : $value['r']) : 0;
			if (isset($value['g']))
				$this->g = $value['g'] > 0 ? ($value['g'] > 1 ? 1 : $value['g']) : 0;
			if (isset($value['b']))
				$this->b = $value['b'] > 0 ? ($value['b'] > 1 ? 1 : $value['b']) : 0;
		} else if (is_string($value)) {
			$matches = array();
			if ($value == 'random') {
				$this->randomize();
			} else if (preg_match('/([0-9A-Fa-f][0-9A-Fa-f])([0-9A-Fa-f][0-9A-Fa-f])([0-9A-Fa-f][0-9A-Fa-f])/', $value, $matches)) {
				$this->r = ('0x'.$matches[1]) / 255.0;
				$this->g = ('0x'.$matches[2]) / 255.0;
				$this->b = ('0x'.$matches[3]) / 255.0;
			}
		} else if (is_a($value, 'CollectdColor')) {
			$this->r = $value->r;
			$this->g = $value->g;
			$this->b = $value->b;
		}
	}

	function randomize() {
		$this->r = rand(0, 255) / 255.0;
		$this->g = rand(0, 255) / 255.0;
		$this->b = 0.0;
		$min = 0.0;
		$max = 1.0;

		if (($this->r + $this->g) < 1.0) {
			$min = 1.0 - ($this->r + $this->g);
		} else {
			$max = 2.0 - ($this->r + $this->g);
		}
		$this->b = $min + ((rand(0, 255)/255.0) * ($max - $min));
	}

	function fade($bkgnd = null, $alpha = 0.25) {
		if (is_null($bkgnd) || !is_a($bkgnd, 'CollectdColor')) {
			$bg_r = 1.0;
			$bg_g = 1.0;
			$bg_b = 1.0;
		} else {
			$bg_r = $bkgnd->r;
			$bg_g = $bkgnd->g;
			$bg_b = $bkgnd->b;
		}

		$this->r = $alpha * $this->r + ((1.0 - $alpha) * $bg_r);
		$this->g = $alpha * $this->g + ((1.0 - $alpha) * $bg_g);
		$this->b = $alpha * $this->b + ((1.0 - $alpha) * $bg_b);
	}

	function as_array() {
		return array('r'=>$this->r, 'g'=>$this->g, 'b'=>$this->b);
	}

	function as_string() {
		$r = (int)($this->r*255);
		$g = (int)($this->g*255);
		$b = (int)($this->b*255);
		return sprintf('%02x%02x%02x', $r > 255 ? 255 : $r, $g > 255 ? 255 : $g, $b > 255 ? 255 : $b);
	}
}


/**
 * Helper function to strip quotes from RRD output
 * @str RRD-Info generated string
 * @return String with one surrounding pair of quotes stripped
 */
function rrd_strip_quotes($str) {
	if ($str[0] == '"' && $str[strlen($str)-1] == '"')
		return substr($str, 1, strlen($str)-2);
	else
		return $str;
}

function rrd_escape($str) {
	return str_replace(array('\\', ':'), array('\\\\', '\\:'), $str);
}

/**
 * Determine useful information about RRD file
 * @file Name of RRD file to analyse
 * @return Array describing the RRD file
 */
function rrd_info($file) {
	$info = array('filename'=>$file);

	$rrd = popen(RRDTOOL.' info '.escapeshellarg($file), 'r');
	if ($rrd) {
		while (($s = fgets($rrd)) !== false) {
			$p = strpos($s, '=');
			if ($p === false)
				continue;
			$key = trim(substr($s, 0, $p));
			$value = trim(substr($s, $p+1));
			if (strncmp($key,'ds[', 3) == 0) {
				/* DS definition */
				$p = strpos($key, ']');
				$ds = substr($key, 3, $p-3);
				if (!isset($info['DS']))
					$info['DS'] = array();
				$ds_key = substr($key, $p+2);

				if (strpos($ds_key, '[') === false) {
					if (!isset($info['DS']["$ds"]))
						$info['DS']["$ds"] = array();
					$info['DS']["$ds"]["$ds_key"] = rrd_strip_quotes($value);
				}
			} else if (strncmp($key, 'rra[', 4) == 0) {
				/* RRD definition */
				$p = strpos($key, ']');
				$rra = substr($key, 4, $p-4);
				if (!isset($info['RRA']))
					$info['RRA'] = array();
				$rra_key = substr($key, $p+2);

				if (strpos($rra_key, '[') === false) {
					if (!isset($info['RRA']["$rra"]))
						$info['RRA']["$rra"] = array();
					$info['RRA']["$rra"]["$rra_key"] = rrd_strip_quotes($value);
				}
			} else if (strpos($key, '[') === false) {
				$info[$key] = rrd_strip_quotes($value);
			}
		}
		pclose($rrd);
	}
	return $info;
}

function rrd_get_color($code, $line = true) {
	global $config;
	$name = ($line ? 'f_' : 'h_').$code;
	if (!isset($config['rrd_colors'][$name])) {
		$c_f = new CollectdColor('random');
		$c_h = new CollectdColor($c_f);
		$c_h->fade();
		$config['rrd_colors']['f_'.$code] = $c_f->as_string();
		$config['rrd_colors']['h_'.$code] = $c_h->as_string();
	}
	return $config['rrd_colors'][$name];
}

/**
 * Draw RRD file based on it's structure
 * @host
 * @plugin
 * @pinst
 * @type
 * @tinst
 * @opts
 * @return Commandline to call RRDGraph in order to generate the final graph
 */
function collectd_draw_rrd($host, $plugin, $pinst = null, $type, $tinst = null, $opts = array()) {
	global $config;
	$timespan_def = null;
	if (!isset($opts['timespan']))
		$timespan_def = reset($config['timespan']);
	else foreach ($config['timespan'] as &$ts)
		if ($ts['name'] == $opts['timespan'])
			$timespan_def = $ts;

	if (!isset($opts['rrd_opts']))
		$opts['rrd_opts'] = array();
	if (isset($opts['logarithmic']) && $opts['logarithmic'])
		array_unshift($opts['rrd_opts'], '-o');

	$rrdinfo = null;
	$rrdfile = sprintf('%s/%s%s%s/%s%s%s', $host, $plugin, is_null($pinst) ? '' : '-', $pinst, $type, is_null($tinst) ? '' : '-', $tinst);
	foreach ($config['datadirs'] as $datadir)
		if (is_file($datadir.'/'.$rrdfile.'.rrd')) {
			$rrdinfo = rrd_info($datadir.'/'.$rrdfile.'.rrd');
			if (isset($rrdinfo['RRA']) && is_array($rrdinfo['RRA']))
				break;
			else
				$rrdinfo = null;
		}

	if (is_null($rrdinfo))
		return false;

	$graph = array();
	$has_avg = false;
	$has_max = false;
	$has_min = false;
	reset($rrdinfo['RRA']);
	$l_max = 0;
	while (list($k, $v) = each($rrdinfo['RRA'])) {
		if ($v['cf'] == 'MAX')
			$has_max = true;
		else if ($v['cf'] == 'AVERAGE')
			$has_avg = true;
		else if ($v['cf'] == 'MIN')
			$has_min = true;
	}
	reset($rrdinfo['DS']);
	while (list($k, $v) = each($rrdinfo['DS'])) {
		if (strlen($k) > $l_max)
			$l_max = strlen($k);
		if ($has_min)
			$graph[] = sprintf('DEF:%s_min=%s:%s:MIN', $k, rrd_escape($rrdinfo['filename']), $k);
		if ($has_avg)
			$graph[] = sprintf('DEF:%s_avg=%s:%s:AVERAGE', $k, rrd_escape($rrdinfo['filename']), $k);
		if ($has_max)
			$graph[] = sprintf('DEF:%s_max=%s:%s:MAX', $k, rrd_escape($rrdinfo['filename']), $k);
	}
	if ($has_min && $has_max || $has_min && $has_avg || $has_avg && $has_max) {
		$n = 1;
		reset($rrdinfo['DS']);
		while (list($k, $v) = each($rrdinfo['DS'])) {
			$graph[] = sprintf('LINE:%s_%s', $k, $has_min ? 'min' : 'avg');
			$graph[] = sprintf('CDEF:%s_var=%s_%s,%s_%s,-', $k, $k, $has_max ? 'max' : 'avg', $k, $has_min ? 'min' : 'avg');
			$graph[] = sprintf('AREA:%s_var#%s::STACK', $k, rrd_get_color($n++, false));
		}
	}

	reset($rrdinfo['DS']);
	$n = 1;
	while (list($k, $v) = each($rrdinfo['DS'])) {
		$graph[] = sprintf('LINE1:%s_avg#%s:%s ', $k, rrd_get_color($n++, true), $k.substr('                  ', 0, $l_max-strlen($k)));
		if (isset($opts['tinylegend']) && $opts['tinylegend'])
			continue;
		if ($has_avg)
			$graph[] = sprintf('GPRINT:%s_avg:AVERAGE:%%5.1lf%%s Avg%s', $k, $has_max || $has_min || $has_avg ? ',' : "\\l");
		if ($has_min)
			$graph[] = sprintf('GPRINT:%s_min:MIN:%%5.1lf%%s Max%s', $k, $has_max || $has_avg ? ',' : "\\l");
		if ($has_max)
			$graph[] = sprintf('GPRINT:%s_max:MAX:%%5.1lf%%s Max%s', $k, $has_avg ? ',' : "\\l");
		if ($has_avg)
			$graph[] = sprintf('GPRINT:%s_avg:LAST:%%5.1lf%%s Last\\l', $k);
	}

	$rrd_cmd = array(RRDTOOL, 'graph', '-', '-a', 'PNG', '-w', $config['rrd_width'], '-h', $config['rrd_height'], '-s', -1*$timespan_def['seconds'], '-t', $rrdfile);
	$rrd_cmd = array_merge($rrd_cmd, $config['rrd_opts'], $opts['rrd_opts'], $graph);

	$cmd = RRDTOOL;
	for ($i = 1; $i < count($rrd_cmd); $i++)
		$cmd .= ' '.escapeshellarg($rrd_cmd[$i]);

	return $cmd;
}

/**
 * Draw RRD file based on it's structure
 * @timespan
 * @host
 * @plugin
 * @pinst
 * @type
 * @tinst
 * @opts
 * @return Commandline to call RRDGraph in order to generate the final graph
 */
function collectd_draw_generic($timespan, $host, $plugin, $pinst = null, $type, $tinst = null) {
	global $config, $GraphDefs;
	$timespan_def = null;
	foreach ($config['timespan'] as &$ts)
		if ($ts['name'] == $timespan)
			$timespan_def = $ts;
	if (is_null($timespan_def))
		$timespan_def = reset($config['timespan']);

	if (!isset($GraphDefs[$type]))
		return false;

	$rrd_file = sprintf('%s/%s%s%s/%s%s%s', $host, $plugin, is_null($pinst) ? '' : '-', $pinst, $type, is_null($tinst) ? '' : '-', $tinst);
	$rrd_cmd  = array(RRDTOOL, 'graph', '-', '-a', 'PNG', '-w', $config['rrd_width'], '-h', $config['rrd_height'], '-s', -1*$timespan_def['seconds'], '-t', $rrd_file);
	$rrd_cmd  = array_merge($rrd_cmd, $config['rrd_opts']);
	$rrd_args = $GraphDefs[$type];

	foreach ($config['datadirs'] as $datadir) {
		$file = $datadir.'/'.$rrd_file.'.rrd';
		if (!is_file($file))
			continue;

		$file = str_replace(":", "\\:", $file);
		$rrd_args = str_replace('{file}', rrd_escape($file), $rrd_args);

		$rrdgraph = array_merge($rrd_cmd, $rrd_args);
		$cmd = RRDTOOL;
		for ($i = 1; $i < count($rrdgraph); $i++)
			$cmd .= ' '.escapeshellarg($rrdgraph[$i]);

		return $cmd;
	}
	return false;
}

/**
 * Draw stack-graph for set of RRD files
 * @opts Graph options like colors
 * @sources List of array(name, file, ds)
 * @return Commandline to call RRDGraph in order to generate the final graph
 */
function collectd_draw_meta_stack(&$opts, &$sources) {
	global $config;
	$timespan_def = null;
	if (!isset($opts['timespan']))
		$timespan_def = reset($config['timespan']);
	else foreach ($config['timespan'] as &$ts)
		if ($ts['name'] == $opts['timespan'])
			$timespan_def = $ts;

	if (!isset($opts['title']))
		$opts['title'] = 'Unknown title';
	if (!isset($opts['rrd_opts']))
		$opts['rrd_opts'] = array();
	if (!isset($opts['colors']))
		$opts['colors'] = array();
	if (isset($opts['logarithmic']) && $opts['logarithmic'])
		array_unshift($opts['rrd_opts'], '-o');

	$cmd = array(RRDTOOL, 'graph', '-', '-a', 'PNG', '-w', $config['rrd_width'], '-h', $config['rrd_height'], '-s', -1*$timespan_def['seconds'], '-t', $opts['title']);
	$cmd = array_merge($cmd, $config['rrd_opts'], $opts['rrd_opts']);
	$max_inst_name = 0;

	foreach($sources as &$inst_data) {
		$inst_name = str_replace('!', '_', $inst_data['name']);
		$file      = $inst_data['file'];
		$ds        = isset($inst_data['ds']) ? $inst_data['ds'] : 'value';

		if (strlen($inst_name) > $max_inst_name)
			$max_inst_name = strlen($inst_name);

		if (!is_file($file))
			continue;

		$cmd[] = 'DEF:'.$inst_name.'_min='.rrd_escape($file).':'.$ds.':MIN';
		$cmd[] = 'DEF:'.$inst_name.'_avg='.rrd_escape($file).':'.$ds.':AVERAGE';
		$cmd[] = 'DEF:'.$inst_name.'_max='.rrd_escape($file).':'.$ds.':MAX';
		$cmd[] = 'CDEF:'.$inst_name.'_nnl='.$inst_name.'_avg,UN,0,'.$inst_name.'_avg,IF';
	}
	$inst_data = end($sources);
	$inst_name = $inst_data['name'];
	$cmd[] = 'CDEF:'.$inst_name.'_stk='.$inst_name.'_nnl';

	$inst_data1 = end($sources);
	while (($inst_data0 = prev($sources)) !== false) {
		$inst_name0 = str_replace('!', '_', $inst_data0['name']);
		$inst_name1 = str_replace('!', '_', $inst_data1['name']);

		$cmd[] = 'CDEF:'.$inst_name0.'_stk='.$inst_name0.'_nnl,'.$inst_name1.'_stk,+';
		$inst_data1 = $inst_data0;
	}

	foreach($sources as &$inst_data) {
		$inst_name = str_replace('!', '_', $inst_data['name']);
		$legend = sprintf('%s', $inst_data['name']);
		while (strlen($legend) < $max_inst_name)
			$legend .= ' ';
		$number_format = isset($opts['number_format']) ? $opts['number_format'] : '%6.1lf';

		if (isset($opts['colors'][$inst_name]))
			$line_color = new CollectdColor($opts['colors'][$inst_name]);
		else
			$line_color = new CollectdColor('random');
		$area_color = new CollectdColor($line_color);
		$area_color->fade();

		$cmd[] = 'AREA:'.$inst_name.'_stk#'.$area_color->as_string();
		$cmd[] = 'LINE1:'.$inst_name.'_stk#'.$line_color->as_string().':'.$legend;
		if (!(isset($opts['tinylegend']) && $opts['tinylegend'])) {
			$cmd[] = 'GPRINT:'.$inst_name.'_min:MIN:'.$number_format.' Min,';
			$cmd[] = 'GPRINT:'.$inst_name.'_avg:AVERAGE:'.$number_format.' Avg,';
			$cmd[] = 'GPRINT:'.$inst_name.'_max:MAX:'.$number_format.' Max,';
			$cmd[] = 'GPRINT:'.$inst_name.'_avg:LAST:'.$number_format.' Last\\l';
		}
	}

	$rrdcmd = RRDTOOL;
	for ($i = 1; $i < count($cmd); $i++)
		$rrdcmd .= ' '.escapeshellarg($cmd[$i]);
	return $rrdcmd;
}

/**
 * Draw stack-graph for set of RRD files
 * @opts Graph options like colors
 * @sources List of array(name, file, ds)
 * @return Commandline to call RRDGraph in order to generate the final graph
 */
function collectd_draw_meta_line(&$opts, &$sources) {
	global $config;
	$timespan_def = null;
	if (!isset($opts['timespan']))
		$timespan_def = reset($config['timespan']);
	else foreach ($config['timespan'] as &$ts)
		if ($ts['name'] == $opts['timespan'])
			$timespan_def = $ts;

	if (!isset($opts['title']))
		$opts['title'] = 'Unknown title';
	if (!isset($opts['rrd_opts']))
		$opts['rrd_opts'] = array();
	if (!isset($opts['colors']))
		$opts['colors'] = array();
	if (isset($opts['logarithmic']) && $opts['logarithmic'])
		array_unshift($opts['rrd_opts'], '-o');

	$cmd = array(RRDTOOL, 'graph', '-', '-a', 'PNG', '-w', $config['rrd_width'], '-h', $config['rrd_height'], '-s', -1*$timespan_def['seconds'], '-t', $opts['title']);
	$cmd = array_merge($cmd, $config['rrd_opts'], $opts['rrd_opts']);
	$max_inst_name = 0;

	foreach ($sources as &$inst_data) {
		$inst_name = str_replace('!', '_', $inst_data['name']);
		$file      = $inst_data['file'];
		$ds        = isset($inst_data['ds']) ? $inst_data['ds'] : 'value';

		if (strlen($inst_name) > $max_inst_name)
			$max_inst_name = strlen($inst_name);

		if (!is_file($file))
			continue;

		$cmd[] = 'DEF:'.$inst_name.'_min='.rrd_escape($file).':'.$ds.':MIN';
		$cmd[] = 'DEF:'.$inst_name.'_avg='.rrd_escape($file).':'.$ds.':AVERAGE';
		$cmd[] = 'DEF:'.$inst_name.'_max='.rrd_escape($file).':'.$ds.':MAX';
	}

	foreach ($sources as &$inst_data) {
		$inst_name = str_replace('!', '_', $inst_data['name']);
		$legend = sprintf('%s', $inst_name);
		while (strlen($legend) < $max_inst_name)
			$legend .= ' ';
		$number_format = isset($opts['number_format']) ? $opts['number_format'] : '%6.1lf';

		if (isset($opts['colors'][$inst_name]))
			$line_color = new CollectdColor($opts['colors'][$inst_name]);
		else
			$line_color = new CollectdColor('random');

		$cmd[] = 'LINE1:'.$inst_name.'_avg#'.$line_color->as_string().':'.$legend;
		if (!(isset($opts['tinylegend']) && $opts['tinylegend'])) {
			$cmd[] = 'GPRINT:'.$inst_name.'_min:MIN:'.$number_format.' Min,';
			$cmd[] = 'GPRINT:'.$inst_name.'_avg:AVERAGE:'.$number_format.' Avg,';
			$cmd[] = 'GPRINT:'.$inst_name.'_max:MAX:'.$number_format.' Max,';
			$cmd[] = 'GPRINT:'.$inst_name.'_avg:LAST:'.$number_format.' Last\\l';
		}
	}

	$rrdcmd = RRDTOOL;
	for ($i = 1; $i < count($cmd); $i++)
		$rrdcmd .= ' '.escapeshellarg($cmd[$i]);
	return $rrdcmd;
}

?>
