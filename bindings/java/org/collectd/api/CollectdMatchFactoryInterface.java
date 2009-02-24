/*
 * collectd/java - org/collectd/api/CollectdMatchFactoryInterface.java
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
 * Interface for objects implementing a "match factory".
 *
 * Objects implementing this interface are used to create objects implementing
 * the CollectdMatchInterface interface.
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 * @see CollectdMatchInterface
 * @see Collectd#registerMatch
 */
public interface CollectdMatchFactoryInterface
{
	/**
	 * Create a new "match" object.
	 *
	 * This method uses the configuration provided as argument to create a
	 * new object which must implement the {@link CollectdMatchInterface}
	 * interface.
	 *
	 * This function corresponds to the <code>create</code> member of the
	 * <code>src/filter_chain.h:match_proc_t</code> struct.
	 *
	 * @return New {@link CollectdMatchInterface} object.
	 */
	public CollectdMatchInterface createMatch (OConfigItem ci);
}
