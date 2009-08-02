/*
 * collectd/java - org/collectd/java/GenericJMXConfValue.java
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

package org.collectd.java;

import java.util.List;
import java.util.Iterator;
import java.util.ArrayList;

import javax.management.MBeanServerConnection;
import javax.management.ObjectName;

import org.collectd.api.Collectd;
import org.collectd.api.DataSet;
import org.collectd.api.DataSource;
import org.collectd.api.ValueList;
import org.collectd.api.PluginData;
import org.collectd.api.OConfigValue;
import org.collectd.api.OConfigItem;

class GenericJMXConfValue
{
  private String ds_name;
  private DataSet ds;
  private List<String> _attributes;
  private String instance_prefix;

  private Number genericObjectToNumber (Object obj, int ds_type) /* {{{ */
  {
    if (obj instanceof String)
    {
      String str = (String) obj;
      
      try
      {
        if (ds_type == DataSource.TYPE_GAUGE)
          return (new Double (str));
        else
          return (new Long (str));
      }
      catch (NumberFormatException e)
      {
        return (null);
      }
    }
    else if (obj instanceof Integer)
    {
      return (new Integer ((Integer) obj));
    }
    else if (obj instanceof Long)
    {
      return (new Long ((Long) obj));
    }
    else if (obj instanceof Double)
    {
      return (new Double ((Double) obj));
    }

    return (null);
  } /* }}} Number genericObjectToNumber */

  private Number queryAttribute (MBeanServerConnection conn, /* {{{ */
      ObjectName objName, String attrName,
      DataSource dsrc)
  {
    Object attrObj;

    try
    {
      attrObj = conn.getAttribute (objName, attrName);
    }
    catch (Exception e)
    {
      Collectd.logError ("GenericJMXConfValue.query: getAttribute failed: "
          + e);
      return (null);
    }

    return (genericObjectToNumber (attrObj, dsrc.getType ()));
  } /* }}} int queryAttribute */

  private String getConfigString (OConfigItem ci) /* {{{ */
  {
    List<OConfigValue> values;
    OConfigValue v;

    values = ci.getValues ();
    if (values.size () != 1)
    {
      Collectd.logError ("GenericJMXConfValue: The " + ci.getKey ()
          + " configuration option needs exactly one string argument.");
      return (null);
    }

    v = values.get (0);
    if (v.getType () != OConfigValue.OCONFIG_TYPE_STRING)
    {
      Collectd.logError ("GenericJMXConfValue: The " + ci.getKey ()
          + " configuration option needs exactly one string argument.");
      return (null);
    }

    return (v.getString ());
  } /* }}} String getConfigString */

/*
 *    <Value>
 *      Type "memory"
 *      Attribute "HeapMemoryUsage"
 *      # Type instance:
 *      InstancePrefix "heap-"
 *    </Value>
 */
  public GenericJMXConfValue (OConfigItem ci) /* {{{ */
    throws IllegalArgumentException
  {
    List<OConfigItem> children;
    Iterator<OConfigItem> iter;

    this.ds_name = null;
    this.ds = null;
    this._attributes = new ArrayList<String> ();
    this.instance_prefix = null;


    children = ci.getChildren ();
    iter = children.iterator ();
    while (iter.hasNext ())
    {
      OConfigItem child = iter.next ();

      if (child.getKey ().equalsIgnoreCase ("Type"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this.ds_name = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("Attribute"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._attributes.add (tmp);
      }
      else if (child.getKey ().equalsIgnoreCase ("InstancePrefix"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this.instance_prefix = tmp;
      }
      else
        throw (new IllegalArgumentException ("Unknown option: "
              + child.getKey ()));
    }

    if (this.ds_name == null)
      throw (new IllegalArgumentException ("No data set was defined."));
    else if (this._attributes.size () == 0)
      throw (new IllegalArgumentException ("No attribute was defined."));
  } /* }}} GenericJMXConfValue (OConfigItem ci) */

  public void query (MBeanServerConnection conn, ObjectName objName, /* {{{ */
      PluginData pd)
  {
    ValueList vl;
    List<DataSource> dsrc;

    if (this.ds == null)
    {
      this.ds = Collectd.getDS (this.ds_name);
      if (ds == null)
      {
        Collectd.logError ("GenericJMXConfValue: Unknown type: "
            + this.ds_name);
        return;
      }
    }

    dsrc = this.ds.getDataSources ();
    if (dsrc.size () != this._attributes.size ())
    {
      Collectd.logError ("GenericJMXConfValue.query: The data set "
          + ds_name + " has " + this.ds.getDataSources ().size ()
          + " data sources, but there were " + this._attributes.size ()
          + " attributes configured. This doesn't match!");
      this.ds = null;
      return;
    }

    vl = new ValueList (pd);
    vl.setType (this.ds_name);
    vl.setTypeInstance (this.instance_prefix);

    assert (dsrc.size () == this._attributes.size ());
    for (int i = 0; i < this._attributes.size (); i++)
    {
      Number v;

      v = queryAttribute (conn, objName, this._attributes.get (i),
          dsrc.get (i));
      if (v == null)
      {
        Collectd.logError ("GenericJMXConfValue.query: "
            + "Querying attribute " + this._attributes.get (i) + " failed.");
        return;
      }
      Collectd.logDebug ("GenericJMXConfValue.query: dsrc[" + i + "]: v = " + v);
      vl.addValue (v);
    }

    Collectd.dispatchValues (vl);
  } /* }}} void query */
}

/* vim: set sw=2 sts=2 et fdm=marker : */
