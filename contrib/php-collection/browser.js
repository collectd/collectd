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

// Toggle visibility of a div
function toggleDiv(divID) {
	var div   = document.getElementById(divID);
	var label = document.getElementById(divID+'_sw');
	var label_txt = null;
	if (div) {
		if (div.style.display == 'none') {
			div.style.display = 'block';
			label_txt = 'Hide';
		} else {
			div.style.display = 'none';
			label_txt = 'Show';
		}
	}
	if (label_txt && label) {
		var childCnt = label.childNodes.length;
		while (childCnt > 0)
			label.removeChild(label.childNodes[--childCnt]);
		label.appendChild(document.createTextNode(label_txt));
	}
	GraphPositionToolbox(null);
}

var req = null;

// DHTML helper code to asynchronous loading of content
function loadXMLDoc(url, query) {
	if (window.XMLHttpRequest) {
		req = new XMLHttpRequest();
		req.onreadystatechange = processReqChange;
		req.open('POST', url, true);
		req.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded; charset=UTF-8'); 
		req.send(query);
	} else if (window.ActiveXObject) {
		req = new ActiveXObject("Microsoft.XMLHTTP");
		if (req) {
			req.onreadystatechange = processReqChange;
			req.open('POST', url, true);
			req.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded; charset=UTF-8'); 
			req.send(query);
		}
	}
}

// DHTML new-content dispatcher
function processReqChange(evt) {
	if (req.readyState == 4) {
		if (req.status == 200) {
			var response = req.responseXML.documentElement;
			var method = response.getElementsByTagName('method')[0].firstChild.data;
			var result = response.getElementsByTagName('result')[0];
			req = null;
			eval(method + '(result)');
		}
	}
}

// Update contents of a <select> drop-down list
function refillSelect(options, select) {
	if (!select)
		return -1;

	var childCnt = select.childNodes.length;
	var oldValue = select.selectedIndex > 0 ? select.options[select.selectedIndex].value : '/';
	while (childCnt > 0)
		select.removeChild(select.childNodes[--childCnt]);

	var optCnt = options ? options.length : 0;
	if (optCnt == 0) {
		select.setAttribute('disabled', 'disabled');
		return -1;
	} else {
		select.removeAttribute('disabled');
		var keepSelection = false;
		if (optCnt == 1) {
			keepSelection = true;
			oldValue = options[0].firstChild ? options[0].firstChild.data : '';
		} else if (oldValue != '/') {
			for (i = 0; i < optCnt && !keepSelection; i++)
				if (oldValue == (options[i].firstChild ? options[i].firstChild.data : ''))
					keepSelection = true;
		}
		newOption = document.createElement("option");
		newOption.value = '/';
		if (keepSelection)
			newOption.setAttribute('disabled', 'disabled');
		else
			newOption.setAttribute('selected', 'selected');
		newOption.setAttribute('style', 'font-style: italic');
		newOption.appendChild(document.createTextNode('- please select -'));
		select.appendChild(newOption);
		for (i = 0; i < optCnt; i++) {
			newOption = document.createElement("option");
			newOption.value = options[i].firstChild ? options[i].firstChild.data : '';
			if (keepSelection && newOption.value == oldValue)
				newOption.setAttribute('selected', 'selected');
			if (newOption.value[0] == '@') {
				newOption.setAttribute('style', 'font-style: italic');
				if (newOption.value == '@' || newOption.value == '@merge')
					newOption.appendChild(document.createTextNode('Meta graph'));
				else if (newOption.value == '@all')
					newOption.appendChild(document.createTextNode('All entries'));
				else if (newOption.value == '@merge_sum')
					newOption.appendChild(document.createTextNode('Meta summed graph'));
				else if (newOption.value == '@merge_avg')
					newOption.appendChild(document.createTextNode('Meta averaged graph'));
				else if (newOption.value == '@merge_stack')
					newOption.appendChild(document.createTextNode('Meta stacked graph'));
				else if (newOption.value == '@merge_line')
					newOption.appendChild(document.createTextNode('Meta lines graph'));
				else
					newOption.appendChild(document.createTextNode(newOption.value));
			} else
				newOption.appendChild(document.createTextNode(newOption.value));
			select.appendChild(newOption);
		}
		return keepSelection ? select.selectedIndex : -1;
	}
}

// Request refresh of host list
function ListRefreshHost() {
	var query = 'action=list_hosts';
	loadXMLDoc(dhtml_url, query);
}

// Handle update to host list
function ListOfHost(response) {
	var select = document.getElementById('host_list');
	var idx = refillSelect(response ? response.getElementsByTagName('option') : null, select);
	if (idx > 0) {
		ListRefreshPlugin();
	} else
		ListOfPlugin(null);
}

// Request refresh of plugin list
function ListRefreshPlugin() {
	var host_list = document.getElementById('host_list');
	var host      = host_list.selectedIndex >= 0 ? host_list.options[host_list.selectedIndex].value : '/';
	if (host != '/') {
		var query = 'action=list_plugins&host='+encodeURIComponent(host);
		loadXMLDoc(dhtml_url, query);
	} else {
		ListOfPlugin(null);
	}
}

// Handle update to plugin list
function ListOfPlugin(response) {
	var select = document.getElementById('plugin_list');
	var idx = refillSelect(response ? response.getElementsByTagName('option') : null, select);
	if (idx > 0) {
		ListRefreshPluginInstance();
	} else
		ListOfPluginInstance(null);
}

// Request refresh of plugin instance list
function ListRefreshPluginInstance() {
	var host_list   = document.getElementById('host_list');
	var host        = host_list.selectedIndex >= 0 ? host_list.options[host_list.selectedIndex].value : '/';
	var plugin_list = document.getElementById('plugin_list');
	var plugin      = plugin_list.selectedIndex >= 0 ? plugin_list.options[plugin_list.selectedIndex].value : '/';
	if (host != '/' && plugin != '/') {
		var query = 'action=list_pinsts&host='+encodeURIComponent(host)+'&plugin='+encodeURIComponent(plugin);
		loadXMLDoc(dhtml_url, query);
	} else {
		ListOfPluginInstance(null);
	}
}

// Handle update of plugin instance list
function ListOfPluginInstance(response) {
	var select = document.getElementById('pinst_list');
	var idx = refillSelect(response ? response.getElementsByTagName('option') : null, select);
	if (idx > 0) {
		ListRefreshType();
	} else
		ListOfType(null);
}

// Request refresh of type list
function ListRefreshType() {
	var host_list   = document.getElementById('host_list');
	var host        = host_list.selectedIndex >= 0 ? host_list.options[host_list.selectedIndex].value : '/';
	var plugin_list = document.getElementById('plugin_list');
	var plugin      = plugin_list.selectedIndex >= 0 ? plugin_list.options[plugin_list.selectedIndex].value : '/';
	var pinst_list  = document.getElementById('pinst_list');
	var pinst       = pinst_list.selectedIndex >= 0 ? pinst_list.options[pinst_list.selectedIndex].value : '/';
	if (host != '/' && plugin != '/' && pinst != '/') {
		var query = 'action=list_types&host='+encodeURIComponent(host)+'&plugin='+encodeURIComponent(plugin)+'&plugin_instance='+encodeURIComponent(pinst);
		loadXMLDoc(dhtml_url, query);
	} else {
		ListOfType(null);
	}
}

// Handle update of type list
function ListOfType(response) {
	var select = document.getElementById('type_list');
	var idx = refillSelect(response ? response.getElementsByTagName('option') : null, select);
	if (idx > 0) {
		ListRefreshTypeInstance();
	} else
		ListOfTypeInstance(null);
}

// Request refresh of type instance list
function ListRefreshTypeInstance() {
	var host_list   = document.getElementById('host_list');
	var host        = host_list.selectedIndex >= 0 ? host_list.options[host_list.selectedIndex].value : '/';
	var plugin_list = document.getElementById('plugin_list');
	var plugin      = plugin_list.selectedIndex >= 0 ? plugin_list.options[plugin_list.selectedIndex].value : '/';
	var pinst_list  = document.getElementById('pinst_list');
	var pinst       = pinst_list.selectedIndex >= 0 ? pinst_list.options[pinst_list.selectedIndex].value : '/';
	var type_list   = document.getElementById('type_list');
	var type        = type_list.selectedIndex >= 0 ? type_list.options[type_list.selectedIndex].value : '/';
	if (host != '/' && plugin != '/' && pinst != '/' && type != '/') {
		var query = 'action=list_tinsts&host='+encodeURIComponent(host)+'&plugin='+encodeURIComponent(plugin)+'&plugin_instance='+encodeURIComponent(pinst)+'&type='+encodeURIComponent(type);
		loadXMLDoc(dhtml_url, query);
	} else {
		ListOfTypeInstance(null);
	}
}

// Handle update of type instance list
function ListOfTypeInstance(response) {
	var select = document.getElementById('tinst_list');
	var idx = refillSelect(response ? response.getElementsByTagName('option') : null, select);
	if (idx > 0) {
		// Enable add button
		RefreshButtons();
	} else {
		// Disable add button
		RefreshButtons();
	}
}

function RefreshButtons() {
	var host_list   = document.getElementById('host_list');
	var host        = host_list.selectedIndex >= 0 ? host_list.options[host_list.selectedIndex].value : '/';
	var plugin_list = document.getElementById('plugin_list');
	var plugin      = plugin_list.selectedIndex >= 0 ? plugin_list.options[plugin_list.selectedIndex].value : '/';
	var pinst_list  = document.getElementById('pinst_list');
	var pinst       = pinst_list.selectedIndex >= 0 ? pinst_list.options[pinst_list.selectedIndex].value : '/';
	var type_list   = document.getElementById('type_list');
	var type        = type_list.selectedIndex >= 0 ? type_list.options[type_list.selectedIndex].value : '/';
	var tinst_list  = document.getElementById('tinst_list');
	var tinst       = tinst_list.selectedIndex >= 0 ? tinst_list.options[tinst_list.selectedIndex].value : '/';
	if (host != '/' && plugin != '/' && pinst != '/' && type != '/' && tinst != '/') {
		document.getElementById('btnAdd').removeAttribute('disabled');
	} else {
		document.getElementById('btnAdd').setAttribute('disabled', 'disabled');
	}

	var graphs = document.getElementById('graphs');
	if (graphs.getElementsByTagName('div').length > 1) {
		document.getElementById('btnClear').removeAttribute('disabled');
		document.getElementById('btnRefresh').removeAttribute('disabled');
	} else {
		document.getElementById('btnClear').setAttribute('disabled', 'disabled');
		document.getElementById('btnRefresh').setAttribute('disabled', 'disabled');
	}
}

var nextGraphId = 1;
var graphList = new Array();

function GraphAppend() {
	var host_list   = document.getElementById('host_list');
	var host        = host_list.selectedIndex >= 0 ? host_list.options[host_list.selectedIndex].value : '/';
	var plugin_list = document.getElementById('plugin_list');
	var plugin      = plugin_list.selectedIndex >= 0 ? plugin_list.options[plugin_list.selectedIndex].value : '/';
	var pinst_list  = document.getElementById('pinst_list');
	var pinst       = pinst_list.selectedIndex >= 0 ? pinst_list.options[pinst_list.selectedIndex].value : '/';
	var type_list   = document.getElementById('type_list');
	var type        = type_list.selectedIndex >= 0 ? type_list.options[type_list.selectedIndex].value : '/';
	var tinst_list  = document.getElementById('tinst_list');
	var tinst       = tinst_list.selectedIndex >= 0 ? tinst_list.options[tinst_list.selectedIndex].value : '/';
	var time_list   = document.getElementById('timespan');
	var timespan    = time_list.selectedIndex >= 0 ? time_list.options[time_list.selectedIndex].value : '';
	var tinyLegend  = document.getElementById('tinylegend').checked;
	var logarithmic = document.getElementById('logarithmic').checked;
	if (host[0] == '@' || plugin[0] == '@' || pinst[0] == '@' || type[0] == '@' || (tinst[0] == '@' && tinst.substr(0, 5) != '@merge')) {
		var query = 'action=list_graphs&host='+encodeURIComponent(host)+'&plugin='+encodeURIComponent(plugin)+'&plugin_instance='+encodeURIComponent(pinst);
		query = query+'&type='+encodeURIComponent(type)+'&type_instance='+encodeURIComponent(tinst)+'&timespan='+encodeURIComponent(timespan);
		query = query+(logarithmic ? '&logarithmic=1' : '')+(tinyLegend ? '&tinylegend=1' : '');
		loadXMLDoc(dhtml_url, query);
	} else
		GraphDoAppend(host, plugin, pinst, type, tinst, timespan, tinyLegend, logarithmic);
}

function ListOfGraph(response) {
	var graphs = response ? response.getElementsByTagName('graph') : null;
	if (graphs && graphs.length > 0) {
		for (i = 0; i < graphs.length; i++)
			GraphDoAppend(graphs[i].getAttribute('host'), graphs[i].getAttribute('plugin'), graphs[i].getAttribute('plugin_instance'),
			              graphs[i].getAttribute('type'), graphs[i].getAttribute('type_instance'), graphs[i].getAttribute('timespan'),
			              graphs[i].getAttribute('tinyLegend') == '1', graphs[i].getAttribute('logarithmic') == '1');
	} else
		alert('No graph found for adding');
}

function GraphDoAppend(host, plugin, pinst, type, tinst, timespan, tinyLegend, logarithmic) {
	var graphs      = document.getElementById('graphs');

	if (host != '/' && plugin != '/' && pinst != '/' && type != '/') {
		var graph_id   = 'graph_'+nextGraphId++;
		var graph_src  = graph_url+'?host='+encodeURIComponent(host)+'&plugin='+encodeURIComponent(plugin)+'&plugin_instance='+encodeURIComponent(pinst)+'&type='+encodeURIComponent(type);
		var graph_alt  = '';
		var grap_title = '';
		if (tinst == '@') {
			graph_alt   = host+'/'+plugin+(pinst.length > 0 ? '-'+pinst : '')+'/'+type;
			graph_title = type+' of '+plugin+(pinst.length > 0 ? '-'+pinst : '')+' plugin for '+host;
		} else {
			graph_alt   = host+'/'+plugin+(pinst.length > 0 ? '-'+pinst : '')+'/'+type+(tinst.length > 0 ? '-'+tinst : '');
			graph_title = type+(tinst.length > 0 ? '-'+tinst : '')+' of '+plugin+(pinst.length > 0 ? '-'+pinst : '')+' plugin for '+host;
			graph_src  += '&type_instance='+encodeURIComponent(tinst);
		}
		if (logarithmic)
			graph_src += '&logarithmic=1';
		if (tinyLegend)
			graph_src += '&tinylegend=1';
		if (timespan)
			graph_src += '&timespan='+encodeURIComponent(timespan);
		var now    = new Date();
		graph_src += '&ts='+now.getTime();
		graphList.push(graph_id+' '+encodeURIComponent(graph_alt)+(logarithmic ? '&logarithmic=1' : '')+(tinyLegend ? '&tinylegend=1' : '')+'&timespan='+encodeURIComponent(timespan));

		// Graph container
		newGraph = document.createElement('div');
		newGraph.setAttribute('class', 'graph');
		newGraph.setAttribute('id', graph_id);
		// Graph cell + graph
		newImg = document.createElement('img');
		newImg.setAttribute('src', graph_src);
		newImg.setAttribute('alt', graph_alt);
		newImg.setAttribute('title', graph_title);
		newImg.setAttribute('onclick', 'GraphToggleTools("'+graph_id+'")');
		newGraph.appendChild(newImg);
		graphs.appendChild(newGraph);
	}
	document.getElementById('nograph').style.display = 'none';
	RefreshButtons();
}

function GraphDropAll() {
	var graphs = document.getElementById('graphs');
	var childCnt = graphs.childNodes.length;
	while (childCnt > 0)
		if (graphs.childNodes[--childCnt].id != 'nograph' && (graphs.childNodes[childCnt].nodeName == 'div' || graphs.childNodes[childCnt].nodeName == 'DIV'))
			graphs.removeChild(graphs.childNodes[childCnt]);
		else if (graphs.childNodes[childCnt].id == 'nograph')
			graphs.childNodes[childCnt].style.display = 'block';
	graphList = new Array();
	RefreshButtons();
}

function GraphToggleTools(graph) {
	var graphId = document.getElementById('ge_graphid').value;
	var ref_img = null;
	if (graphId == graph || graph == '') {
		ref_img = null;
	} else {
		var graphDiv = document.getElementById(graph);
		var imgs     = graphDiv ? graphDiv.getElementsByTagName('img') : null;
		var imgCnt   = imgs ? imgs.length : 0;
		while (imgCnt > 0)
			if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph')
				ref_img = imgs[imgCnt];
	}
	if (ref_img) {
		var ts_sel  =  document.getElementById('ge_timespan');
		var src_url = ref_img.src;
		var ge      = document.getElementById('ge');
		// Fix field values
		var ts = src_url.match(/&timespan=[^&]*/);
		ts = ts ? ts[0].substr(10) : '';
		document.getElementById('ge_graphid').value = graph;
		document.getElementById('ge_tinylegend').checked = src_url.match(/&tinylegend=1/);
		document.getElementById('ge_logarithmic').checked = src_url.match(/&logarithmic=1/);
		for (i = 0; i < ts_sel.options.length; i++)
			if (ts_sel.options[i].value == ts) {
				ts_sel.selectedIndex = i;
				break;
			}
		// show tools box and position it properly
		ge.style.display = 'table';
		GraphPositionToolbox(ref_img);
	} else {
		// hide tools box
		document.getElementById('ge').style.display = 'none';
		document.getElementById('ge_graphid').value = '';
	}
}

function GraphPositionToolbox(ref_img) {
	var ge      = document.getElementById('ge');
	if (ge.style.display != 'none') {
		var wl = 0; var wt = 0;
		var x = ref_img;
		if (ref_img == null) {
			var graphDiv = document.getElementById(document.getElementById('ge_graphid').value);
			var imgs     = graphDiv ? graphDiv.getElementsByTagName('img') : null;
			var imgCnt   = imgs ? imgs.length : 0;
			while (imgCnt > 0)
				if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph')
					ref_img = imgs[imgCnt];

			if (ref_img == null) {
				document.getElementById('ge_graphid').value = '';
				ge.style.display = 'none';
				return;
			} else
				x = ref_img;
		}
		while (x != null) {
			wl += x.offsetLeft;
			wt += x.offsetTop;
			x = x.offsetParent;
		}
		ge.style.left    = (wl + (ref_img.offsetWidth - ge.offsetWidth) / 2)+'px';
		ge.style.top     = (wt + (ref_img.offsetHeight - ge.offsetHeight) / 2)+'px';
	}
}

function GraphRefreshAll() {
	var imgs   = document.getElementById('graphs').getElementsByTagName('img');
	var imgCnt = imgs.length;
	var now    = new Date();
	var newTS  = '&ts='+now.getTime();
	while (imgCnt > 0)
		if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph') {
			var oldSrc = imgs[imgCnt].src;
			var newSrc = oldSrc.replace(/&ts=[0-9]+/, newTS);
			if (newSrc == oldSrc)
				newSrc = newSrc + newTS;
			imgs[imgCnt].setAttribute('src', newSrc);
		}
}

function GraphRefresh(graph) {
	var graphElement = null;
	if (graph == null) {
		var graphId = document.getElementById('ge_graphid').value;
		if (graphId != '')
			graphElement = document.getElementById(graphId);
	} else 
		graphElement = document.getElementById(graph);
	if (graphElement != null) {
		var imgs = graphElement.getElementsByTagName('img');
		var imgCnt = imgs.length;
		while (imgCnt > 0)
			if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph') {
				var now    = new Date();
				var newTS  = '&ts='+now.getTime();
				var oldSrc = imgs[imgCnt].src;
				var newSrc = oldSrc.replace(/&ts=[0-9]+/, newTS);
				if (newSrc == oldSrc)
					newSrc = newSrc+newTS;
				imgs[imgCnt].setAttribute('src', newSrc);
				break;
			}
	}
}

function GraphAdjust(graph) {
	var graphId = graph == null ? document.getElementById('ge_graphid').value : graph;
	var graphElement = document.getElementById(graphId);
	if (graphElement != null) {
		var time_list   = document.getElementById('ge_timespan');
		var timespan    = time_list.selectedIndex >= 0 ? time_list.options[time_list.selectedIndex].value : '';
		var tinyLegend  = document.getElementById('ge_tinylegend').checked;
		var logarithmic = document.getElementById('ge_logarithmic').checked
		var imgs = graphElement.getElementsByTagName('img');
		var imgCnt = imgs.length;
		var ref_img     = null;
		while (imgCnt > 0)
			if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph') {
				var now    = new Date();
				var newTS  = '&ts='+now.getTime();
				var oldSrc = imgs[imgCnt].src;
				var newSrc = oldSrc.replace(/&ts=[^&]*/, newTS);
				if (newSrc == oldSrc)
					newSrc = newSrc+newTS;
				newSrc     = newSrc.replace(/&logarithmic=[^&]*/, '');
				if (logarithmic)
					newSrc += '&logarithmic=1';
				newSrc     = newSrc.replace(/&tinylegend=[^&]*/, '');
				if (tinyLegend)
					newSrc += '&tinylegend=1';
				newSrc     = newSrc.replace(/&timespan=[^&]*/, '');
				if (timespan)
					newSrc += '&timespan='+encodeURIComponent(timespan);
				imgs[imgCnt].setAttribute('src', newSrc);

				var myList = Array();
				for (i = 0; i < graphList.length; i++)
					if (graphList[i].substring(0, graphId.length) == graphId && graphList[i].charAt(graphId.length) == ' ') {
						newSrc = graphList[i];
						newSrc = newSrc.replace(/&logarithmic=[^&]*/, '');
						newSrc = newSrc.replace(/&tinylegend=[^&]*/, '');
						newSrc = newSrc.replace(/&timespan=[^&]*/, '');
						newSrc = newSrc+(logarithmic ? '&logarithmic=1' : '')+(tinyLegend ? '&tinylegend=1' : '')+'&timespan='+encodeURIComponent(timespan);
						myList.push(newSrc);
						continue;
					} else
						myList.push(graphList[i]);
				graphList = myList;
				window.setTimeout("GraphPositionToolbox(null)", 10);
				// GraphPositionToolbox(imgs[imgCnt]);
				break;
			}
	}
}

function GraphRemove(graph) {
	var graphs = document.getElementById('graphs');
	var graphId = graph == null ? document.getElementById('ge_graphid').value : graph;
	var graphElement = document.getElementById(graphId);
	if (graphElement) {
		GraphToggleTools('');
		graphs.removeChild(graphElement);
		RefreshButtons();
		if (graphs.getElementsByTagName('div').length == 1)
			document.getElementById('nograph').style.display = 'block';

		var myList = Array();
		for (i = 0; i < graphList.length; i++)
			if (graphList[i].substring(0, graphId.length) == graphId && graphList[i].charAt(graphId.length) == ' ')
				continue;
			else
				myList.push(graphList[i]);
		graphList = myList;
	}
}

function GraphMoveUp(graph) {
	var graphs    = document.getElementById('graphs');
	var graphId   = graph == null ? document.getElementById('ge_graphid').value : graph;
	var childCnt  = graphs.childNodes.length;
	var prevGraph = null;
	for (i = 0; i < childCnt; i++)
		if (graphs.childNodes[i].nodeName == 'div' || graphs.childNodes[i].nodeName == 'DIV') {
			if (graphs.childNodes[i].id == 'nograph') {
				// Skip
			} else if (graphs.childNodes[i].id == graphId) {
				var myGraph = graphs.childNodes[i];
				if (prevGraph) {
					graphs.removeChild(myGraph);
					graphs.insertBefore(myGraph, prevGraph);
				}
				break;
			} else
				prevGraph = graphs.childNodes[i];
		}
	for (i = 0; i < graphList.length; i++)
		if (graphList[i].substring(0, graphId.length) == graphId && graphList[i].charAt(graphId.length) == ' ') {
			if (i > 0) {
				var tmp = graphList[i-1];
				graphList[i-1] = graphList[i];
				graphList[i]   = tmp;
			}
			break;
		}
	GraphPositionToolbox(null);
}

function GraphMoveDown(graph) {
	var graphs    = document.getElementById('graphs');
	var graphId   = graph == null ? document.getElementById('ge_graphid').value : graph;
	var childCnt  = graphs.childNodes.length;
	var nextGraph = null;
	var myGraph   = null;
	for (i = 0; i < childCnt; i++)
		if (graphs.childNodes[i].nodeName == 'div' || graphs.childNodes[i].nodeName == 'DIV') {
			if (graphs.childNodes[i].id == 'nograph') {
				// Skip
			} else if (graphs.childNodes[i].id == graphId) {
				myGraph = graphs.childNodes[i];
			} else if (myGraph) {
				nextGraph = graphs.childNodes[i];
				graphs.removeChild(nextGraph);
				graphs.insertBefore(nextGraph, myGraph);
				break;
			}
		}
	for (i = 0; i < graphList.length; i++)
		if (graphList[i].substring(0, graphId.length) == graphId && graphList[i].charAt(graphId.length) == ' ') {
			if (i+1 < graphList.length) {
				var tmp = graphList[i+1];
				graphList[i+1] = graphList[i];
				graphList[i]   = tmp;
			}
			break;
		}
	GraphPositionToolbox(null);
}

function GraphListFromCookie(lname) {
	if (document.cookie.length > 0) {
		var cname= 'graphLst'+lname+'=';
		var cookies = document.cookie.split('; ');
		for (i = 0; i < cookies.length; i++)
			if (cookies[i].substring(0, cname.length) == cname)
				return cookies[i].substring(cname.length).split('/');
	}
	return new Array();
}

function GraphListNameSort(a, b) {
	if (a[0] > b[0])
		return 1
	else if (a[0] < b[0])
		return -1;
	else
		return 0;
}

function GraphListRefresh() {
	var select   = document.getElementById('GraphList');
	var childCnt = select.childNodes.length;
	var oldValue = select.selectedIndex > 0 ? select.options[select.selectedIndex].value : '/';
	while (childCnt > 0)
		select.removeChild(select.childNodes[--childCnt]);

	// Determine available names
	var options = new Array();
	if (document.cookie.length > 0) {
		var cookies = document.cookie.split('; ');
		for (i = 0; i < cookies.length; i++)
			if (cookies[i].substring(0, 8) == 'graphLst') {
				var p = cookies[i].indexOf('=');
				if (p < 0)
					continue;
				options.push(new Array(cookies[i].substring(8, p), cookies[i].substring(p+1).split('/').length));
			}
	}
	options.sort(GraphListNameSort);

	var optCnt  = options ? options.length : 0;
	if (optCnt == 0) {
		select.setAttribute('disabled', 'disabled');
		return -1;
	} else {
		select.removeAttribute('disabled');
		for (i = 0; i < optCnt; i++) {
			newOption = document.createElement("option");
			newOption.value = options[i][0];
			if (newOption.value == oldValue)
				newOption.setAttribute('selected', 'selected');
			if (options[i][1] == 1)
				newOption.appendChild(document.createTextNode(newOption.value+' (1 graph)'));
			else
				newOption.appendChild(document.createTextNode(newOption.value+' ('+options[i][1]+' graphs)'));
			select.appendChild(newOption);
		}
		return select.selectedIndex;
	}
}

function GraphListCheckName(doalert) {
	var lname = document.getElementById('GraphListName');
	if (lname) {
		if (lname.value.match(/^[a-zA-Z0-9_-]+$/)) {
			lname.style.backgroundColor = '';
			return lname.value;
		} else {
			lname.style.backgroundColor = '#ffdddd';
			if (doalert && lname.value.length == 0)
				alert('Graph list name is empty.\n\n'+
				      'Please fill in a name and try again.');
			else if (doalert)
				alert('Graph list name contains non-permitted character.\n\n'+
				      'Only anlphanumerical characters (a-z, A-Z, 0-9), hyphen (-) and underscore (_) are permitted.\n'+
				      'Please correct and try again.');
			lname.focus();
		}
	}
	return '';
}

function GraphSave() {
	var lstName = GraphListCheckName(true);
	if (lstName.length == 0)
		return;
	if (graphList.length > 0) {
		// Save graph list to cookie
		var str = '';
		for (i = 0; i < graphList.length; i++) {
			var g = graphList[i].indexOf(' ');
			if (i > 0)
				str += '/';
			str += graphList[i].substring(g+1);
		}

		document.cookie = 'graphLst'+lstName+'='+str;
		if (GraphListFromCookie(lstName).length == 0)
			alert("Failed to save graph list '"+lstName+"' to cookie.");
		else
			alert("Successfully saved current graph list.");
	} else {
		document.cookie = 'graphLst'+lstName+'=; expires='+new Date().toGMTString();
		alert("Cleared saved graph list.");
	}
	GraphListRefresh();
}

function GraphDrop() {
	var cname = document.getElementById('GraphList');
	if (cname && cname.selectedIndex >= 0) {
		cname = cname.options[cname.selectedIndex].value;
		document.cookie = 'graphLst'+cname+'=; expires='+new Date().toGMTString();
		GraphListRefresh();
	} else
		return;
}

function GraphLoad() {
	var cname = document.getElementById('GraphList');
	if (cname && cname.selectedIndex >= 0)
		cname = cname.options[cname.selectedIndex].value;
	else
		return;
	// Load graph list from cookie
	var grLst = GraphListFromCookie(cname);
	var oldLength = graphList.length;
	for (i = 0; i < grLst.length; i++) {
		var host        = '';
		var plugin      = '';
		var pinst       = '';
		var type        = '';
		var tinst       = '';
		var timespan    = '';
		var logarithmic = false;
		var tinyLegend  = false;
		var graph = grLst[i].split('&');
		for (j = 0; j < graph.length; j++)
			if (graph[j] == 'logarithmic=1')
				logarithmic = true;
			else if (graph[j] == 'tinylegend=1')
				tinyLegend = true;
			else if (graph[j].substring(0, 9) == 'timespan=')
				timespan = decodeURIComponent(graph[j].substring(9));
		graph = decodeURIComponent(graph[0]).split('/');
		host = graph[0];
		if (graph.length > 1) {
			var g = graph[1].indexOf('-');
			if (g >= 0) {
				plugin = graph[1].substring(0, g);
				pinst  = graph[1].substring(g+1);
			} else
				plugin = graph[1];
		}
		if (graph.length > 2) {
			var g = graph[2].indexOf('-');
			if (g >= 0) {
				type  = graph[2].substring(0, g);
				tinst = graph[2].substring(g+1);
			} else
				type  = graph[2];
		}

		if (host && plugin && type)
			GraphDoAppend(host, plugin, pinst, type, tinst, timespan, tinyLegend, logarithmic);
	}
	if (grLst.length == 0)
		alert("No list '"+cname+"' found for loading.");
	else if (grLst.length + oldLength != graphList.length)
		alert("Could not load all graphs, probably damaged cookie.");
}

