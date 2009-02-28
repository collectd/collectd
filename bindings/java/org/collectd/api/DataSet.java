/*
 * collectd/java - org/collectd/api/OConfigItem.java
 * Copyright (C) 2009  Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 */

package org.collectd.api;

import java.util.List;
import java.util.ArrayList;

/**
 * Java representation of collectd/src/plugin.h:data_set_t structure.
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 */
public class DataSet
{
    private String _type;
    private List<DataSource> _ds;

    private DataSet ()
    {
        this._type = null;
        this._ds = new ArrayList<DataSource> ();
    }

    public DataSet (String type)
    {
        this._type = type;
        this._ds = new ArrayList<DataSource> ();
    }

    public DataSet (String type, DataSource dsrc)
    {
        this._type = type;
        this._ds = new ArrayList<DataSource> ();
        this._ds.add (dsrc);
    }

    public DataSet (String type, List<DataSource> ds)
    {
        this._type = type;
        this._ds = ds;
    }

    public void setType (String type)
    {
        this._type = type;
    }

    public String getType ()
    {
        return (this._type);
    }

    public void addDataSource (DataSource dsrc)
    {
        this._ds.add (dsrc);
    }

    public List<DataSource> getDataSources ()
    {
        return (this._ds);
    }

    public String toString ()
    {
        StringBuffer sb = new StringBuffer ();
        int i;

        sb.append (this._type);
        for (i = 0; i < this._ds.size (); i++)
        {
            if (i == 0)
                sb.append ("\t");
            else
                sb.append (", ");
            sb.append (this._ds.get (i).toString ());
        }

        return (sb.toString ());
    }

    static public DataSet parseDataSet (String str)
    {
        DataSet ds = new DataSet ();
        String[] fields;
        int i;

        str = str.trim();
        if (str.length() == 0) {
            return (null);
        }
        if (str.charAt(0) == '#') {
            return (null);
        }

        fields = str.split ("\\s+");
        if (fields.length < 2)
            return (null);

        ds._type = fields[0];

        for (i = 1; i < fields.length; i++) {
            DataSource dsrc;

            dsrc = DataSource.parseDataSource (fields[i]);
            if (dsrc == null)
                break;

            ds._ds.add (dsrc);
        }

        if (i < fields.length)
            return (null);

        return (ds);
    } /* DataSet parseDataSet */
} /* class DataSet */

/* vim: set sw=4 sts=4 et : */
