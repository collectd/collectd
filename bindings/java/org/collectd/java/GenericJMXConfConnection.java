/**
 * collectd - bindings/java/org/collectd/java/GenericJMXConfConnection.java
 * Copyright (C) 2009-2012  Florian octo Forster
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

package org.collectd.java;

import java.util.List;
import java.util.Map;
import java.util.Iterator;
import java.util.ArrayList;
import java.util.HashMap;

import javax.management.MBeanServerConnection;

import javax.management.remote.JMXServiceURL;
import javax.management.remote.JMXConnector;
import javax.management.remote.JMXConnectorFactory;

import static javax.management.remote.rmi.RMIConnectorServer.RMI_CLIENT_SOCKET_FACTORY_ATTRIBUTE;

import javax.rmi.ssl.SslRMIClientSocketFactory;

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
  private boolean _ssl = false;
  private JMXConnector _jmx_connector = null;
  private MBeanServerConnection _mbean_connection = null;
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

  private String getHost () /* {{{ */
  {
    if (this._host != null)
    {
      return (this._host);
    }

    return Collectd.getHostname();
  } /* }}} String getHost */

  private void connect () /* {{{ */
  {
    JMXServiceURL service_url;
    Map<String,Object> environment;

    // already connected
    if (this._jmx_connector != null) {
      return;
    }

    environment = null;
    if (this._password != null)
    {
      String[] credentials;

      if (this._username == null)
        this._username = new String ("monitorRole");

      credentials = new String[] { this._username, this._password };

      environment = new HashMap<String,Object> ();
      environment.put (JMXConnector.CREDENTIALS, credentials);
      environment.put (JMXConnectorFactory.PROTOCOL_PROVIDER_CLASS_LOADER, this.getClass().getClassLoader());
      if(this._ssl) {
        SslRMIClientSocketFactory rmiClientSocketFactory = new SslRMIClientSocketFactory();
        // Required when JMX is secured with SSL
        // with com.sun.management.jmxremote.ssl=true
        // as shown in http://docs.oracle.com/javase/8/docs/technotes/guides/management/agent.html#gdfvq
        environment.put(RMI_CLIENT_SOCKET_FACTORY_ATTRIBUTE, rmiClientSocketFactory);
        // Required when JNDI Registry is secured with SSL
        // with com.sun.management.jmxremote.registry.ssl=true
        // This property is defined in com.sun.jndi.rmi.registry.RegistryContext.SOCKET_FACTORY
        environment.put("com.sun.jndi.rmi.factory.socket", rmiClientSocketFactory);
      }
    }

    try
    {
      service_url = new JMXServiceURL (this._service_url);
      this._jmx_connector = JMXConnectorFactory.connect (service_url, environment);
      this._mbean_connection = _jmx_connector.getMBeanServerConnection ();
    }
    catch (Exception e)
    {
      Collectd.logError ("GenericJMXConfConnection: "
          + "Creating MBean server connection failed: " + e);
      disconnect ();
      return;
    }
  } /* }}} void connect */

  private void disconnect () /* {{{ */
  {
    try
    {
      if (this._jmx_connector != null) {
        this._jmx_connector.close();
      }
    }
    catch (Exception e)
    {
      // It's fine if close throws an exception
    }

    this._jmx_connector = null;
    this._mbean_connection = null;
  } /* }}} void disconnect */

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
      } else if (child.getKey ().equalsIgnoreCase ("Ssl"))
      {
        String tmp = getConfigString (child);
        if (tmp != null)
          this._ssl = Boolean.valueOf(tmp);
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

    // try to connect
    connect ();

    if (this._mbean_connection == null)
      return;

    Collectd.logDebug ("GenericJMXConfConnection.query: "
        + "Reading " + this._mbeans.size () + " mbeans from "
        + ((this._host != null) ? this._host : "(null)"));

    pd = new PluginData ();
    pd.setHost (this.getHost ());
    pd.setPlugin ("GenericJMX");

    for (int i = 0; i < this._mbeans.size (); i++)
    {
      int status;

      status = this._mbeans.get (i).query (this._mbean_connection, pd,
          this._instance_prefix);
      if (status != 0)
      {
        disconnect ();
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
