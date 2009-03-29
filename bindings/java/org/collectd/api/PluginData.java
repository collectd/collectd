/*
 * jcollectd
 * Copyright (C) 2009 Hyperic, Inc.
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
 */

package org.collectd.api;

import java.util.Date;

/**
 * Shared members of value_list_t and notification_t structures.
 */
public class PluginData {

    protected long _time = 0;
    protected String _host;
    protected String _plugin;
    protected String _pluginInstance = "";
    protected String _type = "";
    protected String _typeInstance = "";

    public PluginData() {
        
    }

    public PluginData(PluginData pd) {
        _time = pd._time;
        _host = pd._host;
        _plugin = pd._plugin;
        _pluginInstance = pd._pluginInstance;
        _type = pd._type;
        _typeInstance = pd._typeInstance;
    }

    public long getTime() {
        return _time;
    }

    public void setTime(long time) {
        _time = time;
    }

    public String getHost() {
        return _host;
    }

    public void setHost(String host) {
        _host = host;
    }

    public String getPlugin() {
        return _plugin;
    }

    public void setPlugin(String plugin) {
        _plugin = plugin;
    }

    public String getPluginInstance() {
        return _pluginInstance;
    }

    public void setPluginInstance(String pluginInstance) {
        _pluginInstance = pluginInstance;
    }

    public String getType() {
        return _type;
    }

    public void setType(String type) {
        _type = type;
    }

    public String getTypeInstance() {
        return _typeInstance;
    }

    public void setTypeInstance(String typeInstance) {
        _typeInstance = typeInstance;
    }

    public boolean defined(String val) {
        return (val != null) && (val.length() > 0);
    }

    public String getSource() {
        final char DLM = '/';
        StringBuffer sb = new StringBuffer();
        if (defined(_host)) {
            sb.append(_host);
        }
        if (defined(_plugin)) {
            sb.append(DLM).append(_plugin);
        }
        if (defined(_pluginInstance)) {
            sb.append(DLM).append(_pluginInstance);
        }
        if (defined(_type)) {
            sb.append(DLM).append(_type);
        }
        if (defined(_typeInstance)) {
            sb.append(DLM).append(_typeInstance);
        }
        return sb.toString();        
    }

    public String toString() {
        StringBuffer sb = new StringBuffer();
        sb.append('[').append(new Date(_time)).append("] ");
        sb.append(getSource());
        return sb.toString();
    }
}
