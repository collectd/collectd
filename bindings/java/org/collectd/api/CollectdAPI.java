/*
 * collectd/java - org/collectd/api/CollectdAPI.java
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
 * Java API to internal functions of collectd.
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 */
public class CollectdAPI
{
  /**
   * Java representation of collectd/src/plugin.h:plugin_dispatch_values
   */
  native public static int DispatchValues (ValueList vl);

  /**
   * Java representation of collectd/src/plugin.h:plugin_get_ds
   */
  native public static DataSet GetDS (String type);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_read
   */
  native public static int RegisterRead (String name,
      CollectdReadInterface obj);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_write
   */
  native public static int RegisterWrite (String name,
      CollectdWriteInterface obj);
} /* class CollectdAPI */

/* vim: set sw=2 sts=2 et fdm=marker : */
