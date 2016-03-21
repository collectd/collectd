package org.collectd.java;

import org.collectd.api.Collectd;

public class GenericJMXLogger extends Collectd {

  private static boolean loggingEnabled = true;

  public static void logError(String message)
  {
    if (loggingEnabled)
      Collectd.logError(message);
  }

  public static void logWarning(String message)
  {
    if (loggingEnabled)
      Collectd.logWarning(message);
  }

  public static void logNotice(String message)
  {
    if (loggingEnabled)
      Collectd.logNotice(message);
  }

  public static void logInfo(String message)
  {
    if (loggingEnabled)
      Collectd.logInfo(message);
  }

  public static void logDebug(String message)
  {
    if (loggingEnabled)
      Collectd.logDebug(message);
  }

  public static void disableLogging() {
   loggingEnabled = false;
  }
}
