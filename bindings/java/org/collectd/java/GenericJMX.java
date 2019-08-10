/**
 * collectd - bindings/java/org/collectd/java/GenericJMX.java
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
import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

import org.collectd.api.Collectd;
import org.collectd.api.CollectdConfigInterface;
import org.collectd.api.CollectdInitInterface;
import org.collectd.api.CollectdReadInterface;
import org.collectd.api.CollectdShutdownInterface;
import org.collectd.api.OConfigValue;
import org.collectd.api.OConfigItem;

public class GenericJMX implements CollectdConfigInterface,
       CollectdReadInterface,
       CollectdShutdownInterface
{
  static private Map<String,GenericJMXConfMBean> _mbeans
    = new TreeMap<String,GenericJMXConfMBean> ();

  private List<GenericJMXConfConnection> _connections = null;

  public GenericJMX ()
  {
    Collectd.registerConfig   ("GenericJMX", this);
    Collectd.registerRead     ("GenericJMX", this);
    Collectd.registerShutdown ("GenericJMX", this);

    this._connections = new ArrayList<GenericJMXConfConnection> ();
  }

  public int config (OConfigItem ci) /* {{{ */
  {
    List<OConfigItem> children;
    int i;

    Collectd.logDebug ("GenericJMX plugin: config: ci = " + ci + ";");

    children = ci.getChildren ();
    for (i = 0; i < children.size (); i++)
    {
      OConfigItem child;
      String key;

      child = children.get (i);
      key = child.getKey ();
      if (key.equalsIgnoreCase ("MBean"))
      {
        try
        {
          GenericJMXConfMBean mbean = new GenericJMXConfMBean (child);
          putMBean (mbean);
        }
        catch (IllegalArgumentException e)
        {
          Collectd.logError ("GenericJMX plugin: "
              + "Evaluating `MBean' block failed: " + e);
        }
      }
      else if (key.equalsIgnoreCase ("Connection"))
      {
        try
        {
          GenericJMXConfConnection conn = new GenericJMXConfConnection (child);
          this._connections.add (conn);
        }
        catch (IllegalArgumentException e)
        {
          Collectd.logError ("GenericJMX plugin: "
              + "Evaluating `Connection' block failed: " + e);
        }
      }
      else
      {
        Collectd.logError ("GenericJMX plugin: Unknown config option: " + key);
      }
    } /* for (i = 0; i < children.size (); i++) */

    return (0);
  } /* }}} int config */

  public int read () /* {{{ */
  {
    for (int i = 0; i < this._connections.size (); i++)
    {
      try
      {
        this._connections.get (i).query ();
      }
      catch (Exception e)
      {
        Collectd.logError ("GenericJMX: Caught unexpected exception: " + e);
        e.printStackTrace ();
      }
    }

    return (0);
  } /* }}} int read */

  public int shutdown () /* {{{ */
  {
    System.out.print ("org.collectd.java.GenericJMX.Shutdown ();\n");
    this._connections = null;
    return (0);
  } /* }}} int shutdown */

  /*
   * static functions
   */
  static public GenericJMXConfMBean getMBean (String alias)
  {
    return (_mbeans.get (alias));
  }

  static private void putMBean (GenericJMXConfMBean mbean)
  {
    Collectd.logDebug ("GenericJMX.putMBean: Adding " + mbean.getName ());
    _mbeans.put (mbean.getName (), mbean);
  }
} /* class GenericJMX */

/* vim: set sw=2 sts=2 et fdm=marker : */
