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
 * Java representation of collectd/src/liboconfig/oconfig.h:oconfig_item_t structure.
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 */
public class OConfigItem
{
  private String _key = null;
  private List<OConfigValue> _values   = new ArrayList<OConfigValue> ();
  private List<OConfigItem>  _children = new ArrayList<OConfigItem> ();

  public OConfigItem (String key)
  {
    _key = key;
  } /* OConfigItem (String key) */

  public String getKey ()
  {
    return (_key);
  } /* String getKey () */

  public void addValue (OConfigValue cv)
  {
    _values.add (cv);
  } /* void addValue (OConfigValue cv) */

  public void addValue (String s)
  {
    _values.add (new OConfigValue (s));
  } /* void addValue (String s) */

  public void addValue (Number n)
  {
    _values.add (new OConfigValue (n));
  } /* void addValue (String s) */

  public void addValue (boolean b)
  {
    _values.add (new OConfigValue (b));
  } /* void addValue (String s) */

  public List<OConfigValue> getValues ()
  {
    return (_values);
  } /* List<OConfigValue> getValues () */

  public void addChild (OConfigItem ci)
  {
    _children.add (ci);
  } /* void addChild (OConfigItem ci) */

  public List<OConfigItem> getChildren ()
  {
    return (_children);
  } /* List<OConfigItem> getChildren () */

  public String toString ()
  {
    return (new String ("{ key: " + _key + "; "
          + "values: " + _values.toString () + "; "
          + "children: " + _children.toString () + "; }"));
  } /* String toString () */
} /* class OConfigItem */

/* vim: set sw=2 sts=2 et fdm=marker : */
