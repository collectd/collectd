/*
 * collectd/java - org/collectd/api/CollectdMatchInterface.java
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
 * Interface for objects implementing a match method.
 *
 * These objects are instantiated using objects which implement the
 * CollectdMatchFactoryInterface interface. They are not instantiated by the
 * daemon directly!
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 * @see CollectdMatchFactoryInterface
 * @see Collectd#registerMatch
 */
public interface CollectdMatchInterface
{
	/**
	 * Callback method for matches.
	 *
	 * This method is called to decide whether or not a given ValueList
	 * matches or not. How this is determined is the is the main part of
	 * this function.
	 *
	 * @return One of {@link Collectd#FC_MATCH_NO_MATCH} and {@link Collectd#FC_MATCH_MATCHES}.
	 * @see CollectdMatchFactoryInterface
	 */
	public int match (DataSet ds, ValueList vl);
} /* public interface CollectdMatchInterface */
