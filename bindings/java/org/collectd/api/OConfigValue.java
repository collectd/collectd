/*
 * collectd/java - org/collectd/api/OConfigValue.java
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

/**
 * Java representation of collectd/src/liboconfig/oconfig.h:oconfig_value_t structure.
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 */
public class OConfigValue
{
  public static final int OCONFIG_TYPE_STRING  = 0;
  public static final int OCONFIG_TYPE_NUMBER  = 1;
  public static final int OCONFIG_TYPE_BOOLEAN = 2;

  private int     _type;
  private String  _value_string;
  private Number  _value_number;
  private boolean _value_boolean;

  public OConfigValue (String s)
  {
    _type = OCONFIG_TYPE_STRING;
    _value_string  = s;
    _value_number  = null;
    _value_boolean = false;
  } /* OConfigValue (String s) */

  public OConfigValue (Number n)
  {
    _type = OCONFIG_TYPE_NUMBER;
    _value_string  = null;
    _value_number  = n;
    _value_boolean = false;
  } /* OConfigValue (String s) */

  public OConfigValue (boolean b)
  {
    _type = OCONFIG_TYPE_BOOLEAN;
    _value_string  = null;
    _value_number  = null;
    _value_boolean = b;
  } /* OConfigValue (String s) */

  public int getType ()
  {
    return (_type);
  } /* int getType */

  public String getString ()
  {
    return (_value_string);
  } /* String getString */

  public Number getNumber ()
  {
    return (_value_number);
  } /* String getString */

  public boolean getBoolean ()
  {
    return (_value_boolean);
  } /* String getString */

  public String toString ()
  {
    if (_type == OCONFIG_TYPE_STRING)
      return (_value_string);
    else if (_type == OCONFIG_TYPE_NUMBER)
      return (_value_number.toString ());
    else if (_type == OCONFIG_TYPE_BOOLEAN)
      return (Boolean.toString (_value_boolean));
    return (null);
  } /* String toString () */
} /* class OConfigValue */

/* vim: set sw=2 sts=2 et fdm=marker : */
