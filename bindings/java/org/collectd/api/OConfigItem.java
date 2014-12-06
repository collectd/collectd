/**
 * collectd - bindings/java/org/collectd/api/OConfigItem.java
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
 * Java representation of collectd/src/liboconfig/oconfig.h:oconfig_item_t structure.
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
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
