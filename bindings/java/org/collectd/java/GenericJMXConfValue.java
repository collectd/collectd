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

import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.Iterator;
import java.util.ArrayList;

import java.math.BigDecimal;
import java.math.BigInteger;

import javax.management.MBeanServerConnection;
import javax.management.ObjectName;
import javax.management.openmbean.OpenType;
import javax.management.openmbean.CompositeData;
import javax.management.openmbean.InvalidKeyException;

import org.collectd.api.Collectd;
import org.collectd.api.DataSet;
import org.collectd.api.DataSource;
import org.collectd.api.ValueList;
import org.collectd.api.PluginData;
import org.collectd.api.OConfigValue;
import org.collectd.api.OConfigItem;

/**
 * Representation of a &lt;value&nbsp;/&gt; block and query functionality.
 *
 * This class represents a &lt;value&nbsp;/&gt; block in the configuration. As
 * such, the constructor takes an {@link org.collectd.api.OConfigValue} to
 * construct an object of this class.
 *
 * The object can then be asked to query data from JMX and dispatch it to
 * collectd.
 *
 * @see GenericJMXConfMBean
 */
class GenericJMXConfValue
{
  private String _ds_name;
  private DataSet _ds;
  private List<String> _attributes;
  private String _instance_prefix;
  private List<String> _instance_from;
  private boolean _is_table;

  /**
   * Converts a generic (OpenType) object to a number.
   *
   * Returns null if a conversion is not possible or not implemented.
   */
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
    else if (obj instanceof Byte)
    {
      return (new Byte ((Byte) obj));
    }
    else if (obj instanceof Short)
    {
      return (new Short ((Short) obj));
    }
    else if (obj instanceof Integer)
    {
      return (new Integer ((Integer) obj));
    }
    else if (obj instanceof Long)
    {
      return (new Long ((Long) obj));
    }
    else if (obj instanceof Float)
    {
      return (new Float ((Float) obj));
    }
    else if (obj instanceof Double)
    {
      return (new Double ((Double) obj));
    }
    else if (obj instanceof BigDecimal)
    {
      return (BigDecimal.ZERO.add ((BigDecimal) obj));
    }
    else if (obj instanceof BigInteger)
    {
      return (BigInteger.ZERO.add ((BigInteger) obj));
    }

    return (null);
  } /* }}} Number genericObjectToNumber */

  /**
   * Converts a generic list to a list of numbers.
   *
   * Returns null if one or more objects could not be converted.
   */
  private List<Number> genericListToNumber (List<Object> objects) /* {{{ */
  {
    List<Number> ret = new ArrayList<Number> ();
    List<DataSource> dsrc = this._ds.getDataSources ();

    assert (objects.size () == dsrc.size ());

    for (int i = 0; i < objects.size (); i++)
    {
      Number n;

      n = genericObjectToNumber (objects.get (i), dsrc.get (i).getType ());
      if (n == null)
        return (null);
      ret.add (n);
    }

    return (ret);
  } /* }}} List<Number> genericListToNumber */

  /**
   * Converts a list of CompositeData to a list of numbers.
   *
   * From each <em>CompositeData </em> the key <em>key</em> is received and all
   * those values are converted to a number. If one of the
   * <em>CompositeData</em> doesn't have the specified key or one returned
   * object cannot converted to a number then the function will return null.
   */
  private List<Number> genericCompositeToNumber (List<CompositeData> cdlist, /* {{{ */
      String key)
  {
    List<Object> objects = new ArrayList<Object> ();

    for (int i = 0; i < cdlist.size (); i++)
    {
      CompositeData cd;
      Object value;

      cd = cdlist.get (i);
      try
      {
        value = cd.get (key);
      }
      catch (InvalidKeyException e)
      {
        return (null);
      }
      objects.add (value);
    }

    return (genericListToNumber (objects));
  } /* }}} List<Number> genericCompositeToNumber */

  private void submitTable (List<Object> objects, ValueList vl, /* {{{ */
      String instancePrefix)
  {
    List<CompositeData> cdlist;
    Set<String> keySet = null;
    Iterator<String> keyIter;

    cdlist = new ArrayList<CompositeData> ();
    for (int i = 0; i < objects.size (); i++)
    {
      Object obj;

      obj = objects.get (i);
      if (obj instanceof CompositeData)
      {
        CompositeData cd;

        cd = (CompositeData) obj;

        if (i == 0)
          keySet = cd.getCompositeType ().keySet ();

        cdlist.add (cd);
      }
      else
      {
        Collectd.logError ("GenericJMXConfValue: At least one of the "
            + "attributes was not of type `CompositeData', as required "
            + "when table is set to `true'.");
        return;
      }
    }

    assert (keySet != null);

    keyIter = keySet.iterator ();
    while (keyIter.hasNext ())
    {
      String key;
      List<Number> values;

      key = keyIter.next ();
      values = genericCompositeToNumber (cdlist, key);
      if (values == null)
      {
        Collectd.logError ("GenericJMXConfValue: Cannot build a list of "
            + "numbers for key " + key + ". Most likely not all attributes "
            + "have this key.");
        continue;
      }

      if (instancePrefix == null)
        vl.setTypeInstance (key);
      else
        vl.setTypeInstance (instancePrefix + key);
      vl.setValues (values);

      Collectd.dispatchValues (vl);
    }
  } /* }}} void submitTable */

  private void submitScalar (List<Object> objects, ValueList vl, /* {{{ */
      String instancePrefix)
  {
    List<Number> values;

    values = genericListToNumber (objects);
    if (values == null)
    {
      Collectd.logError ("GenericJMXConfValue: Cannot convert list of "
          + "objects to numbers.");
      return;
    }

    if (instancePrefix == null)
      vl.setTypeInstance ("");
    else
      vl.setTypeInstance (instancePrefix);
    vl.setValues (values);

    Collectd.dispatchValues (vl);
  } /* }}} void submitScalar */

  private Object queryAttributeRecursive (CompositeData parent, /* {{{ */
      List<String> attrName)
  {
    String key;
    Object value;

    key = attrName.remove (0);

    try
    {
      value = parent.get (key);
    }
    catch (InvalidKeyException e)
    {
      return (null);
    }

    if (attrName.size () == 0)
    {
      return (value);
    }
    else
    {
      if (value instanceof CompositeData)
        return (queryAttributeRecursive ((CompositeData) value, attrName));
      else
        return (null);
    }
  } /* }}} queryAttributeRecursive */

  private Object queryAttribute (MBeanServerConnection conn, /* {{{ */
      ObjectName objName, String attrName)
  {
    List<String> attrNameList;
    String key;
    Object value;
    String[] attrNameArray;

    attrNameList = new ArrayList<String> ();

    attrNameArray = attrName.split ("\\.");
    key = attrNameArray[0];
    for (int i = 1; i < attrNameArray.length; i++)
      attrNameList.add (attrNameArray[i]);

    try
    {
      try
      {
        value = conn.getAttribute (objName, key);
      }
      catch (javax.management.AttributeNotFoundException e)
      {
        value = conn.invoke (objName, key, /* args = */ null, /* types = */ null);
      }
    }
    catch (Exception e)
    {
      Collectd.logError ("GenericJMXConfValue.query: getAttribute failed: "
          + e);
      return (null);
    }

    if (attrNameList.size () == 0)
    {
      return (value);
    }
    else
    {
      if (value instanceof CompositeData)
        return (queryAttributeRecursive((CompositeData) value, attrNameList));
      else if (value instanceof OpenType)
      {
        OpenType ot = (OpenType) value;
        Collectd.logNotice ("GenericJMXConfValue: Handling of OpenType \""
            + ot.getTypeName () + "\" is not yet implemented.");
        return (null);
      }
      else
      {
        Collectd.logError ("GenericJMXConfValue: Received object of "
            + "unknown class.");
        return (null);
      }
    }
  } /* }}} Object queryAttribute */

  private String join (String separator, List<String> list) /* {{{ */
  {
    StringBuffer sb;

    sb = new StringBuffer ();

    for (int i = 0; i < list.size (); i++)
    {
      if (i > 0)
        sb.append ("-");
      sb.append (list.get (i));
    }

    return (sb.toString ());
  } /* }}} String join */

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

  private Boolean getConfigBoolean (OConfigItem ci) /* {{{ */
  {
    List<OConfigValue> values;
    OConfigValue v;
    Boolean b;

    values = ci.getValues ();
    if (values.size () != 1)
    {
      Collectd.logError ("GenericJMXConfValue: The " + ci.getKey ()
          + " configuration option needs exactly one boolean argument.");
      return (null);
    }

    v = values.get (0);
    if (v.getType () != OConfigValue.OCONFIG_TYPE_BOOLEAN)
    {
      Collectd.logError ("GenericJMXConfValue: The " + ci.getKey ()
          + " configuration option needs exactly one boolean argument.");
      return (null);
    }

    return (new Boolean (v.getBoolean ()));
  } /* }}} String getConfigBoolean */

  /**
   * Constructs a new value with the configured properties.
   */
  public GenericJMXConfValue (OConfigItem ci) /* {{{ */
    throws IllegalArgumentException
  {
    List<OConfigItem> children;
    Iterator<OConfigItem> iter;

    this._ds_name = null;
    this._ds = null;
    this._attributes = new ArrayList<String> ();
    this._instance_prefix = null;
    this._instance_from = new ArrayList<String> ();
    this._is_table = false;

    /*
     * <Value>
     *   Type "memory"
     *   Table true|false
     *   Attribute "HeapMemoryUsage"
     *   Attribute "..."
     *   :
     *   # Type instance:
     *   InstancePrefix "heap-"
     * </Value>
     */
    children = ci.getChildren ();
    iter = children.iterator ();
    while (iter.hasNext ())
    {
      OConfigItem child = iter.next ();

      if (child.getKey ().equalsIgnoreCase ("Type"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._ds_name = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("Table"))
      {
        Boolean tmp = getConfigBoolean (child);
        if (tmp != null)
          this._is_table = tmp.booleanValue ();
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
          this._instance_prefix = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("InstanceFrom"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._instance_from.add (tmp);
      }
      else
        throw (new IllegalArgumentException ("Unknown option: "
              + child.getKey ()));
    }

    if (this._ds_name == null)
      throw (new IllegalArgumentException ("No data set was defined."));
    else if (this._attributes.size () == 0)
      throw (new IllegalArgumentException ("No attribute was defined."));
  } /* }}} GenericJMXConfValue (OConfigItem ci) */

  /**
   * Query values via JMX according to the object's configuration and dispatch
   * them to collectd.
   *
   * @param conn    Connection to the MBeanServer.
   * @param objName Object name of the MBean to query.
   * @param pd      Preset naming components. The members host, plugin and
   *                plugin instance will be used.
   */
  public void query (MBeanServerConnection conn, ObjectName objName, /* {{{ */
      PluginData pd)
  {
    ValueList vl;
    List<DataSource> dsrc;
    List<Object> values;
    List<String> instanceList;
    String instancePrefix;

    if (this._ds == null)
    {
      this._ds = Collectd.getDS (this._ds_name);
      if (this._ds == null)
      {
        Collectd.logError ("GenericJMXConfValue: Unknown type: "
            + this._ds_name);
        return;
      }
    }

    dsrc = this._ds.getDataSources ();
    if (dsrc.size () != this._attributes.size ())
    {
      Collectd.logError ("GenericJMXConfValue.query: The data set "
          + this._ds_name + " has " + this._ds.getDataSources ().size ()
          + " data sources, but there were " + this._attributes.size ()
          + " attributes configured. This doesn't match!");
      this._ds = null;
      return;
    }

    vl = new ValueList (pd);
    vl.setType (this._ds_name);

    /*
     * Build the instnace prefix from the fixed string prefix and the
     * properties of the objName.
     */
    instanceList = new ArrayList<String> ();
    for (int i = 0; i < this._instance_from.size (); i++)
    {
      String propertyName;
      String propertyValue;

      propertyName = this._instance_from.get (i);
      propertyValue = objName.getKeyProperty (propertyName);
      if (propertyValue == null)
      {
        Collectd.logError ("GenericJMXConfMBean: "
            + "No such property in object name: " + propertyName);
      }
      else
      {
        instanceList.add (propertyValue);
      }
    }

    if (this._instance_prefix != null)
      instancePrefix = new String (this._instance_prefix
          + join ("-", instanceList));
    else
      instancePrefix = join ("-", instanceList);

    /*
     * Build a list of `Object's which is then passed to `submitTable' and
     * `submitScalar'.
     */
    values = new ArrayList<Object> ();
    assert (dsrc.size () == this._attributes.size ());
    for (int i = 0; i < this._attributes.size (); i++)
    {
      Object v;

      v = queryAttribute (conn, objName, this._attributes.get (i));
      if (v == null)
      {
        Collectd.logError ("GenericJMXConfValue.query: "
            + "Querying attribute " + this._attributes.get (i) + " failed.");
        return;
      }

      values.add (v);
    }

    if (this._is_table)
      submitTable (values, vl, instancePrefix);
    else
      submitScalar (values, vl, instancePrefix);
  } /* }}} void query */
} /* class GenericJMXConfValue */

/* vim: set sw=2 sts=2 et fdm=marker : */
