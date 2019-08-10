/**
 * collectd - bindings/java/org/collectd/api/DataSet.java
 * Copyright (C) 2009       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

package org.collectd.api;

import java.util.List;
import java.util.ArrayList;

/**
 * Java representation of collectd/src/plugin.h:data_set_t structure.
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
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
