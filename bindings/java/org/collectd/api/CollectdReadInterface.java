/*
 * collectd/java - org/collectd/api/CollectdReadInterface.java
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
 * Interface for objects implementing a read method.
 *
 * Objects implementing this interface can be registered with the daemon. Their
 * read method is then called periodically to acquire and submit values.
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 * @see Collectd#registerRead
 */
public interface CollectdReadInterface
{
	/**
	 * Callback method for read plugins.
	 *
	 * This method is called once every few seconds (depends on the
	 * configuration of the daemon). It is supposed to gather values in
	 * some way and submit them to the daemon using
	 * {@link Collectd#dispatchValues}.
	 *
	 * @return zero when successful, non-zero when an error occurred.
	 * @see Collectd#dispatchValues
	 */
	public int read ();
}
