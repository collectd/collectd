#include "utils_buffer.h"

#include <stdlib.h>


static void buffer_setup_safe (buffer_t *buf);


static void
buffer_setup_safe (buffer_t *buf)
{
    /*
     * Set up the buffer such that the other functions are safe to
     * call, and the buffer always behaves as if it's full.
     */
    buf->data = NULL;
    buf->len = 1;
    buf->max = 1;
    buf->own_alloc = false;
}


int
buffer_init (buffer_t *buf, char *data, size_t len, size_t max)
{
    int rc = 0;

    if (max < len || (max > len && data != NULL)) {
        rc = BUFFER_INVALID_ARGUMENT;
        goto fail;
    }

    buf->own_alloc = (data == NULL);
    if (buf->own_alloc)
        data = malloc (len);
    if (data == NULL) {
        rc = BUFFER_MALLOC_FAIL;
        goto fail;
    }

    buf->data = data;
    buf->len = len;
    buf->max = max;

    buffer_reset (buf);

    return 0;

fail:
    buffer_setup_safe (buf);
    return rc;
}


void
buffer_clear (buffer_t *buf)
{
    if (buf->own_alloc && buf->data != NULL)
        free (buf->data);
    buffer_setup_safe (buf);
}


int
__buffer_grow (buffer_t *buf, size_t needed)
{
    if (needed > buf->max)
        return BUFFER_NO_SPACE;

    /*
     * The desired new allocation size is twice the old size.
     * However we have "needed" as a lower bound and "buf->max"
     * as an upper bound. We have established above that these
     * bounds do not contradict one another. Now ensure the new
     * allocation size adheres to the bounds.
     */
    size_t newlen = buf->len * 2;
    if (newlen > buf->max)
        newlen = buf->max;
    if (newlen < needed)
        newlen = needed;

    char *newdata = realloc (buf->data, newlen);
    if (newdata == NULL)
        return BUFFER_MALLOC_FAIL;

    buf->data = newdata;
    buf->len = newlen;

    return 0;
}


int
buffer_vprintf (buffer_t *buf, const char *fmt, va_list args)
{
    int input_len;

    for (;;) {
        const unsigned avail = buffer_alloc_left (buf);
        /*
         * snprintf() always puts a trailing NUL, so we can let it have
         * the extra byte we reserve for that.
         */
        input_len = vsnprintf (buf->data + buf->pos, avail + 1, fmt, args);
        if (input_len < 0)
            return BUFFER_INVALID_ARGUMENT;
        if (input_len <= avail)
            break; /* success */
        int rc = buffer_ensure_alloc (buf, input_len);
        if (rc < 0)
            return rc;
    }

    buf->pos += input_len;
    return input_len;
}


int
buffer_cycle (buffer_t *buf, char **oldbuf, size_t *used)
{
    char *newdata = malloc (buf->len);
    if (newdata == NULL)
        return BUFFER_MALLOC_FAIL;

    *oldbuf = buffer_getstr (buf);
    *used = buffer_getpos (buf);

    buf->data = newdata;
    buf->own_alloc = true;
    buffer_reset (buf);

    return 0;
}


int
buffer_putmem (buffer_t *buf, const void *data, size_t len)
{
    int rc = buffer_ensure_alloc (buf, len);
    if (rc < 0)
        return rc;

    memcpy (buf->data + buf->pos, data, len);
    buf->pos += len;
    return len;
}


int
buffer_putstr (buffer_t *buf, const char *data)
{
    /*
     * Using strlen() and then buffer_putmem() means we traverse the
     * string twice, but we think this is more efficient than
     * processing the input string one character at a time, because
     * strlen() and memcpy() are usually highly optimized.
     */
    return buffer_putmem (buf, data, strlen (data));
}


int
buffer_printf (buffer_t *buf, const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    int r = buffer_vprintf (buf, fmt, args);
    va_end (args);
    return r;
}
