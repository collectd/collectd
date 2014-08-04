/**
 * collectd - bindings/java/org/collectd/api/OConfigValue.java
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

/**
 * Java representation of collectd/src/liboconfig/oconfig.h:oconfig_value_t structure.
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
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
