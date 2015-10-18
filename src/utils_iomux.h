#ifndef UTILS_IOMUX_H
#define UTILS_IOMUX_H

/*
 * Portable I/O multiplexing interface.
 *
 * This provides a convenient interface to OS specific facilities for
 * efficient I/O multiplexing. Currently the following interfaces are
 * supported:
 * - Linux epoll (see "man epoll_create");
 * - Solaris event ports (see "man port_create"), which is the
 *   successor to Solaris' older /dev/poll interface;
 * - BSD's kqueue
 * - poll(), as a portable fallback.
 *
 * To use the framework, a user must first get a multiplexer instance
 * with iomux_create().
 *
 * iomux_setfd() can then be used to add file handles for the
 * multiplexer to watch. The "events" argument uses the same values
 * as poll(), though only POLLIN and POLLOUT (and POLLIN|POLLOUT) are
 * really supported. Other flags should not be specified.
 *
 * With each file descriptor, the user must supply an event callback
 * and optionally a user_data pointer which gets passed on to the event
 * callback. The user_data pointer can be NULL.
 *
 * iomux_setfd() can be used both to add a new fd, or to update the
 * events, callback and user_data pointer for an fd already added to
 * the multiplexer. To remove an fd from the multiplexer, pass a zero
 * value for "events". (In this case the callback may be NULL.)
 *
 * Use iomux_run() to put the multiplexer to work. It will wait for
 * events for at most the number of milliseconds given in "timeout,"
 * and call the "event_cb" for each event detected, providing the
 * file descriptor, the bitmask of events detected (this can be
 * any combination of POLLIN, POLLOUT and POLLERR, and possibly other
 * POLL* flags returned by the underlying interface). Specify a timeout
 * of -1 if you want the call to block indefinitely. Other negative
 * values should not be specified.
 *
 * iomux_run() will return a positive value if any events have been
 * processed (i.e. at least one event callback was called), zero in
 * case of a timeout, and a negative value on error.
 *
 * The code is thread safe, but you must ensure not to use the same
 * multiplexer instance from multiple threads simultaneously. The
 * code does not do any locking of its own.
 */

typedef void (*iomux_event_cb) (int fd, int events, void *user_data);

typedef struct iomux_s iomux_t;

iomux_t *iomux_create (void);
int iomux_setfd (iomux_t *mux, int fd, int events, iomux_event_cb event_cb, void *user_data);
int iomux_run (iomux_t *mux, int timeout);
void iomux_free (iomux_t *mux);

#endif /* UTILS_IOMUX_H */
