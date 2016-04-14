#ifndef UTILS_BUFFER_H
#define UTILS_BUFFER_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>


/*
 * A buffer is a structure describing an area in memory where data can
 * be written to in a linear manner.
 *
 * Buffers have the following properties:
 *
 * - The data area (data) is the memory area the buffer is currently
 *   writing data to.
 *
 * - The position (pos) is the offset into the data area where the next
 *   byte added to the buffer will be placed.
 *
 * - The length (len) is the number of bytes currently allocated to the
 *   data area.
 *
 * - The maximum (max) is the maximum number of bytes that the data
 *   area can grow to.
 *
 * - A buffer is said to "own the allocation" if the data area was
 *   allocated via malloc() by one of the buffer_* functions.
 *
 * - A buffer is "dynamic" if max > len, and "static" if max == len.
 *   (max < len can never happen.)
 *   A buffer can only be dynamic if it owns the allocation.
 *
 * The last byte in the data area is always reserved for a trailing NUL
 * byte. For efficiency reasons, the trailing NUL will not always be
 * maintained, but we always ensure there is space in the buffer to put
 * a trailing NUL, and buffer_getstr() (see below) does ensure the
 * trailing NUL is present.
 */


struct buffer_s {
    char *data;
    size_t len, pos, max;
    bool own_alloc;
};
typedef struct buffer_s buffer_t;


/*
 * Static initializer, to be used like
 *
 *   buffer_t buf = BUFFER_STATIC_INIT (data, len)
 *
 * A statically initialized buffer does not own the allocation and is
 * thus static.
 */
#define BUFFER_STATIC_INIT(data, len) {(data), (len), 0, (len), false}


enum {
    BUFFER_NO_SPACE         = -1,
    BUFFER_MALLOC_FAIL      = -2,
    BUFFER_INVALID_ARGUMENT = -3,
};


/*
 * Initialize a buffer.
 *
 * Iff data is NULL, buffer_init() will allocate memory for the data
 * area using malloc(), and the buffer will own the allocation.
 *
 * BUFFER_MALLOC_FAIL will occur if data is NULL and the data area
 * cannot be allocated.
 *
 * BUFFER_INVALID_ARGUMENT will occur if len > max, or if trying to
 * initialize a dynamic buffer with data != NULL.
 *
 * If buffer_init() fails, buf will be left in a safe state: All of the
 * other buffer_* functions can be safely called on buf. They will not
 * be able to write any data, but there won't be any undefined behavior
 * (i.e. crashes).
 *
 * Returns an error code or zero on success.
 */
int buffer_init (buffer_t *buf, char *data, size_t len, size_t max);

/*
 * Free the data area if the buffer owns the allocation.
 *
 * After the buffer has been cleared, it will be in a "safe state" as
 * described as if after a failed call to buffer_init().
 */
void buffer_clear (buffer_t *buf);

/*
 * Same as buffer_setpos (buf, 0) (see below).
 */
static inline void buffer_reset (buffer_t *buf);

/*
 * Return the number of bytes available for writing before the buffer
 * reaches the "max" size, provided there will be no allocation
 * failures. This does not include the space reserved for the trailing
 * NUL.
 */
static inline size_t buffer_space_left (const buffer_t *buf);

/*
 * Return the number of bytes available for writing in the current
 * data area. This does not include the space reserved for the trailing
 * NUL.
 */
static inline size_t buffer_alloc_left (const buffer_t *buf);

/*
 * After a successful call to buffer_ensure_alloc(), the return value
 * of buffer_alloc_left() will be at least "needed".
 *
 * Returns an error code or zero on success.
 */
static inline int buffer_ensure_alloc (buffer_t *buf, size_t needed);

/*
 * Get the buffer's current position.
 */
static inline size_t buffer_getpos (const buffer_t *buf);

/*
 * Set the buffer's current position. This can only be used to "rewind"
 * the buffer. It is an error to specify pos > buffer_getpos (buf),
 * which would result in exposing uninitialized data in the buffer.
 *
 * Returns an error code or zero on success.
 */
static inline int buffer_setpos (buffer_t *buf, size_t pos);

/*
 * Ensure the data area is NUL terminated, and return a pointer to its
 * first byte.
 *
 * Returns a valid pointer, or NULL if the buffer is in a "safe state"
 * after a buffer_init() failure or a buffer_clear() call.
 */
static inline char *buffer_getstr (const buffer_t *buf);

/*
 * Append a single character to the buffer.
 *
 * Returns an error code or the value 1 if the character was written
 * successfully.
 */
static inline int buffer_putc (buffer_t *buf, char c);

/*
 * Append a number of bytes to the buffer.
 *
 * Returns an error code or "len" on success.
 */
int buffer_putmem (buffer_t *buf, const void *data, size_t len);

/*
 * Append a NUL-terminated string to the buffer. Fails if the string
 * cannot completely fit into the buffer.
 *
 * Returns an error code or the length of the input string on success.
 */
int buffer_putstr (buffer_t *buf, const char *data);

/*
 * Append the result of a "printf" call to the buffer.
 *
 * Returns an error code or the number of bytes written on success.
 */
int buffer_vprintf (buffer_t *buf, const char *fmt, va_list args)
    __attribute__ ((format (printf, 2, 0)));
int buffer_printf (buffer_t *buf, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));

/*
 * "Cycle" the buffer. This will allocate a new data area the same size
 * as the current one, reset the position to 0 and return the previous
 * data area and position in "oldbuf" and "used" respectively. The
 * data area returned will be NUL-terminated.
 *
 * If buffer_cycle() fails to allocate a new data area, the buffer will
 * be left unchanged and BUFFER_MALLOC_FAIL will be returned.
 *
 * After a successful call to buffer_cycle(), the buffer will own the
 * allocation regardless of whether it did before.
 */
int buffer_cycle (buffer_t *buf, char **oldbuf, size_t *used);


/*
 * Implementation details below here.
 */


int __buffer_grow (buffer_t *buf, size_t needed);


static inline void
buffer_reset (buffer_t *buf)
{
    buffer_setpos (buf, 0);
}


static inline size_t
buffer_space_left (const buffer_t *buf)
{
    /* last byte is reserved for NUL */
    return buf->max - buf->pos - 1;
}


static inline size_t
buffer_alloc_left (const buffer_t *buf)
{
    /* last byte is reserved for NUL */
    return buf->len - buf->pos - 1;
}


/*
 * The "quick" part of the function is inlined here for optimum
 * performance. Only in the unlikely case that a realloc is needed,
 * divert to an actual call.
 */
static inline int
buffer_ensure_alloc (buffer_t *buf, size_t needed)
{
    /* add 1 byte for trailing NUL */
    needed = buf->pos + needed + 1;
    if (buf->len >= needed)
        return 0;
    return __buffer_grow (buf, needed);
}


static inline size_t
buffer_getpos (const buffer_t *buf)
{
    return buf->pos;
}


static inline int
buffer_setpos (buffer_t *buf, size_t pos)
{
    if (pos > buf->pos)
        return BUFFER_INVALID_ARGUMENT;

    buf->pos = pos;
    return 0;
}


static inline char *
buffer_getstr (const buffer_t *buf)
{
    if (buf->data != NULL)
        buf->data[buf->pos] = 0;
    return buf->data;
}


static inline int
buffer_putc (buffer_t *buf, char c)
{
    int rc = buffer_ensure_alloc (buf, 1);
    if (rc < 0)
        return rc;

    buf->data[buf->pos++] = c;
    return 1;
}


#endif /* UTILS_BUFFER_H */
