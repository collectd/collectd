/**
 * collectd - bindings/java/org/collectd/api/CollectdMatchFactoryInterface.java
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
 * Interface for objects implementing a "match factory".
 *
 * Objects implementing this interface are used to create objects implementing
 * the CollectdMatchInterface interface.
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
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
