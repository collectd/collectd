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

import java.util.ArrayList;
import java.util.List;

/**
 * Java representation of collectd/src/plugin.h:value_list_t structure.
 */
public class ValueList extends PluginData {

    List<Number> _values = new ArrayList<Number>();
    List<DataSource> _ds = new ArrayList<DataSource>();

    long _interval;

    public ValueList() {
        
    }

    public ValueList(PluginData pd) {
        super(pd);
    }

    public ValueList(ValueList vl) {
        this((PluginData)vl);
        _interval = vl._interval;
        _values.addAll(vl.getValues());
        _ds.addAll(vl._ds);
    }

    public List<Number> getValues() {
        return _values;
    }

    public void setValues(List<Number> values) {
        _values = values;
    }

    public void addValue(Number value) {
        _values.add(value);
    }

    /* Used by the network parsing code */
    public void clearValues () {
        _values.clear ();
    }

    public List<DataSource> getDataSource() {
        if (_ds.size() > 0) {
            return _ds;
        }
        else {
            return null;
        }
    }

    public void setDataSource(List<DataSource> ds) {
        _ds = ds;
    }

    public long getInterval() {
        return _interval;
    }

    public void setInterval(long interval) {
        _interval = interval;
    }

    public String toString() {
        StringBuffer sb = new StringBuffer(super.toString());
        sb.append("=[");
        List<DataSource> ds = getDataSource();
        int size = _values.size();
        for (int i=0; i<size; i++) {
            Number val = _values.get(i);
            String name;
            if (ds == null) {
                name = "unknown" + i;
            }
            else {
                name = ds.get(i).getName();
            }
            sb.append(name).append('=').append(val);
            if (i < size-1) {
                sb.append(',');
            }
        }
        sb.append("]");
        return sb.toString();
    }
}
