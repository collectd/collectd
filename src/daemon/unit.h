/**
 * collectd - src/daemon/unit.h
 * Copyright (C) 2023       Florian "octo" Forster
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
 **/

#ifndef DAEMON_UNIT_H
#define DAEMON_UNIT_H 1

#include "daemon/metric.h"

/* default_unit tries to guess a metric family's unit.
 *
 * If fam->unit is not NULL, that pointer is returned. Otherwise, the function
 * tries to heuristically determine a unit for the metric family, based on known
 * OpenTelemetry metric names:
 * https://opentelemetry.io/docs/specs/semconv/general/metrics/#instrument-naming
 * If successful, a new string is allocated on the heap and returned. This string must
 * be freed using free(). If unsuccessful, NULL is returned.
 *
 * This is designed to be used like this:
 *   fam->unit = default_unit(fam);
 */
char *default_unit(metric_family_t const *fam);

#endif
