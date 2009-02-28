/*
 * jcollectd
 * Copyright (C) 2009 Hyperic, Inc.
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
 */

package org.collectd.api;

/**
 * Java representation of collectd/src/plugin.h:notfication_t structure.
 */
public class Notification extends PluginData {
    public static final int FAILURE = 1;
    public static final int WARNING = 2;
    public static final int OKAY    = 4;

    public static String[] SEVERITY = {
        "FAILURE",
        "WARNING",
        "OKAY",
        "UNKNOWN"
    };

    private int _severity;
    private String _message;

    public Notification () {
        _severity = 0;
        _message = "Initial notification message";
    }

    public Notification (PluginData pd) {
        super (pd);
        _severity = 0;
        _message = "Initial notification message";
    }

    public void setSeverity (int severity) {
        if ((severity == FAILURE)
                || (severity == WARNING)
                || (severity == OKAY))
            this._severity = severity;
    }

    public int getSeverity() {
        return _severity;
    }

    public String getSeverityString() {
        switch (_severity) {
            case FAILURE:
                return SEVERITY[0];
            case WARNING:
                return SEVERITY[1];
            case OKAY:
                return SEVERITY[2];
            default:
                return SEVERITY[3];
        }
    }

    public void setMessage (String message) {
        this._message = message;
    }

    public String getMessage() {
        return _message;
    }

    public String toString() {
        StringBuffer sb = new StringBuffer(super.toString());
        sb.append(" [").append(getSeverityString()).append("] ");
        sb.append(_message);
        return sb.toString();
    }
}
