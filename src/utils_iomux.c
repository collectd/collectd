#include "plugin.h"
#include "utils_iomux.h"
#include "utils_hashtable.h"

#include <stdlib.h>


struct iomux_common {
    hashtable_t fdtable;
};


struct entry_common {
    int fd;
    int events;
    iomux_event_cb callback;
    void *user_data;
};


static void iomux_event (iomux_t *mux, int fd, int events);
static bool hash_cmp (void *_entry, void *_key);


/* Implementation must provide these. */
struct entry;
static int iomux_setfd_impl (iomux_t *impl, struct entry *entry, int prev_events);
static void iomux_restore (iomux_t *impl, struct entry *entry);


#if HAVE_EPOLL_CREATE
#include "utils_iomux-epoll.h"
#elif HAVE_PORT_CREATE
#include "utils_iomux-solaris.h"
#elif HAVE_KQUEUE
#include "utils_iomux-kqueue.h"
#else
#include "utils_iomux-poll.h"
#endif


iomux_t *
iomux_create (void)
{
    iomux_t *mux = malloc (sizeof (*mux));

    if (mux == NULL)
        return NULL;

    int rc = hashtable_init (&mux->common.fdtable, sizeof (struct entry), 0, 4);
    if (rc != 0) {
        ERROR ("iomux_init: hashtable_init: %s", strerror (rc));
        free (mux);
        return NULL;
    }

    if (iomux_init (mux) < 0) {
        hashtable_destroy (&mux->common.fdtable);
        free (mux);
        return NULL;
    }

    return mux;
}


void
iomux_free (iomux_t *mux)
{
    iomux_clear (mux);
    hashtable_destroy (&mux->common.fdtable);
    free (mux);
}


static bool
hash_cmp (void *_entry, void *_key)
{
    const struct entry *const entry = _entry;
    const int fd = * (int *) _key;
    return entry->common.fd == fd;
}


int
iomux_setfd (iomux_t *mux, int fd, int events, iomux_event_cb event_cb, void *user_data)
{
    struct entry *entry = NULL;
    hashtable_t *const table = &mux->common.fdtable;
    int hashrc = hashtable_lookup (table, fd, hash_cmp, &fd, (void **) &entry);
    int prev_events = 0;

    if (hashrc == 0)
        prev_events = entry->common.events;
    else if (events == 0)
        return 0;

    entry->common.fd = fd;
    entry->common.events = events;
    entry->common.callback = event_cb;
    entry->common.user_data = user_data;

    int rc = iomux_setfd_impl (mux, entry, prev_events);
    if (rc != 0)
        return rc;

    if (hashrc == ENOENT)
        hashtable_insert (table, entry); /* XXX handle errors */
    else if (events == 0)
        hashtable_delete (table, entry);

    return 0;
}


static void
iomux_event (iomux_t *mux, int fd, int events)
{
    struct entry *entry = NULL;
    int hashrc = hashtable_lookup (&mux->common.fdtable, fd, hash_cmp, &fd, (void **) &entry);
    if (hashrc == 0) {
        iomux_restore (mux, entry);
        entry->common.callback (fd, events, entry->common.user_data);
    } else {
        WARNING ("iomux: received event for unknown fd %d", fd);
    }
}
