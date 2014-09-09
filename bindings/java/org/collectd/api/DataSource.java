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

/**
 * Java representation of collectd/src/plugin.h:data_source_t structure. 
 */
public class DataSource {
    public static final int TYPE_COUNTER  = 0;
    public static final int TYPE_GAUGE    = 1;
    public static final int TYPE_DERIVE   = 2;
    public static final int TYPE_ABSOLUTE = 3;

    static final String COUNTER  = "COUNTER";
    static final String GAUGE    = "GAUGE";
    static final String DERIVE   = "DERIVE";
    static final String ABSOLUTE = "ABSOLUTE";

    static final String NAN = "U";
    private static final String[] TYPES = { COUNTER, GAUGE, DERIVE, ABSOLUTE };

    String _name;
    int _type;
    double _min;
    double _max;

    public DataSource (String name, int type, double min, double max) {
        this._name = name;
        this._type = TYPE_GAUGE;
        if (type == TYPE_COUNTER)
            this._type = TYPE_COUNTER;
        else if (type == TYPE_DERIVE)
            this._type = TYPE_DERIVE;
        else if (type == TYPE_ABSOLUTE)
            this._type = TYPE_ABSOLUTE;
        this._min = min;
        this._max = max;
    }

    /* Needed in parseDataSource below. Other code should use the above
     * constructor or `parseDataSource'. */
    private DataSource () {
        this._type = TYPE_GAUGE;
    }

    public String getName() {
        return _name;
    }

    public void setName(String name) {
        _name = name;
    }

    public int getType() {
        return _type;
    }

    public void setType(int type) {
        _type = type;
    }

    public double getMin() {
        return _min;
    }

    public void setMin(double min) {
        _min = min;
    }

    public double getMax() {
        return _max;
    }

    public void setMax(double max) {
        _max = max;
    }

    static double toDouble(String val) {
        if (val.equals(NAN)) {
            return Double.NaN;
        }
        else {
            return Double.parseDouble(val);
        }
    }

    private String asString(double val) {
        if (Double.isNaN(val)) {
            return NAN;
        }
        else {
            return String.valueOf(val);
        }
    }

    public String toString() {
        StringBuffer sb = new StringBuffer();
        final char DLM = ':';
        sb.append(_name).append(DLM);
        sb.append(TYPES[_type]).append(DLM);
        sb.append(asString(_min)).append(DLM);
        sb.append(asString(_max));
        return sb.toString();
    }

    static public DataSource parseDataSource (String str)
    {
        String[] fields;
        int str_len = str.length ();
        DataSource dsrc = new DataSource ();

        /* Ignore trailing commas. This makes it easier for parsing code. */
        if (str.charAt (str_len - 1) == ',') {
            str = str.substring (0, str_len - 1);
        }

        fields = str.split(":");
        if (fields.length != 4)
            return (null);

        dsrc._name = fields[0];

        if (fields[1].equals (DataSource.GAUGE)) {
            dsrc._type  = TYPE_GAUGE;
        }
        else {
            dsrc._type  = TYPE_COUNTER;
        }

        dsrc._min =  toDouble (fields[2]);
        dsrc._max =  toDouble (fields[3]);

        return (dsrc);
    } /* DataSource parseDataSource */
}

/* vim: set sw=4 sts=4 et : */
