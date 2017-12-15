/**
 * collectd - src/globals.h
 * Copyright (C) 2017  Google LLC
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
 **/

#ifndef GLOBALS_H
#define GLOBALS_H

#include <inttypes.h>

#ifndef DATA_MAX_NAME_LEN
#define DATA_MAX_NAME_LEN 128
#endif

#ifndef PRIsz
#define PRIsz "zu"
#endif /* PRIsz */

/* Type for time as used by "utils_time.h" */
typedef uint64_t cdtime_t;

/* hostname_set updates hostname_g */
void hostname_set(char const *hostname);

extern char *hostname_g;
extern cdtime_t interval_g;
extern int pidfile_from_cli;
extern int timeout_g;
#endif /* GLOBALS_H */
