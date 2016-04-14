#include <stdbool.h>
#include <sys/event.h>
#include <sys/time.h>
#include <poll.h>


struct iomux_s {
    struct iomux_common common;
    int queue;
};


struct entry {
    struct entry_common common;
};


static int
iomux_init (iomux_t *impl)
{
    impl->queue = kqueue ();
    if (impl->queue < 0) {
        ERROR ("iomux_init: kqueue: %s", strerror (errno));
        return -1;
    }
    return 0;
}


static void
iomux_clear (iomux_t *impl)
{
    close (impl->queue);
}


static void
add_ev (struct kevent *ev, int *n, struct entry *entry, int changed, int event, int filter)
{
    if (changed & event) {
        EV_SET (ev + *n,
                entry->common.fd,
                filter,
                (entry->common.events & event) ? EV_ADD : EV_DELETE,
                0, 0, 0);
		(*n)++;
	}
}


static int
iomux_setfd_impl (iomux_t *impl, struct entry *entry, int prev_events)
{
    struct kevent ev[2];
    int n = 0;
    const int changed = entry->common.events ^ prev_events;

    add_ev (ev, &n, entry, changed, POLLIN, EVFILT_READ);
    add_ev (ev, &n, entry, changed, POLLOUT, EVFILT_WRITE);

    struct timespec ts = {0, 0};
    if (n != 0 && kevent (impl->queue, ev, n, NULL, 0, &ts) < 0)
        ERROR ("iomux: kevent: %s", strerror (errno));

    return 0;
}


int
iomux_run (iomux_t *impl, int timeout)
{
    struct timespec ts = { timeout / 1000, timeout % 1000 };
    struct kevent events[16];

    int count = kevent (impl->queue, NULL, 0, events, 16, (timeout >= 0) ? &ts : NULL);
    if (count < 0) {
        ERROR ("iomux_run: kevent: %s", strerror (errno));
        return 0;
    }

    int i;
    for (i = 0; i < count; i++) {
        struct kevent *const ev = events + i;
        int flag;

        if (ev->filter == EVFILT_READ)
            flag = POLLIN;
        else if (ev->filter == EVFILT_WRITE)
            flag = POLLOUT;
        else
            continue;

        if ((ev->flags & EV_EOF) && (ev->fflags != 0))
            flag |= POLLERR;

        iomux_event (impl, ev->ident, flag);
    }

    return count;
}


static void
iomux_restore (iomux_t *impl, struct entry *entry)
{
}
