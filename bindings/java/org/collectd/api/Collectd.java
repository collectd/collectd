/**
 * collectd - bindings/java/org/collectd/api/Collectd.java
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
 * Java API to internal functions of collectd.
 *
 * All functions in this class are {@code static}. You don't need to create an
 * object of this class (in fact, you can't). Just call these functions
 * directly.
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
 */
public class Collectd
{

  /**
   * Constant for severity (log level) "error".
   *
   * @see CollectdLogInterface
   */
  public static final int LOG_ERR     = 3;

  /**
   * Constant for severity (log level) "warning".
   *
   * @see CollectdLogInterface
   */
  public static final int LOG_WARNING = 4;

  /**
   * Constant for severity (log level) "notice".
   *
   * @see CollectdLogInterface
   */
  public static final int LOG_NOTICE  = 5;

  /**
   * Constant for severity (log level) "info".
   *
   * @see CollectdLogInterface
   */
  public static final int LOG_INFO    = 6;

  /**
   * Constant for severity (log level) "debug".
   *
   * @see CollectdLogInterface
   */
  public static final int LOG_DEBUG   = 7;

  /**
   * Return value of match methods: No match.
   *
   * This is one of two valid return values from match callbacks, indicating
   * that the passed {@link DataSet} and {@link ValueList} did not match.
   *
   * Do not use the numeric value directly, it is subject to change without
   * notice!
   *
   * @see CollectdMatchInterface
   */
  public static final int FC_MATCH_NO_MATCH  = 0;

  /**
   * Return value of match methods: Match.
   *
   * This is one of two valid return values from match callbacks, indicating
   * that the passed {@link DataSet} and {@link ValueList} did match.
   *
   * Do not use the numeric value directly, it is subject to change without
   * notice!
   *
   * @see CollectdMatchInterface
   */
  public static final int FC_MATCH_MATCHES   = 1;

  /**
   * Return value of target methods: Continue.
   *
   * This is one of three valid return values from target callbacks, indicating
   * that processing of the {@link ValueList} should continue.
   *
   * Do not use the numeric value directly, it is subject to change without
   * notice!
   *
   * @see CollectdTargetInterface
   */
  public static final int FC_TARGET_CONTINUE = 0;

  /**
   * Return value of target methods: Stop.
   *
   * This is one of three valid return values from target callbacks, indicating
   * that processing of the {@link ValueList} should stop immediately.
   *
   * Do not use the numeric value directly, it is subject to change without
   * notice!
   *
   * @see CollectdTargetInterface
   */
  public static final int FC_TARGET_STOP     = 1;

  /**
   * Return value of target methods: Return.
   *
   * This is one of three valid return values from target callbacks, indicating
   * that processing of the current chain should be stopped and processing of
   * the {@link ValueList} should continue in the calling chain.
   *
   * Do not use the numeric value directly, it is subject to change without
   * notice!
   *
   * @see CollectdTargetInterface
   */
  public static final int FC_TARGET_RETURN   = 2;

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_config
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdConfigInterface
   */
  native public static int registerConfig (String name,
      CollectdConfigInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_init
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdInitInterface
   */
  native public static int registerInit (String name,
      CollectdInitInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_read
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdReadInterface
   */
  native public static int registerRead (String name,
      CollectdReadInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_write
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdWriteInterface
   */
  native public static int registerWrite (String name,
      CollectdWriteInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_flush
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdFlushInterface
   */
  native public static int registerFlush (String name,
      CollectdFlushInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_shutdown
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdShutdownInterface
   */
  native public static int registerShutdown (String name,
      CollectdShutdownInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_log
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdLogInterface
   */
  native public static int registerLog (String name,
      CollectdLogInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_register_notification
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdNotificationInterface
   */
  native public static int registerNotification (String name,
      CollectdNotificationInterface object);

  /**
   * Java representation of collectd/src/filter_chain.h:fc_register_match
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdMatchFactoryInterface
   */
  native public static int registerMatch (String name,
      CollectdMatchFactoryInterface object);

  /**
   * Java representation of collectd/src/filter_chain.h:fc_register_target
   *
   * @return Zero when successful, non-zero otherwise.
   * @see CollectdTargetFactoryInterface
   */
  native public static int registerTarget (String name,
      CollectdTargetFactoryInterface object);

  /**
   * Java representation of collectd/src/plugin.h:plugin_dispatch_values
   *
   * @return Zero when successful, non-zero otherwise.
   */
  native public static int dispatchValues (ValueList vl);

  /**
   * Java representation of collectd/src/plugin.h:plugin_dispatch_notification
   *
   * @return Zero when successful, non-zero otherwise.
   */
  native public static int dispatchNotification (Notification n);

  /**
   * Java representation of collectd/src/plugin.h:plugin_get_ds
   *
   * @return The appropriate {@link DataSet} object or {@code null} if no such
   * type is registered.
   */
  native public static DataSet getDS (String type);

  /**
   * Java representation of collectd/src/plugin.h:plugin_log
   */
  native private static void log (int severity, String message);

  /**
   * Yield contents of collectd/src/collectd.h:hostname_g
   *
   * @return The hostname as set in the collectd configuration.
   */
  native public static java.lang.String getHostname ();

  /**
   * Prints an error message.
   */
  public static void logError (String message)
  {
    log (LOG_ERR, message);
  } /* void logError */

  /**
   * Prints a warning message.
   */
  public static void logWarning (String message)
  {
    log (LOG_WARNING, message);
  } /* void logWarning */

  /**
   * Prints a notice.
   */
  public static void logNotice (String message)
  {
    log (LOG_NOTICE, message);
  } /* void logNotice */

  /**
   * Prints an info message.
   */
  public static void logInfo (String message)
  {
    log (LOG_INFO, message);
  } /* void logInfo */

  /**
   * Prints a debug message.
   */
  public static void logDebug (String message)
  {
    log (LOG_DEBUG, message);
  } /* void logDebug */
} /* class Collectd */

/* vim: set sw=2 sts=2 et fdm=marker : */
