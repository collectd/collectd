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
}

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
function processReqChange() {
	if (req.readyState == 4) {
		if (req.status == 200) {
			response = req.responseXML.documentElement;
			method = response.getElementsByTagName('method')[0].firstChild.data;
			result = response.getElementsByTagName('result')[0];
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
			if (keepSelection && optCnt == 1 && newOption.value == '@') {
				newOption.setAttribute('style', 'font-style: italic');
				newOption.appendChild(document.createTextNode('Meta graph'));
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

function GraphAppend() {
	var graphs      = document.getElementById('graphs');
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
		if (document.getElementById('logarithmic').checked)
			graph_src += '&logarithmic=1';
		if (document.getElementById('tinylegend').checked)
			graph_src += '&tinylegend=1';
		if (document.getElementById('timespan').selectedIndex >= 0)
			graph_src += '&timespan='+encodeURIComponent(document.getElementById('timespan').options[document.getElementById('timespan').selectedIndex].value);
		var now    = new Date();
		graph_src += '&ts='+now.getTime();

		// Graph container
		newGraph = document.createElement('div');
		newGraph.setAttribute('class', 'graph');
		newGraph.setAttribute('id', graph_id);
		// Graph cell + graph
		newDiv = document.createElement('div');
		newDiv.setAttribute('class', 'graph_img');
		newImg = document.createElement('img');
		newImg.setAttribute('src', graph_src);
		newImg.setAttribute('alt', graph_alt);
		newImg.setAttribute('title', graph_title);
		newDiv.appendChild(newImg);
		newGraph.appendChild(newDiv);
		// Graph tools
		newDiv = document.createElement('div');
		newDiv.setAttribute('class', 'graph_opts');
		// - move up
		newImg = document.createElement('img');
		newImg.setAttribute('src', 'move-up.png');
		newImg.setAttribute('alt', 'UP');
		newImg.setAttribute('title', 'Move graph up');
		newA = document.createElement('a');
		newA.setAttribute('href', 'javascript:GraphMoveUp("'+graph_id+'")');
		newA.appendChild(newImg);
		newDiv.appendChild(newA);
		newDiv.appendChild(document.createElement('br'));
		// - refresh
		newImg = document.createElement('img');
		newImg.setAttribute('src', 'refresh.png');
		newImg.setAttribute('alt', 'R');
		newImg.setAttribute('title', 'Refresh graph');
		newA = document.createElement('a');
		newA.setAttribute('href', 'javascript:GraphRefresh("'+graph_id+'")');
		newA.appendChild(newImg);
		newDiv.appendChild(newA);
		newDiv.appendChild(document.createElement('br'));
		// - remove
		newImg = document.createElement('img');
		newImg.setAttribute('src', 'delete.png');
		newImg.setAttribute('alt', 'RM');
		newImg.setAttribute('title', 'Remove graph');
		newA = document.createElement('a');
		newA.setAttribute('href', 'javascript:GraphRemove("'+graph_id+'")');
		newA.appendChild(newImg);
		newDiv.appendChild(newA);
		newDiv.appendChild(document.createElement('br'));
		// - move down
		newImg = document.createElement('img');
		newImg.setAttribute('src', 'move-down.png');
		newImg.setAttribute('alt', 'DN');
		newImg.setAttribute('title', 'Move graph down');
		newA = document.createElement('a');
		newA.setAttribute('href', 'javascript:GraphMoveDown("'+graph_id+'")');
		newA.appendChild(newImg);
		newDiv.appendChild(newA);
		newGraph.appendChild(newDiv);

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
	RefreshButtons();
}

function GraphRefresh(graph) {
	if (graph == null) {
		var imgs   = document.getElementById('graphs').getElementsByTagName('img');
		var imgCnt = imgs.length;
		var now    = new Date();
		var newTS  = '&ts='+now.getTime();
		while (imgCnt > 0)
			if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph_img') {
				var oldSrc = imgs[imgCnt].src;
				var newSrc = oldSrc.replace(/&ts=[0-9]+/, newTS);
				if (newSrc == oldSrc)
					newSrc = newSrc + newTS;
				imgs[imgCnt].setAttribute('src', newSrc);
			}
	} else if (document.getElementById(graph)) {
		var imgs = document.getElementById(graph).getElementsByTagName('img');
		var imgCnt = imgs.length;
		while (imgCnt > 0)
			if (imgs[--imgCnt].parentNode.getAttribute('class') == 'graph_img') {
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

function GraphRemove(graph) {
	var graphs = document.getElementById('graphs');
	if (document.getElementById(graph)) {
		graphs.removeChild(document.getElementById(graph));
		RefreshButtons();
		if (graphs.getElementsByTagName('div').length == 1)
			document.getElementById('nograph').style.display = 'block';
	}
}

function GraphMoveUp(graph) {
	var graphs    = document.getElementById('graphs');
	var childCnt  = graphs.childNodes.length;
	var prevGraph = null;
	for (i = 0; i < childCnt; i++)
		if (graphs.childNodes[i].nodeName == 'div' || graphs.childNodes[i].nodeName == 'DIV') {
			if (graphs.childNodes[i].id == 'nograph') {
				// Skip
			} else if (graphs.childNodes[i].id == graph) {
				var myGraph = graphs.childNodes[i];
				if (prevGraph) {
					graphs.removeChild(myGraph);
					graphs.insertBefore(myGraph, prevGraph);
				}
				break;
			} else
				prevGraph = graphs.childNodes[i];
		}
}

function GraphMoveDown(graph) {
	var graphs    = document.getElementById('graphs');
	var childCnt  = graphs.childNodes.length;
	var nextGraph = null;
	var myGraph   = null;
	for (i = 0; i < childCnt; i++)
		if (graphs.childNodes[i].nodeName == 'div' || graphs.childNodes[i].nodeName == 'DIV') {
			if (graphs.childNodes[i].id == 'nograph') {
				// Skip
			} else if (graphs.childNodes[i].id == graph) {
				myGraph = graphs.childNodes[i];
			} else if (myGraph) {
				nextGraph = graphs.childNodes[i];
				graphs.removeChild(nextGraph);
				graphs.insertBefore(nextGraph, myGraph);
				break;
			}
		}
}

