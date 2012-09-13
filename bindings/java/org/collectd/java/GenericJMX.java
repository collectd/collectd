/*
 * collectd/java - org/collectd/java/GenericJMX.java
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
