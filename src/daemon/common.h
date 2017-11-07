/**
 * collectd - src/common.h
 * Copyright (C) 2005-2014  Florian octo Forster
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
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

#ifndef COMMON_H
#define COMMON_H

#include "collectd.h"

#include "plugin.h"

#if HAVE_PWD_H
#include <pwd.h>
#endif

#define sfree(ptr)                                                             \
  do {                                                                         \
    free(ptr);                                                                 \
    (ptr) = NULL;                                                              \
  } while (0)

#define STATIC_ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

#define IS_TRUE(s)                                                             \
  ((strcasecmp("true", (s)) == 0) || (strcasecmp("yes", (s)) == 0) ||          \
   (strcasecmp("on", (s)) == 0))
#define IS_FALSE(s)                                                            \
  ((strcasecmp("false", (s)) == 0) || (strcasecmp("no", (s)) == 0) ||          \
   (strcasecmp("off", (s)) == 0))

struct rate_to_value_state_s {
  value_t last_value;
  cdtime_t last_time;
  gauge_t residual;
};
typedef struct rate_to_value_state_s rate_to_value_state_t;

struct value_to_rate_state_s {
  value_t last_value;
  cdtime_t last_time;
};
typedef struct value_to_rate_state_s value_to_rate_state_t;

char *sstrncpy(char *dest, const char *src, size_t n);

__attribute__((format(printf, 1, 2))) char *ssnprintf_alloc(char const *format,
                                                            ...);

char *sstrdup(const char *s);
void *smalloc(size_t size);
char *sstrerror(int errnum, char *buf, size_t buflen);

#ifndef ERRBUF_SIZE
#define ERRBUF_SIZE 256
#endif

#define STRERROR(e) sstrerror((e), (char[ERRBUF_SIZE]){0}, ERRBUF_SIZE)
#define STRERRNO STRERROR(errno)

/*
 * NAME
 *   sread
 *
 * DESCRIPTION
 *   Reads exactly `n' bytes or fails. Syntax and other behavior is analogous
 *   to `read(2)'.
 *
 * PARAMETERS
 *   `fd'          File descriptor to write to.
 *   `buf'         Buffer that is to be written.
 *   `count'       Number of bytes in the buffer.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if an error occurred. `errno' is set in this
 *   case.
 */
int sread(int fd, void *buf, size_t count);

/*
 * NAME
 *   swrite
 *
 * DESCRIPTION
 *   Writes exactly `n' bytes or fails. Syntax and other behavior is analogous
 *   to `write(2)'.
 *
 * PARAMETERS
 *   `fd'          File descriptor to write to.
 *   `buf'         Buffer that is to be written.
 *   `count'       Number of bytes in the buffer.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if an error occurred. `errno' is set in this
 *   case.
 */
int swrite(int fd, const void *buf, size_t count);

/*
 * NAME
 *   strsplit
 *
 * DESCRIPTION
 *   Splits a string into parts and stores pointers to the parts in `fields'.
 *   The characters split at are: " ", "\t", "\r", and "\n".
 *
 * PARAMETERS
 *   `string'      String to split. This string will be modified. `fields' will
 *                 contain pointers to parts of this string, so free'ing it
 *                 will destroy `fields' as well.
 *   `fields'      Array of strings where pointers to the parts will be stored.
 *   `size'        Number of elements in the array. No more than `size'
 *                 pointers will be stored in `fields'.
 *
 * RETURN VALUE
 *    Returns the number of parts stored in `fields'.
 */
int strsplit(char *string, char **fields, size_t size);

/*
 * NAME
 *   strjoin
 *
 * DESCRIPTION
 *   Joins together several parts of a string using `sep' as a separator. This
 *   is equivalent to the Perl built-in `join'.
 *
 * PARAMETERS
 *   `dst'         Buffer where the result is stored. Can be NULL if you need to
 *                 determine the required buffer size only.
 *   `dst_len'     Length of the destination buffer. No more than this many
 *                 bytes will be written to the memory pointed to by `dst',
 *                 including the trailing null-byte. Must be zero if dst is
 *                 NULL.
 *   `fields'      Array of strings to be joined.
 *   `fields_num'  Number of elements in the `fields' array.
 *   `sep'         String to be inserted between any two elements of `fields'.
 *                 This string is neither prepended nor appended to the result.
 *                 Instead of passing "" (empty string) one can pass NULL.
 *
 * RETURN VALUE
 *   Returns the number of characters in the resulting string, excluding a
 *   tailing null byte. If this value is greater than or equal to "dst_len", the
 *   result in "dst" is truncated (but still null terminated). On error a
 *   negative value is returned.
 */
int strjoin(char *dst, size_t dst_len, char **fields, size_t fields_num,
            const char *sep);

/*
 * NAME
 *   escape_slashes
 *
 * DESCRIPTION
 *   Removes slashes ("/") from "buffer". If buffer contains a single slash,
 *   the result will be "root". Leading slashes are removed. All other slashes
 *   are replaced with underscores ("_").
 *   This function is used by plugin_dispatch_values() to escape all parts of
 *   the identifier.
 *
 * PARAMETERS
 *   `buffer'         String to be escaped.
 *   `buffer_size'    Size of the buffer. No more then this many bytes will be
 *                    written to `buffer', including the trailing null-byte.
 *
 * RETURN VALUE
 *   Returns zero upon success and a value smaller than zero upon failure.
 */
int escape_slashes(char *buffer, size_t buffer_size);

/**
 * NAME
 *   escape_string
 *
 * DESCRIPTION
 *   escape_string quotes and escapes a string to be usable with collectd's
 *   plain text protocol. "simple" strings are left as they are, for example if
 *   buffer is 'simple' before the call, it will remain 'simple'. However, if
 *   buffer contains 'more "complex"' before the call, the returned buffer will
 *   contain '"more \"complex\""'.
 *
 *   If the buffer is too small to contain the escaped string, the string will
 *   be truncated. However, leading and trailing double quotes, as well as an
 *   ending null byte are guaranteed.
 *
 * RETURN VALUE
 *   Returns zero on success, even if the string was truncated. Non-zero on
 *   failure.
 */
int escape_string(char *buffer, size_t buffer_size);

/*
 * NAME
 *   replace_special
 *
 * DESCRIPTION
 *   Replaces any special characters (anything that's not alpha-numeric or a
 *   dash) with an underscore.
 *
 *   E.g. "foo$bar&" would become "foo_bar_".
 *
 * PARAMETERS
 *   `buffer'      String to be handled.
 *   `buffer_size' Length of the string. The function returns after
 *                 encountering a null-byte or reading this many bytes.
 */
void replace_special(char *buffer, size_t buffer_size);

/*
 * NAME
 *   strunescape
 *
 * DESCRIPTION
 *   Replaces any escaped characters in a string with the appropriate special
 *   characters. The following escaped characters are recognized:
 *
 *     \t -> <tab>
 *     \n -> <newline>
 *     \r -> <carriage return>
 *
 *   For all other escacped characters only the backslash will be removed.
 *
 * PARAMETERS
 *   `buf'         String to be unescaped.
 *   `buf_len'     Length of the string, including the terminating null-byte.
 *
 * RETURN VALUE
 *   Returns zero upon success, a value less than zero else.
 */
int strunescape(char *buf, size_t buf_len);

/**
 * Removed trailing newline characters (CR and LF) from buffer, which must be
 * null terminated. Returns the length of the resulting string.
 */
__attribute__((nonnull(1))) size_t strstripnewline(char *buffer);

/*
 * NAME
 *   timeval_cmp
 *
 * DESCRIPTION
 *   Compare the two time values `tv0' and `tv1' and store the absolut value
 *   of the difference in the time value pointed to by `delta' if it does not
 *   equal NULL.
 *
 * RETURN VALUE
 *   Returns an integer less than, equal to, or greater than zero if `tv0' is
 *   less than, equal to, or greater than `tv1' respectively.
 */
int timeval_cmp(struct timeval tv0, struct timeval tv1, struct timeval *delta);

/* make sure tv_usec stores less than a second */
#define NORMALIZE_TIMEVAL(tv)                                                  \
  do {                                                                         \
    (tv).tv_sec += (tv).tv_usec / 1000000;                                     \
    (tv).tv_usec = (tv).tv_usec % 1000000;                                     \
  } while (0)

/* make sure tv_sec stores less than a second */
#define NORMALIZE_TIMESPEC(tv)                                                 \
  do {                                                                         \
    (tv).tv_sec += (tv).tv_nsec / 1000000000;                                  \
    (tv).tv_nsec = (tv).tv_nsec % 1000000000;                                  \
  } while (0)

int check_create_dir(const char *file_orig);

#ifdef HAVE_LIBKSTAT
int get_kstat(kstat_t **ksp_ptr, char *module, int instance, char *name);
long long get_kstat_value(kstat_t *ksp, char *name);
#endif

#ifndef HAVE_HTONLL
unsigned long long ntohll(unsigned long long n);
unsigned long long htonll(unsigned long long n);
#endif

#if FP_LAYOUT_NEED_NOTHING
#define ntohd(d) (d)
#define htond(d) (d)
#elif FP_LAYOUT_NEED_ENDIANFLIP || FP_LAYOUT_NEED_INTSWAP
double ntohd(double d);
double htond(double d);
#else
#error                                                                         \
    "Don't know how to convert between host and network representation of doubles."
#endif

int format_name(char *ret, int ret_len, const char *hostname,
                const char *plugin, const char *plugin_instance,
                const char *type, const char *type_instance);
#define FORMAT_VL(ret, ret_len, vl)                                            \
  format_name(ret, ret_len, (vl)->host, (vl)->plugin, (vl)->plugin_instance,   \
              (vl)->type, (vl)->type_instance)
int format_values(char *ret, size_t ret_len, const data_set_t *ds,
                  const value_list_t *vl, _Bool store_rates);

int parse_identifier(char *str, char **ret_host, char **ret_plugin,
                     char **ret_plugin_instance, char **ret_type,
                     char **ret_type_instance, char *default_host);
int parse_identifier_vl(const char *str, value_list_t *vl);
int parse_value(const char *value, value_t *ret_value, int ds_type);
int parse_values(char *buffer, value_list_t *vl, const data_set_t *ds);

/* parse_value_file reads "path" and parses its content as an integer or
 * floating point, depending on "ds_type". On success, the value is stored in
 * "ret_value" and zero is returned. On failure, a non-zero value is returned.
 */
int parse_value_file(char const *path, value_t *ret_value, int ds_type);

#if !HAVE_GETPWNAM_R
int getpwnam_r(const char *name, struct passwd *pwbuf, char *buf, size_t buflen,
               struct passwd **pwbufp);
#endif

int notification_init(notification_t *n, int severity, const char *message,
                      const char *host, const char *plugin,
                      const char *plugin_instance, const char *type,
                      const char *type_instance);
#define NOTIFICATION_INIT_VL(n, vl)                                            \
  notification_init(n, NOTIF_FAILURE, NULL, (vl)->host, (vl)->plugin,          \
                    (vl)->plugin_instance, (vl)->type, (vl)->type_instance)

typedef int (*dirwalk_callback_f)(const char *dirname, const char *filename,
                                  void *user_data);
int walk_directory(const char *dir, dirwalk_callback_f callback,
                   void *user_data, int hidden);
/* Returns the number of bytes read or negative on error. */
ssize_t read_file_contents(char const *filename, char *buf, size_t bufsize);

counter_t counter_diff(counter_t old_value, counter_t new_value);

/* Convert a rate back to a value_t. When converting to a derive_t, counter_t
 * or absoltue_t, take fractional residuals into account. This is important
 * when scaling counters, for example.
 * Returns zero on success. Returns EAGAIN when called for the first time; in
 * this case the value_t is invalid and the next call should succeed. Other
 * return values indicate an error. */
int rate_to_value(value_t *ret_value, gauge_t rate,
                  rate_to_value_state_t *state, int ds_type, cdtime_t t);

int value_to_rate(gauge_t *ret_rate, value_t value, int ds_type, cdtime_t t,
                  value_to_rate_state_t *state);

/* Converts a service name (a string) to a port number
 * (in the range [1-65535]). Returns less than zero on error. */
int service_name_to_port_number(const char *service_name);

/* Sets various, non-default, socket options */
void set_sock_opts(int sockfd);

/** Parse a string to a derive_t value. Returns zero on success or non-zero on
 * failure. If failure is returned, ret_value is not touched. */
int strtoderive(const char *string, derive_t *ret_value);

/** Parse a string to a gauge_t value. Returns zero on success or non-zero on
 * failure. If failure is returned, ret_value is not touched. */
int strtogauge(const char *string, gauge_t *ret_value);

int strarray_add(char ***ret_array, size_t *ret_array_len, char const *str);
void strarray_free(char **array, size_t array_len);

#ifdef HAVE_SYS_CAPABILITY_H
/** Check if the current process benefits from the capability passed in
 * argument. Returns zero if it does, less than zero if it doesn't or on error.
 * See capabilities(7) for the list of possible capabilities.
 * */
int check_capability(int arg);
#endif /* HAVE_SYS_CAPABILITY_H */

#endif /* COMMON_H */
