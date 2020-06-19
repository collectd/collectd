/**
 * collectd - src/utils/strbuf/strbuf.h
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

#ifndef UTILS_STRBUF_H
#define UTILS_STRBUF_H 1

typedef struct {
  char *ptr;
  size_t pos;
  size_t size;
  _Bool fixed;
} strbuf_t;

/* STRBUF_CREATE allocates a new strbuf_t on the stack, which must be freed
 * using STRBUF_DESTROY before returning from the function. Failure to call
 * STRBUF_DESTROY will leak the memory allocated to (strbuf_t).ptr. */
#define STRBUF_CREATE                                                          \
  &(strbuf_t) { .ptr = NULL }

/* STRBUF_CREATE_STATIC allocates a new strbuf_t on the stack, using the fixed
 * sized buffer "b". You may optionally call STRBUF_DESTROY with a fixed-sized
 * buffer. */
#define STRBUF_CREATE_STATIC(b)                                                \
  &(strbuf_t) { .ptr = b, .size = sizeof(b), .fixed = 1 }

/* STRBUF_DESTROY frees the memory allocated inside the buffer. The buffer
 * itself is assumed to be allocated on the stack and is not freed. */
#define STRBUF_DESTROY(buf)                                                    \
  do {                                                                         \
    if (!buf->fixed) {                                                         \
      free(buf->ptr);                                                          \
    }                                                                          \
    *buf = (strbuf_t){.ptr = NULL};                                            \
  } while (0)

/* strbuf_create allocates a new strbuf_t on the heap, which must be freed
 * using strbuf_destroy. */
strbuf_t *strbuf_create(void);

/* strbuf_create_static allocates a new strbuf_t on the stack, using the fixed
 * sized buffer "buffer". The returned strbuf_t* must be freed using
 * strbuf_destroy. */
strbuf_t *strbuf_create_static(void *buffer, size_t buffer_size);

/* strbuf_destroy frees a strbuf_t* allocated on the heap. */
void strbuf_destroy(strbuf_t *buf);

/* strbuf_reset empties the buffer. If the buffer is dynamically allocated, it
 * will *not* release (all) the allocated memory. */
void strbuf_reset(strbuf_t *buf);

/* strbuf_print adds "s" to the buffer. If the size of the buffer is static and
 * there is no space available in the buffer, ENOSPC is returned. */
int strbuf_print(strbuf_t *buf, char const *s);

/* strbuf_printf adds a string to the buffer using formatting. If the size of
 * the buffer is static and there is no space available in the buffer, ENOSPC
 * is returned. */
int strbuf_printf(strbuf_t *buf, char const *format, ...);

#endif
