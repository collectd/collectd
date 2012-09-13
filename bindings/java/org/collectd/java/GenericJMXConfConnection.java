/*
 * collectd/java - org/collectd/java/GenericJMXConfConnection.java
 * Copyright (C) 2009,2010  Florian octo Forster
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
import java.util.Map;
import java.util.Iterator;
import java.util.ArrayList;
import java.util.HashMap;

import javax.management.MBeanServerConnection;
import javax.management.ObjectName;
import javax.management.MalformedObjectNameException;

import javax.management.remote.JMXServiceURL;
import javax.management.remote.JMXConnector;
import javax.management.remote.JMXConnectorFactory;

import org.collectd.api.Collectd;
import org.collectd.api.PluginData;
import org.collectd.api.OConfigValue;
import org.collectd.api.OConfigItem;

class GenericJMXConfConnection
{
  private String _username = null;
  private String _password = null;
  private String _host = null;
  private String _instance_prefix = null;
  private String _service_url = null;
  private MBeanServerConnection _jmx_connection = null;
  private List<GenericJMXConfMBean> _mbeans = null;

  /*
   * private methods
   */
  private String getConfigString (OConfigItem ci) /* {{{ */
  {
    List<OConfigValue> values;
    OConfigValue v;

    values = ci.getValues ();
    if (values.size () != 1)
    {
      Collectd.logError ("GenericJMXConfConnection: The " + ci.getKey ()
          + " configuration option needs exactly one string argument.");
      return (null);
    }

    v = values.get (0);
    if (v.getType () != OConfigValue.OCONFIG_TYPE_STRING)
    {
      Collectd.logError ("GenericJMXConfConnection: The " + ci.getKey ()
          + " configuration option needs exactly one string argument.");
      return (null);
    }

    return (v.getString ());
  } /* }}} String getConfigString */

private void connect () /* {{{ */
{
  JMXServiceURL service_url;
  JMXConnector connector;
  Map environment;

  if (_jmx_connection != null)
    return;

  environment = null;
  if (this._password != null)
  {
    String[] credentials;

    if (this._username == null)
      this._username = new String ("monitorRole");

    credentials = new String[] { this._username, this._password };

    environment = new HashMap ();
    environment.put (JMXConnector.CREDENTIALS, credentials);
  }

  try
  {
    service_url = new JMXServiceURL (this._service_url);
    connector = JMXConnectorFactory.connect (service_url, environment);
    _jmx_connection = connector.getMBeanServerConnection ();
  }
  catch (Exception e)
  {
    Collectd.logError ("GenericJMXConfConnection: "
        + "Creating MBean server connection failed: " + e);
    return;
  }
} /* }}} void connect */

/*
 * public methods
 *
 * <Connection>
 *   Host "tomcat0.mycompany"
 *   ServiceURL "service:jmx:rmi:///jndi/rmi://localhost:17264/jmxrmi"
 *   Collect "java.lang:type=GarbageCollector,name=Copy"
 *   Collect "java.lang:type=Memory"
 * </Connection>
 *
 */
  public GenericJMXConfConnection (OConfigItem ci) /* {{{ */
    throws IllegalArgumentException
  {
    List<OConfigItem> children;
    Iterator<OConfigItem> iter;

    this._mbeans = new ArrayList<GenericJMXConfMBean> ();

    children = ci.getChildren ();
    iter = children.iterator ();
    while (iter.hasNext ())
    {
      OConfigItem child = iter.next ();

      if (child.getKey ().equalsIgnoreCase ("Host"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._host = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("User"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._username = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("Password"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._password = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("ServiceURL"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._service_url = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("InstancePrefix"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._instance_prefix = tmp;
      }
      else if (child.getKey ().equalsIgnoreCase ("Collect"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
        {
          GenericJMXConfMBean mbean;

          mbean = GenericJMX.getMBean (tmp);
          if (mbean == null)
            throw (new IllegalArgumentException ("No such MBean defined: "
                  + tmp + ". Please make sure all `MBean' blocks appear "
                  + "before (above) all `Connection' blocks."));
          Collectd.logDebug ("GenericJMXConfConnection: " + this._host + ": Add " + tmp);
          this._mbeans.add (mbean);
        }
      }
      else
        throw (new IllegalArgumentException ("Unknown option: "
              + child.getKey ()));
    }

    if (this._service_url == null)
      throw (new IllegalArgumentException ("No service URL was defined."));
    if (this._mbeans.size () == 0)
      throw (new IllegalArgumentException ("No valid collect statement "
            + "present."));
  } /* }}} GenericJMXConfConnection (OConfigItem ci) */

  public void query () /* {{{ */
  {
    PluginData pd;

    connect ();

    if (this._jmx_connection == null)
      return;

    Collectd.logDebug ("GenericJMXConfConnection.query: "
        + "Reading " + this._mbeans.size () + " mbeans from "
        + ((this._host != null) ? this._host : "(null)"));

    pd = new PluginData ();
    pd.setHost ((this._host != null) ? this._host : "localhost");
    pd.setPlugin ("GenericJMX");

    for (int i = 0; i < this._mbeans.size (); i++)
    {
      int status;

      status = this._mbeans.get (i).query (this._jmx_connection, pd,
          this._instance_prefix);
      if (status != 0)
      {
        this._jmx_connection = null;
        return;
      }
    } /* for */
  } /* }}} void query */

  public String toString ()
  {
    return (new String ("host = " + this._host + "; "
          + "url = " + this._service_url));
  }
}

/* vim: set sw=2 sts=2 et fdm=marker : */
