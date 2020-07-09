/**
 * collectd - src/utils_strbuf.h
 * Copyright (C) 2017       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

#include "collectd.h"

#include "utils/strbuf/strbuf.h"

#include <stdarg.h>

static size_t strbuf_avail(strbuf_t *buf) {
  if ((buf == NULL) || (buf->size == 0)) {
    return 0;
  }

  assert(buf->pos < buf->size);
  return buf->size - (buf->pos + 1);
}

static size_t strbuf_pagesize() {
  static size_t cached_pagesize;

  if (!cached_pagesize) {
    long tmp = sysconf(_SC_PAGESIZE);
    if (tmp >= 1)
      cached_pagesize = (size_t)tmp;
    else
      cached_pagesize = 1024;
  }

  return cached_pagesize;
}

/* strbuf_resize resizes an dynamic buffer to ensure that "need" bytes can be
 * stored in it. When called with an empty buffer, i.e. buf->size == 0, it will
 * allocate just enough memory to store need+1 bytes. Subsequent calls will
 * only allocate memory when needed, doubling the allocated memory size each
 * time until the page size is reached, then allocating. */
static int strbuf_resize(strbuf_t *buf, size_t need) {
  if (buf->fixed)
    return 0;

  if (strbuf_avail(buf) > need)
    return 0;

  size_t new_size;
  if (buf->size == 0) {
    /* New buffers: try to use a reasonable default. */
    new_size = 512;
  } else if (buf->size < strbuf_pagesize()) {
    /* Small buffers: double the size. */
    new_size = 2 * buf->size;
  } else {
    /* Large buffers: allocate an additional page. */
    size_t pages = (buf->size + strbuf_pagesize() - 1) / strbuf_pagesize();
    new_size = (pages + 1) * strbuf_pagesize();
  }

  /* Check that the new size is large enough. If not, calculate the exact number
   * of bytes needed. */
  if (new_size < (buf->pos + need + 1))
    new_size = buf->pos + need + 1;

  char *new_ptr = realloc(buf->ptr, new_size);
  if (new_ptr == NULL)
    return ENOMEM;

  buf->ptr = new_ptr;
  buf->size = new_size;

  return 0;
}

strbuf_t *strbuf_create(void) { return calloc(1, sizeof(strbuf_t)); }

strbuf_t *strbuf_create_fixed(void *buffer, size_t buffer_size) {
  strbuf_t *buf = calloc(1, sizeof(*buf));
  if (buf == NULL)
    return NULL;

  *buf = (strbuf_t){
      .ptr = buffer,
      .size = buffer_size,
      .fixed = 1,
  };
  return buf;
}

void strbuf_destroy(strbuf_t *buf) {
  if (buf == NULL) {
    return;
  }

  if (!buf->fixed) {
    free(buf->ptr);
  }
  free(buf);
}

void strbuf_reset(strbuf_t *buf) {
  if (buf == NULL) {
    return;
  }

  buf->pos = 0;
  if (buf->ptr != NULL)
    buf->ptr[buf->pos] = 0;

  if (buf->fixed) {
    return;
  }

  /* Truncate the buffer to the page size. This is deemed a good compromise
   * between freeing memory (after a large buffer has been constructed) and
   * performance (avoid unnecessary allocations). */
  size_t new_size = strbuf_pagesize();
  if (buf->size > new_size) {
    char *new_ptr = realloc(buf->ptr, new_size);
    if (new_ptr != NULL) {
      buf->ptr = new_ptr;
      buf->size = new_size;
    }
  }
}

int strbuf_print(strbuf_t *buf, char const *s) {
  if ((buf == NULL) || (s == NULL))
    return EINVAL;

  size_t s_len = strlen(s);
  int status = strbuf_resize(buf, s_len);
  if (status != 0)
    return status;

  size_t bytes = strbuf_avail(buf);
  if (bytes == 0)
    return ENOSPC;

  if (bytes > s_len)
    bytes = s_len;

  memmove(buf->ptr + buf->pos, s, bytes);
  buf->pos += bytes;
  buf->ptr[buf->pos] = 0;

  return 0;
}

int strbuf_printf(strbuf_t *buf, char const *format, ...) {
  va_list ap;

  va_start(ap, format);
  int status = vsnprintf(NULL, 0, format, ap);
  va_end(ap);
  if (status <= 0)
    return status;

  size_t s_len = (size_t)status;

  status = strbuf_resize(buf, s_len);
  if (status != 0)
    return status;

  size_t bytes = strbuf_avail(buf);
  if (bytes == 0)
    return ENOSPC;

  if (bytes > s_len)
    bytes = s_len;

  va_start(ap, format);
  (void)vsnprintf(buf->ptr + buf->pos, bytes + 1, format, ap);
  va_end(ap);

  buf->pos += bytes;
  buf->ptr[buf->pos] = 0;

  return 0;
}

int strbuf_printn(strbuf_t *buf, char const *s, size_t n) {
  if ((buf == NULL) || (s == NULL))
    return EINVAL;
  if (n == 0) {
    return 0;
  }

  size_t s_len = strnlen(s, n);
  int status = strbuf_resize(buf, s_len);
  if (status != 0)
    return status;

  size_t bytes = strbuf_avail(buf);
  if (bytes == 0)
    return ENOSPC;

  if (bytes > s_len)
    bytes = s_len;

  memmove(buf->ptr + buf->pos, s, bytes);
  buf->pos += bytes;
  buf->ptr[buf->pos] = 0;

  return 0;
}

int strbuf_print_escaped(strbuf_t *buf, char const *s, char const *need_escape,
                         char escape_char) {
  if ((buf == NULL) || (s == NULL) || (need_escape == NULL) ||
      (escape_char == 0)) {
    return EINVAL;
  }

  size_t s_len = strlen(s);
  while (s_len > 0) {
    size_t valid_len = strcspn(s, need_escape);
    if (valid_len == s_len) {
      return strbuf_print(buf, s);
    }
    if (valid_len != 0) {
      int status = strbuf_printn(buf, s, valid_len);
      if (status != 0) {
        return status;
      }

      s += valid_len;
      s_len -= valid_len;
      continue;
    }

    /* Ensure the escape sequence is not truncated. */
    if (buf->fixed && (strbuf_avail(buf) < 2)) {
      return 0;
    }

    char c = s[0];
    if (escape_char == '\\') {
      if (c == '\n') {
        c = 'n';
      } else if (c == '\r') {
        c = 'r';
      } else if (c == '\t') {
        c = 't';
      }
    }

    char tmp[3] = {escape_char, c, 0};
    int status = strbuf_print(buf, tmp);
    if (status != 0) {
      return status;
    }

    s++;
    s_len--;
  }

  return 0;
}
