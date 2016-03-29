/**
 * collectd - bindings/java/org/collectd/java/JMXMemory.java
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

package org.collectd.java;

import java.util.List;
import java.util.Date;

import java.lang.management.ManagementFactory;
import java.lang.management.MemoryUsage;
import java.lang.management.MemoryMXBean;

import javax.management.MBeanServerConnection;
import javax.management.remote.JMXConnector;
import javax.management.remote.JMXConnectorFactory;
import javax.management.remote.JMXServiceURL;

import org.collectd.api.Collectd;
import org.collectd.api.DataSet;
import org.collectd.api.ValueList;
import org.collectd.api.Notification;
import org.collectd.api.OConfigItem;

import org.collectd.api.CollectdConfigInterface;
import org.collectd.api.CollectdInitInterface;
import org.collectd.api.CollectdReadInterface;
import org.collectd.api.CollectdShutdownInterface;

import org.collectd.api.OConfigValue;
import org.collectd.api.OConfigItem;

public class JMXMemory implements CollectdConfigInterface,
       CollectdInitInterface,
       CollectdReadInterface,
       CollectdShutdownInterface
{
  private String _jmx_service_url = null;
  private MemoryMXBean _mbean = null;

  public JMXMemory ()
  {
    Collectd.registerConfig   ("JMXMemory", this);
    Collectd.registerInit     ("JMXMemory", this);
    Collectd.registerRead     ("JMXMemory", this);
    Collectd.registerShutdown ("JMXMemory", this);
  }

  private void submit (String plugin_instance, MemoryUsage usage) /* {{{ */
  {
    ValueList vl;

    long mem_init;
    long mem_used;
    long mem_committed;
    long mem_max;

    mem_init = usage.getInit ();
    mem_used = usage.getUsed ();
    mem_committed = usage.getCommitted ();
    mem_max = usage.getMax ();

    GenericJMXLogger.logDebug ("JMXMemory plugin: plugin_instance = " + plugin_instance + "; "
        + "mem_init = " + mem_init + "; "
        + "mem_used = " + mem_used + "; "
        + "mem_committed = " + mem_committed + "; "
        + "mem_max = " + mem_max + ";");

    vl = new ValueList ();

    vl.setHost ("localhost");
    vl.setPlugin ("JMXMemory");
    vl.setPluginInstance (plugin_instance);
    vl.setType ("memory");

    if (mem_init >= 0)
    {
      vl.addValue (mem_init);
      vl.setTypeInstance ("init");
      Collectd.dispatchValues (vl);
      vl.clearValues ();
    }

    if (mem_used >= 0)
    {
      vl.addValue (mem_used);
      vl.setTypeInstance ("used");
      Collectd.dispatchValues (vl);
      vl.clearValues ();
    }

    if (mem_committed >= 0)
    {
      vl.addValue (mem_committed);
      vl.setTypeInstance ("committed");
      Collectd.dispatchValues (vl);
      vl.clearValues ();
    }

    if (mem_max >= 0)
    {
      vl.addValue (mem_max);
      vl.setTypeInstance ("max");
      Collectd.dispatchValues (vl);
      vl.clearValues ();
    }
  } /* }}} void submit */

  private int configServiceURL (OConfigItem ci) /* {{{ */
  {
    List<OConfigValue> values;
    OConfigValue cv;

    values = ci.getValues ();
    if (values.size () != 1)
    {
      GenericJMXLogger.logError ("JMXMemory plugin: The JMXServiceURL option needs "
          + "exactly one string argument.");
      return (-1);
    }

    cv = values.get (0);
    if (cv.getType () != OConfigValue.OCONFIG_TYPE_STRING)
    {
      GenericJMXLogger.logError ("JMXMemory plugin: The JMXServiceURL option needs "
          + "exactly one string argument.");
      return (-1);
    }

    _jmx_service_url = cv.getString ();
    return (0);
  } /* }}} int configServiceURL */

  public int config (OConfigItem ci) /* {{{ */
  {
    List<OConfigItem> children;
    int i;

    GenericJMXLogger.logDebug ("JMXMemory plugin: config: ci = " + ci + ";");

    children = ci.getChildren ();
    for (i = 0; i < children.size (); i++)
    {
      OConfigItem child;
      String key;

      child = children.get (i);
      key = child.getKey ();
      if (key.equalsIgnoreCase ("JMXServiceURL"))
      {
        configServiceURL (child);
      }
      else
      {
        GenericJMXLogger.logError ("JMXMemory plugin: Unknown config option: " + key);
      }
    }

    return (0);
  } /* }}} int config */

  public int init () /* {{{ */
  {
    JMXServiceURL service_url;
    JMXConnector connector;
    MBeanServerConnection connection;

    if (_jmx_service_url == null)
    {
      GenericJMXLogger.logError ("JMXMemory: _jmx_service_url == null");
      return (-1);
    }

    try
    {
      service_url = new JMXServiceURL (_jmx_service_url);
      connector = JMXConnectorFactory.connect (service_url);
      connection = connector.getMBeanServerConnection ();
      _mbean = ManagementFactory.newPlatformMXBeanProxy (connection,
          ManagementFactory.MEMORY_MXBEAN_NAME,
          MemoryMXBean.class);
    }
    catch (Exception e)
    {
      GenericJMXLogger.logError ("JMXMemory: Creating MBean failed: " + e);
      return (-1);
    }

    return (0);
  } /* }}} int init */

  public int read () /* {{{ */
  {
    if (_mbean == null)
    {
      GenericJMXLogger.logError ("JMXMemory: _mbean == null");
      return (-1);
    }

    submit ("heap", _mbean.getHeapMemoryUsage ());
    submit ("non_heap", _mbean.getNonHeapMemoryUsage ());

    return (0);
  } /* }}} int read */

  public int shutdown () /* {{{ */
  {
    System.out.print ("org.collectd.java.JMXMemory.Shutdown ();\n");
    _jmx_service_url = null;
    _mbean = null;
    return (0);
  } /* }}} int shutdown */
} /* class JMXMemory */

/* vim: set sw=2 sts=2 et fdm=marker : */
