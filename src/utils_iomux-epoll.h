#include <sys/epoll.h>
#include <poll.h>
#include <unistd.h>


/* How many events to fetch (at most) with one epoll_wait() call */
#define EPOLL_BATCH_SIZE 16


struct iomux_s {
    struct iomux_common common;
    int epoll_fd;
};


struct entry {
    struct entry_common common;
};


static inline short
poll_to_epoll (int events)
{
    int r = 0;

    if (events & POLLIN)
        r |= EPOLLIN;
    if (events & POLLOUT)
        r |= EPOLLOUT;

    return r;
}


static inline int
epoll_to_poll (int events)
{
    int r = 0;

    if (events & EPOLLIN)
        r |= POLLIN;
    if (events & EPOLLOUT)
        r |= POLLOUT;
    if (events & EPOLLERR)
        r |= POLLERR;
    if (events & EPOLLHUP)
        r |= POLLHUP;

    return r;
}


static int
iomux_init (iomux_t *impl)
{
    /*
     * epoll_create() is obsolete, but we use it for better
     * compatibility with old Linux kernels. Nothing * to gain
     * by using the newer epoll_create1(), really.
     */
    impl->epoll_fd = epoll_create (1);
    if (impl->epoll_fd < 0) {
        ERROR ("iomux_init: epoll_create: %s", strerror (errno));
        return -1;
    }
    return 0;
}


static void
iomux_clear (iomux_t *impl)
{
    close (impl->epoll_fd);
}


static int
iomux_setfd_impl (iomux_t *impl, struct entry *aux, int prev_events)
{
    int rc, op;
    const char *op_name;

    struct epoll_event ev = {
        .events = poll_to_epoll (aux->common.events),
        .data   = { .fd = aux->common.fd },
    };

    if (prev_events == 0) {
        op_name = "EPOLL_CTL_ADD";
        op = EPOLL_CTL_ADD;
    } else if (aux->common.events == 0) {
        op_name = "EPOLL_CTL_DEL";
        op = EPOLL_CTL_DEL;
    } else {
        op_name = "EPOLL_CTL_MOD";
        op = EPOLL_CTL_MOD;
    }

    rc = epoll_ctl (impl->epoll_fd, op, aux->common.fd, &ev);
    if (rc == 0)
        return 0;

    /*
     * Silence certain error codes which result from cURL asking us
     * to remove fds which have already been closed or removed.
     * We only report them in debug mode.
     */
    if (op == EPOLL_CTL_DEL && (errno == EBADF || errno == ENOENT || errno == EINVAL)) {
        DEBUG ("iomux_delfd: epoll_ctl: %s: %s", op_name, strerror (errno));
        return 0;
    } else {
        ERROR ("iomux_delfd: epoll_ctl: %s: %s", op_name, strerror (errno));
        return -1;
    }
}


int
iomux_run (iomux_t *impl, int timeout)
{
    struct epoll_event events[EPOLL_BATCH_SIZE];
    int count = epoll_wait (impl->epoll_fd, events, EPOLL_BATCH_SIZE, timeout);
    if (count < 0) {
        ERROR ("iomux_run: epoll_wait: %s", strerror (errno));
        count = 0; /* treat error like a timeout */
    }

    int i;
    for (i = 0; i < count; i++) {
        const struct epoll_event *ev = events + i;
        iomux_event (impl, ev->data.fd, epoll_to_poll (ev->events));
    }

    return count != 0;
}


static void
iomux_restore (iomux_t *impl, struct entry *entry)
{
    /* This space intentionally left blank */
}
