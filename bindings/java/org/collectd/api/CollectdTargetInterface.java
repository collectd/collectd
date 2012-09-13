/*
 * collectd/java - org/collectd/api/CollectdTargetInterface.java
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
 * Interface for objects implementing a target method.
 *
 * These objects are instantiated using objects which implement the
 * CollectdTargetFactoryInterface interface. They are not instantiated by the
 * daemon directly!
 *
 * @author Florian Forster &lt;octo at verplant.org&gt;
 * @see CollectdTargetFactoryInterface
 * @see Collectd#registerTarget
 */
public interface CollectdTargetInterface
{
	/**
	 * Callback method for targets.
	 *
	 * This method is called to perform some action on the given ValueList.
	 * What precisely is done depends entirely on the implementing class.
	 *
	 * @return One of: {@link Collectd#FC_TARGET_CONTINUE},
	 * {@link Collectd#FC_TARGET_STOP}, {@link Collectd#FC_TARGET_RETURN}
	 * @see CollectdTargetFactoryInterface
	 */
	public int invoke (DataSet ds, ValueList vl);
} /* public interface CollectdTargetInterface */
