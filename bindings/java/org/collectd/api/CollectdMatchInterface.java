/**
 * collectd - bindings/java/org/collectd/api/CollectdMatchInterface.java
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
 * Interface for objects implementing a match method.
 *
 * These objects are instantiated using objects which implement the
 * CollectdMatchFactoryInterface interface. They are not instantiated by the
 * daemon directly!
 *
 * @author Florian Forster &lt;octo at collectd.org&gt;
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
