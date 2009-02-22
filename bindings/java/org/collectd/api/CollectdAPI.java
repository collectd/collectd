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
  private static final int LOG_ERR     = 3;
  private static final int LOG_WARNING = 4;
  private static final int LOG_NOTICE  = 5;
  private static final int LOG_INFO    = 6;
  private static final int LOG_DEBUG   = 7;

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_config
   */
  native public static int registerConfig (String name,
      CollectdConfigInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_init
   */
  native public static int registerInit (String name,
      CollectdInitInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_read
   */
  native public static int registerRead (String name,
      CollectdReadInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_write
   */
  native public static int registerWrite (String name,
      CollectdWriteInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_shutdown
   */
  native public static int registerShutdown (String name,
      CollectdShutdownInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_dispatch_values
   */
  native public static int dispatchValues (ValueList vl);

  /**
   * Java representation of collectd/src/plugin.h:plugin_get_ds
   */
  native public static DataSet getDS (String type);

  /**
   * Java representation of collectd/src/plugin.h:plugin_log
   */
  native private static void log (int severity, String message);

  public static void logError (String message)
  {
    log (LOG_ERR, message);
  } /* void logError */

  public static void logWarning (String message)
  {
    log (LOG_WARNING, message);
  } /* void logWarning */

  public static void logNotice (String message)
  {
    log (LOG_NOTICE, message);
  } /* void logNotice */

  public static void logInfo (String message)
  {
    log (LOG_INFO, message);
  } /* void logInfo */

  public static void logDebug (String message)
  {
    log (LOG_DEBUG, message);
  } /* void logDebug */

} /* class CollectdAPI */

/* vim: set sw=2 sts=2 et fdm=marker : */
