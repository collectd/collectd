/**
 * collectd - src/common.c
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Michał Mirosław <mirq-linux at rere.qmqm.pl>
**/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_cache.h"

/* for getaddrinfo */
#include <netdb.h>
#include <sys/types.h>

#include <poll.h>

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#if HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

/* for ntohl and htonl */
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#if HAVE_CAPABILITY
#include <sys/capability.h>
#endif

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif

/* AIX doesn't have MSG_DONTWAIT */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT MSG_NONBLOCK
#endif

#if !HAVE_GETPWNAM_R
static pthread_mutex_t getpwnam_r_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#if !HAVE_STRERROR_R
static pthread_mutex_t strerror_r_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

char *sstrncpy(char *dest, const char *src, size_t n) {
  strncpy(dest, src, n);
  dest[n - 1] = '\0';

  return dest;
} /* char *sstrncpy */

char *ssnprintf_alloc(char const *format, ...) /* {{{ */
{
  char static_buffer[1024] = "";
  char *alloc_buffer;
  size_t alloc_buffer_size;
  int status;
  va_list ap;

  /* Try printing into the static buffer. In many cases it will be
   * sufficiently large and we can simply return a strdup() of this
   * buffer. */
  va_start(ap, format);
  status = vsnprintf(static_buffer, sizeof(static_buffer), format, ap);
  va_end(ap);
  if (status < 0)
    return NULL;

  /* "status" does not include the null byte. */
  alloc_buffer_size = (size_t)(status + 1);
  if (alloc_buffer_size <= sizeof(static_buffer))
    return strdup(static_buffer);

  /* Allocate a buffer large enough to hold the string. */
  alloc_buffer = calloc(1, alloc_buffer_size);
  if (alloc_buffer == NULL)
    return NULL;

  /* Print again into this new buffer. */
  va_start(ap, format);
  status = vsnprintf(alloc_buffer, alloc_buffer_size, format, ap);
  va_end(ap);
  if (status < 0) {
    sfree(alloc_buffer);
    return NULL;
  }

  return alloc_buffer;
} /* }}} char *ssnprintf_alloc */

char *sstrdup(const char *s) {
  char *r;
  size_t sz;

  if (s == NULL)
    return NULL;

  /* Do not use `strdup' here, because it's not specified in POSIX. It's
   * ``only'' an XSI extension. */
  sz = strlen(s) + 1;
  r = malloc(sz);
  if (r == NULL) {
    ERROR("sstrdup: Out of memory.");
    exit(3);
  }
  memcpy(r, s, sz);

  return r;
} /* char *sstrdup */

/* Even though Posix requires "strerror_r" to return an "int",
 * some systems (e.g. the GNU libc) return a "char *" _and_
 * ignore the second argument ... -tokkee */
char *sstrerror(int errnum, char *buf, size_t buflen) {
  buf[0] = '\0';

#if !HAVE_STRERROR_R
  {
    char *temp;

    pthread_mutex_lock(&strerror_r_lock);

    temp = strerror(errnum);
    sstrncpy(buf, temp, buflen);

    pthread_mutex_unlock(&strerror_r_lock);
  }
/* #endif !HAVE_STRERROR_R */

#elif STRERROR_R_CHAR_P
  {
    char *temp;
    temp = strerror_r(errnum, buf, buflen);
    if (buf[0] == '\0') {
      if ((temp != NULL) && (temp != buf) && (temp[0] != '\0'))
        sstrncpy(buf, temp, buflen);
      else
        sstrncpy(buf, "strerror_r did not return "
                      "an error message",
                 buflen);
    }
  }
/* #endif STRERROR_R_CHAR_P */

#else
  if (strerror_r(errnum, buf, buflen) != 0) {
    snprintf(buf, buflen, "Error #%i; "
                          "Additionally, strerror_r failed.",
             errnum);
  }
#endif /* STRERROR_R_CHAR_P */

  return buf;
} /* char *sstrerror */

void *smalloc(size_t size) {
  void *r;

  if ((r = malloc(size)) == NULL) {
    ERROR("Not enough memory.");
    exit(3);
  }

  return r;
} /* void *smalloc */

#if 0
void sfree (void **ptr)
{
	if (ptr == NULL)
		return;

	if (*ptr != NULL)
		free (*ptr);

	*ptr = NULL;
}
#endif

int sread(int fd, void *buf, size_t count) {
  char *ptr;
  size_t nleft;
  ssize_t status;

  ptr = (char *)buf;
  nleft = count;

  while (nleft > 0) {
    status = read(fd, (void *)ptr, nleft);

    if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
      continue;

    if (status < 0)
      return status;

    if (status == 0) {
      DEBUG("Received EOF from fd %i. ", fd);
      return -1;
    }

    assert((0 > status) || (nleft >= (size_t)status));

    nleft = nleft - ((size_t)status);
    ptr = ptr + ((size_t)status);
  }

  return 0;
}

int swrite(int fd, const void *buf, size_t count) {
  const char *ptr;
  size_t nleft;
  ssize_t status;
  struct pollfd pfd;

  ptr = (const char *)buf;
  nleft = count;

  if (fd < 0) {
    errno = EINVAL;
    return errno;
  }

  /* checking for closed peer connection */
  pfd.fd = fd;
  pfd.events = POLLIN | POLLHUP;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) > 0) {
    char buffer[32];
    if (recv(fd, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT) == 0) {
      /* if recv returns zero (even though poll() said there is data to be
       * read), that means the connection has been closed */
      errno = ECONNRESET;
      return -1;
    }
  }

  while (nleft > 0) {
    status = write(fd, (const void *)ptr, nleft);

    if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
      continue;

    if (status < 0)
      return errno ? errno : status;

    nleft = nleft - ((size_t)status);
    ptr = ptr + ((size_t)status);
  }

  return 0;
}

int strsplit(char *string, char **fields, size_t size) {
  size_t i;
  char *ptr;
  char *saveptr;

  i = 0;
  ptr = string;
  saveptr = NULL;
  while ((fields[i] = strtok_r(ptr, " \t\r\n", &saveptr)) != NULL) {
    ptr = NULL;
    i++;

    if (i >= size)
      break;
  }

  return (int)i;
}

int strjoin(char *buffer, size_t buffer_size, char **fields, size_t fields_num,
            const char *sep) {
  size_t avail = 0;
  char *ptr = buffer;
  size_t sep_len = 0;

  size_t buffer_req = 0;

  if (((fields_num != 0) && (fields == NULL)) ||
      ((buffer_size != 0) && (buffer == NULL)))
    return -EINVAL;

  if (buffer != NULL)
    buffer[0] = 0;

  if (buffer_size != 0)
    avail = buffer_size - 1;

  if (sep != NULL)
    sep_len = strlen(sep);

  for (size_t i = 0; i < fields_num; i++) {
    size_t field_len = strlen(fields[i]);

    if (i != 0)
      buffer_req += sep_len;
    buffer_req += field_len;

    if ((i != 0) && (sep_len > 0)) {
      if (sep_len >= avail) {
        /* prevent subsequent iterations from writing to the
         * buffer. */
        avail = 0;
        continue;
      }

      memcpy(ptr, sep, sep_len);

      ptr += sep_len;
      avail -= sep_len;
    }

    if (field_len > avail)
      field_len = avail;

    memcpy(ptr, fields[i], field_len);
    ptr += field_len;

    avail -= field_len;
    if (ptr != NULL)
      *ptr = 0;
  }

  return (int)buffer_req;
}

int escape_string(char *buffer, size_t buffer_size) {
  char *temp;
  size_t j;

  /* Check if we need to escape at all first */
  temp = strpbrk(buffer, " \t\"\\");
  if (temp == NULL)
    return 0;

  if (buffer_size < 3)
    return EINVAL;

  temp = calloc(1, buffer_size);
  if (temp == NULL)
    return ENOMEM;

  temp[0] = '"';
  j = 1;

  for (size_t i = 0; i < buffer_size; i++) {
    if (buffer[i] == 0) {
      break;
    } else if ((buffer[i] == '"') || (buffer[i] == '\\')) {
      if (j > (buffer_size - 4))
        break;
      temp[j] = '\\';
      temp[j + 1] = buffer[i];
      j += 2;
    } else {
      if (j > (buffer_size - 3))
        break;
      temp[j] = buffer[i];
      j++;
    }
  }

  assert((j + 1) < buffer_size);
  temp[j] = '"';
  temp[j + 1] = 0;

  sstrncpy(buffer, temp, buffer_size);
  sfree(temp);
  return 0;
} /* int escape_string */

int strunescape(char *buf, size_t buf_len) {
  for (size_t i = 0; (i < buf_len) && (buf[i] != '\0'); ++i) {
    if (buf[i] != '\\')
      continue;

    if (((i + 1) >= buf_len) || (buf[i + 1] == 0)) {
      ERROR("string unescape: backslash found at end of string.");
      /* Ensure null-byte at the end of the buffer. */
      buf[i] = 0;
      return -1;
    }

    switch (buf[i + 1]) {
    case 't':
      buf[i] = '\t';
      break;
    case 'n':
      buf[i] = '\n';
      break;
    case 'r':
      buf[i] = '\r';
      break;
    default:
      buf[i] = buf[i + 1];
      break;
    }

    /* Move everything after the position one position to the left.
     * Add a null-byte as last character in the buffer. */
    memmove(buf + i + 1, buf + i + 2, buf_len - i - 2);
    buf[buf_len - 1] = 0;
  }
  return 0;
} /* int strunescape */

size_t strstripnewline(char *buffer) {
  size_t buffer_len = strlen(buffer);

  while (buffer_len > 0) {
    if ((buffer[buffer_len - 1] != '\n') && (buffer[buffer_len - 1] != '\r'))
      break;
    buffer_len--;
    buffer[buffer_len] = 0;
  }

  return buffer_len;
} /* size_t strstripnewline */

int escape_slashes(char *buffer, size_t buffer_size) {
  size_t buffer_len;

  buffer_len = strlen(buffer);

  if (buffer_len <= 1) {
    if (strcmp("/", buffer) == 0) {
      if (buffer_size < 5)
        return -1;
      sstrncpy(buffer, "root", buffer_size);
    }
    return 0;
  }

  /* Move one to the left */
  if (buffer[0] == '/') {
    memmove(buffer, buffer + 1, buffer_len);
    buffer_len--;
  }

  for (size_t i = 0; i < buffer_len; i++) {
    if (buffer[i] == '/')
      buffer[i] = '_';
  }

  return 0;
} /* int escape_slashes */

void replace_special(char *buffer, size_t buffer_size) {
  for (size_t i = 0; i < buffer_size; i++) {
    if (buffer[i] == 0)
      return;
    if ((!isalnum((int)buffer[i])) && (buffer[i] != '-'))
      buffer[i] = '_';
  }
} /* void replace_special */

int timeval_cmp(struct timeval tv0, struct timeval tv1, struct timeval *delta) {
  struct timeval *larger;
  struct timeval *smaller;

  int status;

  NORMALIZE_TIMEVAL(tv0);
  NORMALIZE_TIMEVAL(tv1);

  if ((tv0.tv_sec == tv1.tv_sec) && (tv0.tv_usec == tv1.tv_usec)) {
    if (delta != NULL) {
      delta->tv_sec = 0;
      delta->tv_usec = 0;
    }
    return 0;
  }

  if ((tv0.tv_sec < tv1.tv_sec) ||
      ((tv0.tv_sec == tv1.tv_sec) && (tv0.tv_usec < tv1.tv_usec))) {
    larger = &tv1;
    smaller = &tv0;
    status = -1;
  } else {
    larger = &tv0;
    smaller = &tv1;
    status = 1;
  }

  if (delta != NULL) {
    delta->tv_sec = larger->tv_sec - smaller->tv_sec;

    if (smaller->tv_usec <= larger->tv_usec)
      delta->tv_usec = larger->tv_usec - smaller->tv_usec;
    else {
      --delta->tv_sec;
      delta->tv_usec = 1000000 + larger->tv_usec - smaller->tv_usec;
    }
  }

  assert((delta == NULL) ||
         ((0 <= delta->tv_usec) && (delta->tv_usec < 1000000)));

  return status;
} /* int timeval_cmp */

int check_create_dir(const char *file_orig) {
  struct stat statbuf;

  char file_copy[512];
  char dir[512];
  int dir_len = 512;
  char *fields[16];
  int fields_num;
  char *ptr;
  char *saveptr;
  int last_is_file = 1;
  int path_is_absolute = 0;
  size_t len;

  /*
   * Sanity checks first
   */
  if (file_orig == NULL)
    return -1;

  if ((len = strlen(file_orig)) < 1)
    return -1;
  else if (len >= sizeof(file_copy))
    return -1;

  /*
   * If `file_orig' ends in a slash the last component is a directory,
   * otherwise it's a file. Act accordingly..
   */
  if (file_orig[len - 1] == '/')
    last_is_file = 0;
  if (file_orig[0] == '/')
    path_is_absolute = 1;

  /*
   * Create a copy for `strtok_r' to destroy
   */
  sstrncpy(file_copy, file_orig, sizeof(file_copy));

  /*
   * Break into components. This will eat up several slashes in a row and
   * remove leading and trailing slashes..
   */
  ptr = file_copy;
  saveptr = NULL;
  fields_num = 0;
  while ((fields[fields_num] = strtok_r(ptr, "/", &saveptr)) != NULL) {
    ptr = NULL;
    fields_num++;

    if (fields_num >= 16)
      break;
  }

  /*
   * For each component, do..
   */
  for (int i = 0; i < (fields_num - last_is_file); i++) {
    /*
     * Do not create directories that start with a dot. This
     * prevents `../../' attacks and other likely malicious
     * behavior.
     */
    if (fields[i][0] == '.') {
      ERROR("Cowardly refusing to create a directory that "
            "begins with a `.' (dot): `%s'",
            file_orig);
      return -2;
    }

    /*
     * Join the components together again
     */
    dir[0] = '/';
    if (strjoin(dir + path_is_absolute, (size_t)(dir_len - path_is_absolute),
                fields, (size_t)(i + 1), "/") < 0) {
      ERROR("strjoin failed: `%s', component #%i", file_orig, i);
      return -1;
    }

    while (42) {
      if ((stat(dir, &statbuf) == -1) && (lstat(dir, &statbuf) == -1)) {
        if (errno == ENOENT) {
          if (mkdir(dir, S_IRWXU | S_IRWXG | S_IRWXO) == 0)
            break;

          /* this might happen, if a different thread created
           * the directory in the meantime
           * => call stat() again to check for S_ISDIR() */
          if (EEXIST == errno)
            continue;

          ERROR("check_create_dir: mkdir (%s): %s", dir, STRERRNO);
          return -1;
        } else {
          ERROR("check_create_dir: stat (%s): %s", dir, STRERRNO);
          return -1;
        }
      } else if (!S_ISDIR(statbuf.st_mode)) {
        ERROR("check_create_dir: `%s' exists but is not "
              "a directory!",
              dir);
        return -1;
      }
      break;
    }
  }

  return 0;
} /* check_create_dir */

#ifdef HAVE_LIBKSTAT
int get_kstat(kstat_t **ksp_ptr, char *module, int instance, char *name) {
  char ident[128];

  *ksp_ptr = NULL;

  if (kc == NULL)
    return -1;

  snprintf(ident, sizeof(ident), "%s,%i,%s", module, instance, name);

  *ksp_ptr = kstat_lookup(kc, module, instance, name);
  if (*ksp_ptr == NULL) {
    ERROR("get_kstat: Cound not find kstat %s", ident);
    return -1;
  }

  if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED) {
    ERROR("get_kstat: kstat %s has wrong type", ident);
    *ksp_ptr = NULL;
    return -1;
  }

#ifdef assert
  assert(*ksp_ptr != NULL);
  assert((*ksp_ptr)->ks_type == KSTAT_TYPE_NAMED);
#endif

  if (kstat_read(kc, *ksp_ptr, NULL) == -1) {
    ERROR("get_kstat: kstat %s could not be read", ident);
    return -1;
  }

  if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED) {
    ERROR("get_kstat: kstat %s has wrong type", ident);
    return -1;
  }

  return 0;
}

long long get_kstat_value(kstat_t *ksp, char *name) {
  kstat_named_t *kn;
  long long retval = -1LL;

  if (ksp == NULL) {
    ERROR("get_kstat_value (\"%s\"): ksp is NULL.", name);
    return -1LL;
  } else if (ksp->ks_type != KSTAT_TYPE_NAMED) {
    ERROR("get_kstat_value (\"%s\"): ksp->ks_type (%#x) "
          "is not KSTAT_TYPE_NAMED (%#x).",
          name, (unsigned int)ksp->ks_type, (unsigned int)KSTAT_TYPE_NAMED);
    return -1LL;
  }

  if ((kn = (kstat_named_t *)kstat_data_lookup(ksp, name)) == NULL)
    return -1LL;

  if (kn->data_type == KSTAT_DATA_INT32)
    retval = (long long)kn->value.i32;
  else if (kn->data_type == KSTAT_DATA_UINT32)
    retval = (long long)kn->value.ui32;
  else if (kn->data_type == KSTAT_DATA_INT64)
    retval =
        (long long)kn->value.i64; /* According to ANSI C99 `long long' must hold
                                     at least 64 bits */
  else if (kn->data_type == KSTAT_DATA_UINT64)
    retval = (long long)kn->value.ui64; /* XXX: Might overflow! */
  else
    WARNING("get_kstat_value: Not a numeric value: %s", name);

  return retval;
}
#endif /* HAVE_LIBKSTAT */

#ifndef HAVE_HTONLL
unsigned long long ntohll(unsigned long long n) {
#if BYTE_ORDER == BIG_ENDIAN
  return n;
#else
  return (((unsigned long long)ntohl(n)) << 32) + ntohl(n >> 32);
#endif
} /* unsigned long long ntohll */

unsigned long long htonll(unsigned long long n) {
#if BYTE_ORDER == BIG_ENDIAN
  return n;
#else
  return (((unsigned long long)htonl(n)) << 32) + htonl(n >> 32);
#endif
} /* unsigned long long htonll */
#endif /* HAVE_HTONLL */

#if FP_LAYOUT_NEED_NOTHING
/* Well, we need nothing.. */
/* #endif FP_LAYOUT_NEED_NOTHING */

#elif FP_LAYOUT_NEED_ENDIANFLIP || FP_LAYOUT_NEED_INTSWAP
#if FP_LAYOUT_NEED_ENDIANFLIP
#define FP_CONVERT(A)                                                          \
  ((((uint64_t)(A)&0xff00000000000000LL) >> 56) |                              \
   (((uint64_t)(A)&0x00ff000000000000LL) >> 40) |                              \
   (((uint64_t)(A)&0x0000ff0000000000LL) >> 24) |                              \
   (((uint64_t)(A)&0x000000ff00000000LL) >> 8) |                               \
   (((uint64_t)(A)&0x00000000ff000000LL) << 8) |                               \
   (((uint64_t)(A)&0x0000000000ff0000LL) << 24) |                              \
   (((uint64_t)(A)&0x000000000000ff00LL) << 40) |                              \
   (((uint64_t)(A)&0x00000000000000ffLL) << 56))
#else
#define FP_CONVERT(A)                                                          \
  ((((uint64_t)(A)&0xffffffff00000000LL) >> 32) |                              \
   (((uint64_t)(A)&0x00000000ffffffffLL) << 32))
#endif

double ntohd(double d) {
  union {
    uint8_t byte[8];
    uint64_t integer;
    double floating;
  } ret;

  ret.floating = d;

  /* NAN in x86 byte order */
  if ((ret.byte[0] == 0x00) && (ret.byte[1] == 0x00) && (ret.byte[2] == 0x00) &&
      (ret.byte[3] == 0x00) && (ret.byte[4] == 0x00) && (ret.byte[5] == 0x00) &&
      (ret.byte[6] == 0xf8) && (ret.byte[7] == 0x7f)) {
    return NAN;
  } else {
    uint64_t tmp;

    tmp = ret.integer;
    ret.integer = FP_CONVERT(tmp);
    return ret.floating;
  }
} /* double ntohd */

double htond(double d) {
  union {
    uint8_t byte[8];
    uint64_t integer;
    double floating;
  } ret;

  if (isnan(d)) {
    ret.byte[0] = ret.byte[1] = ret.byte[2] = ret.byte[3] = 0x00;
    ret.byte[4] = ret.byte[5] = 0x00;
    ret.byte[6] = 0xf8;
    ret.byte[7] = 0x7f;
    return ret.floating;
  } else {
    uint64_t tmp;

    ret.floating = d;
    tmp = FP_CONVERT(ret.integer);
    ret.integer = tmp;
    return ret.floating;
  }
} /* double htond */
#endif /* FP_LAYOUT_NEED_ENDIANFLIP || FP_LAYOUT_NEED_INTSWAP */

int format_name(char *ret, int ret_len, const char *hostname,
                const char *plugin, const char *plugin_instance,
                const char *type, const char *type_instance) {
  char *buffer;
  size_t buffer_size;

  buffer = ret;
  buffer_size = (size_t)ret_len;

#define APPEND(str)                                                            \
  do {                                                                         \
    size_t l = strlen(str);                                                    \
    if (l >= buffer_size)                                                      \
      return ENOBUFS;                                                          \
    memcpy(buffer, (str), l);                                                  \
    buffer += l;                                                               \
    buffer_size -= l;                                                          \
  } while (0)

  assert(plugin != NULL);
  assert(type != NULL);

  APPEND(hostname);
  APPEND("/");
  APPEND(plugin);
  if ((plugin_instance != NULL) && (plugin_instance[0] != 0)) {
    APPEND("-");
    APPEND(plugin_instance);
  }
  APPEND("/");
  APPEND(type);
  if ((type_instance != NULL) && (type_instance[0] != 0)) {
    APPEND("-");
    APPEND(type_instance);
  }
  assert(buffer_size > 0);
  buffer[0] = 0;

#undef APPEND
  return 0;
} /* int format_name */

int format_values(char *ret, size_t ret_len, /* {{{ */
                  const data_set_t *ds, const value_list_t *vl,
                  _Bool store_rates) {
  size_t offset = 0;
  int status;
  gauge_t *rates = NULL;

  assert(0 == strcmp(ds->type, vl->type));

  memset(ret, 0, ret_len);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(ret + offset, ret_len - offset, __VA_ARGS__);            \
    if (status < 1) {                                                          \
      sfree(rates);                                                            \
      return -1;                                                               \
    } else if (((size_t)status) >= (ret_len - offset)) {                       \
      sfree(rates);                                                            \
      return -1;                                                               \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

  BUFFER_ADD("%.3f", CDTIME_T_TO_DOUBLE(vl->time));

  for (size_t i = 0; i < ds->ds_num; i++) {
    if (ds->ds[i].type == DS_TYPE_GAUGE)
      BUFFER_ADD(":" GAUGE_FORMAT, vl->values[i].gauge);
    else if (store_rates) {
      if (rates == NULL)
        rates = uc_get_rate(ds, vl);
      if (rates == NULL) {
        WARNING("format_values: uc_get_rate failed.");
        return -1;
      }
      BUFFER_ADD(":" GAUGE_FORMAT, rates[i]);
    } else if (ds->ds[i].type == DS_TYPE_COUNTER)
      BUFFER_ADD(":%" PRIu64, (uint64_t)vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      BUFFER_ADD(":%" PRIi64, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      BUFFER_ADD(":%" PRIu64, vl->values[i].absolute);
    else {
      ERROR("format_values: Unknown data source type: %i", ds->ds[i].type);
      sfree(rates);
      return -1;
    }
  } /* for ds->ds_num */

#undef BUFFER_ADD

  sfree(rates);
  return 0;
} /* }}} int format_values */

int parse_identifier(char *str, char **ret_host, char **ret_plugin,
                     char **ret_plugin_instance, char **ret_type,
                     char **ret_type_instance, char *default_host) {
  char *hostname = NULL;
  char *plugin = NULL;
  char *plugin_instance = NULL;
  char *type = NULL;
  char *type_instance = NULL;

  hostname = str;
  if (hostname == NULL)
    return -1;

  plugin = strchr(hostname, '/');
  if (plugin == NULL)
    return -1;
  *plugin = '\0';
  plugin++;

  type = strchr(plugin, '/');
  if (type == NULL) {
    if (default_host == NULL)
      return -1;
    /* else: no host specified; use default */
    type = plugin;
    plugin = hostname;
    hostname = default_host;
  } else {
    *type = '\0';
    type++;
  }

  plugin_instance = strchr(plugin, '-');
  if (plugin_instance != NULL) {
    *plugin_instance = '\0';
    plugin_instance++;
  }

  type_instance = strchr(type, '-');
  if (type_instance != NULL) {
    *type_instance = '\0';
    type_instance++;
  }

  *ret_host = hostname;
  *ret_plugin = plugin;
  *ret_plugin_instance = plugin_instance;
  *ret_type = type;
  *ret_type_instance = type_instance;
  return 0;
} /* int parse_identifier */

int parse_identifier_vl(const char *str, value_list_t *vl) /* {{{ */
{
  char str_copy[6 * DATA_MAX_NAME_LEN];
  char *host = NULL;
  char *plugin = NULL;
  char *plugin_instance = NULL;
  char *type = NULL;
  char *type_instance = NULL;
  int status;

  if ((str == NULL) || (vl == NULL))
    return EINVAL;

  sstrncpy(str_copy, str, sizeof(str_copy));

  status = parse_identifier(str_copy, &host, &plugin, &plugin_instance, &type,
                            &type_instance,
                            /* default_host = */ NULL);
  if (status != 0)
    return status;

  sstrncpy(vl->host, host, sizeof(vl->host));
  sstrncpy(vl->plugin, plugin, sizeof(vl->plugin));
  sstrncpy(vl->plugin_instance,
           (plugin_instance != NULL) ? plugin_instance : "",
           sizeof(vl->plugin_instance));
  sstrncpy(vl->type, type, sizeof(vl->type));
  sstrncpy(vl->type_instance, (type_instance != NULL) ? type_instance : "",
           sizeof(vl->type_instance));

  return 0;
} /* }}} int parse_identifier_vl */

int parse_value(const char *value_orig, value_t *ret_value, int ds_type) {
  char *value;
  char *endptr = NULL;
  size_t value_len;

  if (value_orig == NULL)
    return EINVAL;

  value = strdup(value_orig);
  if (value == NULL)
    return ENOMEM;
  value_len = strlen(value);

  while ((value_len > 0) && isspace((int)value[value_len - 1])) {
    value[value_len - 1] = 0;
    value_len--;
  }

  switch (ds_type) {
  case DS_TYPE_COUNTER:
    ret_value->counter = (counter_t)strtoull(value, &endptr, 0);
    break;

  case DS_TYPE_GAUGE:
    ret_value->gauge = (gauge_t)strtod(value, &endptr);
    break;

  case DS_TYPE_DERIVE:
    ret_value->derive = (derive_t)strtoll(value, &endptr, 0);
    break;

  case DS_TYPE_ABSOLUTE:
    ret_value->absolute = (absolute_t)strtoull(value, &endptr, 0);
    break;

  default:
    sfree(value);
    ERROR("parse_value: Invalid data source type: %i.", ds_type);
    return -1;
  }

  if (value == endptr) {
    ERROR("parse_value: Failed to parse string as %s: \"%s\".",
          DS_TYPE_TO_STRING(ds_type), value);
    sfree(value);
    return -1;
  } else if ((NULL != endptr) && ('\0' != *endptr))
    INFO("parse_value: Ignoring trailing garbage \"%s\" after %s value. "
         "Input string was \"%s\".",
         endptr, DS_TYPE_TO_STRING(ds_type), value_orig);

  sfree(value);
  return 0;
} /* int parse_value */

int parse_values(char *buffer, value_list_t *vl, const data_set_t *ds) {
  size_t i;
  char *dummy;
  char *ptr;
  char *saveptr;

  if ((buffer == NULL) || (vl == NULL) || (ds == NULL))
    return EINVAL;

  i = 0;
  dummy = buffer;
  saveptr = NULL;
  vl->time = 0;
  while ((ptr = strtok_r(dummy, ":", &saveptr)) != NULL) {
    dummy = NULL;

    if (i >= vl->values_len) {
      /* Make sure i is invalid. */
      i = 0;
      break;
    }

    if (vl->time == 0) {
      if (strcmp("N", ptr) == 0)
        vl->time = cdtime();
      else {
        char *endptr = NULL;
        double tmp;

        errno = 0;
        tmp = strtod(ptr, &endptr);
        if ((errno != 0)        /* Overflow */
            || (endptr == ptr)  /* Invalid string */
            || (endptr == NULL) /* This should not happen */
            || (*endptr != 0))  /* Trailing chars */
          return -1;

        vl->time = DOUBLE_TO_CDTIME_T(tmp);
      }

      continue;
    }

    if ((strcmp("U", ptr) == 0) && (ds->ds[i].type == DS_TYPE_GAUGE))
      vl->values[i].gauge = NAN;
    else if (0 != parse_value(ptr, &vl->values[i], ds->ds[i].type))
      return -1;

    i++;
  } /* while (strtok_r) */

  if ((ptr != NULL) || (i == 0))
    return -1;
  return 0;
} /* int parse_values */

int parse_value_file(char const *path, value_t *ret_value, int ds_type) {
  FILE *fh;
  char buffer[256];

  fh = fopen(path, "r");
  if (fh == NULL)
    return -1;

  if (fgets(buffer, sizeof(buffer), fh) == NULL) {
    fclose(fh);
    return -1;
  }

  fclose(fh);

  strstripnewline(buffer);

  return parse_value(buffer, ret_value, ds_type);
} /* int parse_value_file */

#if !HAVE_GETPWNAM_R
int getpwnam_r(const char *name, struct passwd *pwbuf, char *buf, size_t buflen,
               struct passwd **pwbufp) {
  int status = 0;
  struct passwd *pw;

  memset(pwbuf, '\0', sizeof(struct passwd));

  pthread_mutex_lock(&getpwnam_r_lock);

  do {
    pw = getpwnam(name);
    if (pw == NULL) {
      status = (errno != 0) ? errno : ENOENT;
      break;
    }

#define GETPWNAM_COPY_MEMBER(member)                                           \
  if (pw->member != NULL) {                                                    \
    int len = strlen(pw->member);                                              \
    if (len >= buflen) {                                                       \
      status = ENOMEM;                                                         \
      break;                                                                   \
    }                                                                          \
    sstrncpy(buf, pw->member, buflen);                                         \
    pwbuf->member = buf;                                                       \
    buf += (len + 1);                                                          \
    buflen -= (len + 1);                                                       \
  }
    GETPWNAM_COPY_MEMBER(pw_name);
    GETPWNAM_COPY_MEMBER(pw_passwd);
    GETPWNAM_COPY_MEMBER(pw_gecos);
    GETPWNAM_COPY_MEMBER(pw_dir);
    GETPWNAM_COPY_MEMBER(pw_shell);

    pwbuf->pw_uid = pw->pw_uid;
    pwbuf->pw_gid = pw->pw_gid;

    if (pwbufp != NULL)
      *pwbufp = pwbuf;
  } while (0);

  pthread_mutex_unlock(&getpwnam_r_lock);

  return status;
} /* int getpwnam_r */
#endif /* !HAVE_GETPWNAM_R */

int notification_init(notification_t *n, int severity, const char *message,
                      const char *host, const char *plugin,
                      const char *plugin_instance, const char *type,
                      const char *type_instance) {
  memset(n, '\0', sizeof(notification_t));

  n->severity = severity;

  if (message != NULL)
    sstrncpy(n->message, message, sizeof(n->message));
  if (host != NULL)
    sstrncpy(n->host, host, sizeof(n->host));
  if (plugin != NULL)
    sstrncpy(n->plugin, plugin, sizeof(n->plugin));
  if (plugin_instance != NULL)
    sstrncpy(n->plugin_instance, plugin_instance, sizeof(n->plugin_instance));
  if (type != NULL)
    sstrncpy(n->type, type, sizeof(n->type));
  if (type_instance != NULL)
    sstrncpy(n->type_instance, type_instance, sizeof(n->type_instance));

  return 0;
} /* int notification_init */

int walk_directory(const char *dir, dirwalk_callback_f callback,
                   void *user_data, int include_hidden) {
  struct dirent *ent;
  DIR *dh;
  int success;
  int failure;

  success = 0;
  failure = 0;

  if ((dh = opendir(dir)) == NULL) {
    ERROR("walk_directory: Cannot open '%s': %s", dir, STRERRNO);
    return -1;
  }

  while ((ent = readdir(dh)) != NULL) {
    int status;

    if (include_hidden) {
      if ((strcmp(".", ent->d_name) == 0) || (strcmp("..", ent->d_name) == 0))
        continue;
    } else /* if (!include_hidden) */
    {
      if (ent->d_name[0] == '.')
        continue;
    }

    status = (*callback)(dir, ent->d_name, user_data);
    if (status != 0)
      failure++;
    else
      success++;
  }

  closedir(dh);

  if ((success == 0) && (failure > 0))
    return -1;
  return 0;
}

ssize_t read_file_contents(const char *filename, char *buf, size_t bufsize) {
  FILE *fh;
  ssize_t ret;

  fh = fopen(filename, "r");
  if (fh == NULL)
    return -1;

  ret = (ssize_t)fread(buf, 1, bufsize, fh);
  if ((ret == 0) && (ferror(fh) != 0)) {
    ERROR("read_file_contents: Reading file \"%s\" failed.", filename);
    ret = -1;
  }

  fclose(fh);
  return ret;
}

counter_t counter_diff(counter_t old_value, counter_t new_value) {
  counter_t diff;

  if (old_value > new_value) {
    if (old_value <= 4294967295U)
      diff = (4294967295U - old_value) + new_value + 1;
    else
      diff = (18446744073709551615ULL - old_value) + new_value + 1;
  } else {
    diff = new_value - old_value;
  }

  return diff;
} /* counter_t counter_diff */

int rate_to_value(value_t *ret_value, gauge_t rate, /* {{{ */
                  rate_to_value_state_t *state, int ds_type, cdtime_t t) {
  gauge_t delta_gauge;
  cdtime_t delta_t;

  if (ds_type == DS_TYPE_GAUGE) {
    state->last_value.gauge = rate;
    state->last_time = t;

    *ret_value = state->last_value;
    return 0;
  }

  /* Counter and absolute can't handle negative rates. Reset "last time"
   * to zero, so that the next valid rate will re-initialize the
   * structure. */
  if ((rate < 0.0) &&
      ((ds_type == DS_TYPE_COUNTER) || (ds_type == DS_TYPE_ABSOLUTE))) {
    memset(state, 0, sizeof(*state));
    return EINVAL;
  }

  /* Another invalid state: The time is not increasing. */
  if (t <= state->last_time) {
    memset(state, 0, sizeof(*state));
    return EINVAL;
  }

  delta_t = t - state->last_time;
  delta_gauge = (rate * CDTIME_T_TO_DOUBLE(delta_t)) + state->residual;

  /* Previous value is invalid. */
  if (state->last_time == 0) /* {{{ */
  {
    if (ds_type == DS_TYPE_DERIVE) {
      state->last_value.derive = (derive_t)rate;
      state->residual = rate - ((gauge_t)state->last_value.derive);
    } else if (ds_type == DS_TYPE_COUNTER) {
      state->last_value.counter = (counter_t)rate;
      state->residual = rate - ((gauge_t)state->last_value.counter);
    } else if (ds_type == DS_TYPE_ABSOLUTE) {
      state->last_value.absolute = (absolute_t)rate;
      state->residual = rate - ((gauge_t)state->last_value.absolute);
    } else {
      assert(23 == 42);
    }

    state->last_time = t;
    return EAGAIN;
  } /* }}} */

  if (ds_type == DS_TYPE_DERIVE) {
    derive_t delta_derive = (derive_t)delta_gauge;

    state->last_value.derive += delta_derive;
    state->residual = delta_gauge - ((gauge_t)delta_derive);
  } else if (ds_type == DS_TYPE_COUNTER) {
    counter_t delta_counter = (counter_t)delta_gauge;

    state->last_value.counter += delta_counter;
    state->residual = delta_gauge - ((gauge_t)delta_counter);
  } else if (ds_type == DS_TYPE_ABSOLUTE) {
    absolute_t delta_absolute = (absolute_t)delta_gauge;

    state->last_value.absolute = delta_absolute;
    state->residual = delta_gauge - ((gauge_t)delta_absolute);
  } else {
    assert(23 == 42);
  }

  state->last_time = t;
  *ret_value = state->last_value;
  return 0;
} /* }}} value_t rate_to_value */

int value_to_rate(gauge_t *ret_rate, /* {{{ */
                  value_t value, int ds_type, cdtime_t t,
                  value_to_rate_state_t *state) {
  gauge_t interval;

  /* Another invalid state: The time is not increasing. */
  if (t <= state->last_time) {
    memset(state, 0, sizeof(*state));
    return EINVAL;
  }

  interval = CDTIME_T_TO_DOUBLE(t - state->last_time);

  /* Previous value is invalid. */
  if (state->last_time == 0) {
    state->last_value = value;
    state->last_time = t;
    return EAGAIN;
  }

  switch (ds_type) {
  case DS_TYPE_DERIVE: {
    derive_t diff = value.derive - state->last_value.derive;
    *ret_rate = ((gauge_t)diff) / ((gauge_t)interval);
    break;
  }
  case DS_TYPE_GAUGE: {
    *ret_rate = value.gauge;
    break;
  }
  case DS_TYPE_COUNTER: {
    counter_t diff = counter_diff(state->last_value.counter, value.counter);
    *ret_rate = ((gauge_t)diff) / ((gauge_t)interval);
    break;
  }
  case DS_TYPE_ABSOLUTE: {
    absolute_t diff = value.absolute;
    *ret_rate = ((gauge_t)diff) / ((gauge_t)interval);
    break;
  }
  default:
    return EINVAL;
  }

  state->last_value = value;
  state->last_time = t;
  return 0;
} /* }}} value_t rate_to_value */

int service_name_to_port_number(const char *service_name) {
  struct addrinfo *ai_list;
  int status;
  int service_number;

  if (service_name == NULL)
    return -1;

  struct addrinfo ai_hints = {.ai_family = AF_UNSPEC};

  status = getaddrinfo(/* node = */ NULL, service_name, &ai_hints, &ai_list);
  if (status != 0) {
    ERROR("service_name_to_port_number: getaddrinfo failed: %s",
          gai_strerror(status));
    return -1;
  }

  service_number = -1;
  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    if (ai_ptr->ai_family == AF_INET) {
      struct sockaddr_in *sa;

      sa = (void *)ai_ptr->ai_addr;
      service_number = (int)ntohs(sa->sin_port);
    } else if (ai_ptr->ai_family == AF_INET6) {
      struct sockaddr_in6 *sa;

      sa = (void *)ai_ptr->ai_addr;
      service_number = (int)ntohs(sa->sin6_port);
    }

    if ((service_number > 0) && (service_number <= 65535))
      break;
  }

  freeaddrinfo(ai_list);

  if ((service_number > 0) && (service_number <= 65535))
    return service_number;
  return -1;
} /* int service_name_to_port_number */

void set_sock_opts(int sockfd) /* {{{ */
{
  int status;
  int socktype;

  status = getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &socktype,
                      &(socklen_t){sizeof(socktype)});
  if (status != 0) {
    WARNING("set_sock_opts: failed to determine socket type");
    return;
  }

  if (socktype == SOCK_STREAM) {
    status =
        setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &(int){1}, sizeof(int));
    if (status != 0)
      WARNING("set_sock_opts: failed to set socket keepalive flag");

#ifdef TCP_KEEPIDLE
    int tcp_keepidle = ((CDTIME_T_TO_MS(plugin_get_interval()) - 1) / 100 + 1);
    status = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &tcp_keepidle,
                        sizeof(tcp_keepidle));
    if (status != 0)
      WARNING("set_sock_opts: failed to set socket tcp keepalive time");
#endif

#ifdef TCP_KEEPINTVL
    int tcp_keepintvl =
        ((CDTIME_T_TO_MS(plugin_get_interval()) - 1) / 1000 + 1);
    status = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &tcp_keepintvl,
                        sizeof(tcp_keepintvl));
    if (status != 0)
      WARNING("set_sock_opts: failed to set socket tcp keepalive interval");
#endif
  }
} /* }}} void set_sock_opts */

int strtoderive(const char *string, derive_t *ret_value) /* {{{ */
{
  derive_t tmp;
  char *endptr;

  if ((string == NULL) || (ret_value == NULL))
    return EINVAL;

  errno = 0;
  endptr = NULL;
  tmp = (derive_t)strtoll(string, &endptr, /* base = */ 0);
  if ((endptr == string) || (errno != 0))
    return -1;

  *ret_value = tmp;
  return 0;
} /* }}} int strtoderive */

int strtogauge(const char *string, gauge_t *ret_value) /* {{{ */
{
  gauge_t tmp;
  char *endptr = NULL;

  if ((string == NULL) || (ret_value == NULL))
    return EINVAL;

  errno = 0;
  endptr = NULL;
  tmp = (gauge_t)strtod(string, &endptr);
  if (errno != 0)
    return errno;
  else if ((endptr == NULL) || (*endptr != 0))
    return EINVAL;

  *ret_value = tmp;
  return 0;
} /* }}} int strtogauge */

int strarray_add(char ***ret_array, size_t *ret_array_len,
                 char const *str) /* {{{ */
{
  char **array;
  size_t array_len = *ret_array_len;

  if (str == NULL)
    return EINVAL;

  array = realloc(*ret_array, (array_len + 1) * sizeof(*array));
  if (array == NULL)
    return ENOMEM;
  *ret_array = array;

  array[array_len] = strdup(str);
  if (array[array_len] == NULL)
    return ENOMEM;

  array_len++;
  *ret_array_len = array_len;
  return 0;
} /* }}} int strarray_add */

void strarray_free(char **array, size_t array_len) /* {{{ */
{
  for (size_t i = 0; i < array_len; i++)
    sfree(array[i]);
  sfree(array);
} /* }}} void strarray_free */

#if HAVE_CAPABILITY
int check_capability(int arg) /* {{{ */
{
  cap_value_t cap_value = (cap_value_t)arg;
  cap_t cap;
  cap_flag_value_t cap_flag_value;

  if (!CAP_IS_SUPPORTED(cap_value))
    return -1;

  if (!(cap = cap_get_proc())) {
    ERROR("check_capability: cap_get_proc failed.");
    return -1;
  }

  if (cap_get_flag(cap, cap_value, CAP_EFFECTIVE, &cap_flag_value) < 0) {
    ERROR("check_capability: cap_get_flag failed.");
    cap_free(cap);
    return -1;
  }
  cap_free(cap);

  return cap_flag_value != CAP_SET;
} /* }}} int check_capability */
#else
int check_capability(__attribute__((unused)) int arg) /* {{{ */
{
  WARNING("check_capability: unsupported capability implementation. "
          "Some plugin(s) may require elevated privileges to work properly.");
  return 0;
} /* }}} int check_capability */
#endif /* HAVE_CAPABILITY */
