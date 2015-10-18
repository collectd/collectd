#include <poll.h>


static const unsigned pollfds_initial_alloc = 64;

struct iomux_s {
    struct iomux_common common;
    struct pollfd *pollfds;
    unsigned fd_count, fd_alloc;
};


struct entry {
    struct entry_common common;
    int index;
};


static int
iomux_init (iomux_t *impl)
{
    impl->pollfds = malloc (pollfds_initial_alloc * sizeof (*impl->pollfds));
    if (impl->pollfds == NULL) {
        ERROR ("iomux_init: out of memory");
        return -1;
    }

    impl->fd_count = 0;
    impl->fd_alloc = pollfds_initial_alloc;
    return 0;
}


static void
iomux_clear (iomux_t *impl)
{
    if (impl->pollfds != NULL)
        free (impl->pollfds);
}


static inline void
assign_index (iomux_t *impl, int index)
{
    struct entry *entry = NULL;
    int *const fd = &impl->pollfds[index].fd;
    if (hashtable_lookup (&impl->common.fdtable, *fd, hash_cmp, fd, (void **) &entry) == 0)
        entry->index = index;
    else
        WARNING ("iomux: pollfd %d not found in table", *fd);
}


static int
iomux_setfd_impl (iomux_t *impl, struct entry *entry, int prev_events)
{
    if (prev_events == 0) {
        /* add new fd */
        if (impl->fd_count >= impl->fd_alloc) {
            /*
             * The array cannot hold any more fds, so grow it.
             */
            const unsigned new_alloc = impl->fd_alloc * 2;

            struct pollfd *new_pollfds = realloc (impl->pollfds, new_alloc * sizeof (*new_pollfds));
            if (new_pollfds == NULL) {
                ERROR ("iomux_setfd: out of memory");
                return -1;
            }
            impl->pollfds = new_pollfds;
            impl->fd_alloc = new_alloc;
        }

        const int i = impl->fd_count++;
        impl->pollfds[i].fd = entry->common.fd;
        impl->pollfds[i].events = entry->common.events;
        entry->index = i;
    } else if (entry->common.events == 0) {
        /* remove */
        const int i = entry->index;

        /*
         * Move the last fd in the array into the place of the one we're
         * removing, unless the one we're removing is the last one.
         */
        impl->fd_count--;
        if (i != impl->fd_count) {
            impl->pollfds[i] = impl->pollfds[impl->fd_count];
            /* Update index field in the hash table for the relocated element. */
            assign_index (impl, i);
        }
    } else {
        /* update existing */
        impl->pollfds[entry->index].events = entry->common.events;
    }

    return 0;
}


int
iomux_run (iomux_t *impl, int timeout)
{
    int count = poll (impl->pollfds, impl->fd_count, timeout);

    if (count < 0) {
        if (errno != EINTR)
            ERROR ("iomux: poll: %s", strerror (errno));
        count = 0; /* treat error like a timeout */
    }

    if (count == 0)
        return 0;

    /* count contains number of pollfds where revents != 0 */
    int i;
    for (i = 0; count != 0 && i < impl->fd_count; i++) {
        struct pollfd *pollfd = impl->pollfds + i;
        if (pollfd->revents != 0) {
            iomux_event (impl, pollfd->fd, pollfd->revents);
            count--;
        }
    }

    return 1;
}


static void
iomux_restore (iomux_t *impl, struct entry *entry)
{
    /* This space intentionally left blank */
}
