/**
 * collectd - bindings/java/org/collectd/api/CollectdReadInterface.java
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
 * Interface for objects implementing a read method.
 *
 * Objects implementing this interface can be registered with the daemon. Their
 * read method is then called periodically to acquire and submit values.
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
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
