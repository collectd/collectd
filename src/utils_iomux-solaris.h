#include <stdbool.h>
#include <port.h>


struct iomux_s {
    struct iomux_common common;
    int port;
};


struct entry {
    struct entry_common common;
};


static int
iomux_init (iomux_t *impl)
{
    impl->port = port_create ();
    if (impl->port < 0) {
        ERROR ("iomux_init: port_create: %s", strerror (errno));
        return -1;
    }
    return 0;
}


static void
iomux_clear (iomux_t *impl)
{
    close (impl->port);
}


static int
iomux_setfd_impl (iomux_t *impl, struct entry *entry, int prev_events)
{
    const char *op_name;
    int rc;

    if (entry->common.events != 0) {
        /* add/update */
        op_name = "port_associate";
        rc = port_associate (impl->port, PORT_SOURCE_FD, entry->common.fd, entry->common.events, NULL);
    } else {
        /* remove */
        op_name = "port_dissociate";
        rc = port_dissociate (impl->port, PORT_SOURCE_FD, entry->common.fd);
    }

    if (rc != 0)
        ERROR ("iomux: %s: %s", op_name, strerror (errno));

    return 0;
}


int
iomux_run (iomux_t *impl, int timeout)
{
    /*
     * This uses port_get() which retrieves a single event from the
     * port. There is a port_getn() function which can retrieve
     * multiple events in one call, but it doesn't fit our purpose
     * because it will wait on and collect more events until its
     * destination array is full. We want to continue processing as
     * soon as an event is available.
     */

    port_event_t event;
    bool have_event = true;
    struct timespec ts = {timeout / 1000, (timeout % 1000) * 1000000};
    event.portev_events = 0;
    if (port_get (impl->port, &event, (timeout >= 0) ? &ts : NULL) != 0) {
        if (errno != ETIME)
            ERROR ("iomux_run: port_get: %s", strerror (errno));
        have_event = false;
    } else if (event.portev_events == 0) {
        /*
         * Work around a weirdness in port_get(). Sometimes it returns
         * without indicating an error but doesn't fill in the event
         * structure. We detect this by checking that portev_events
         * as still 0 as we set it before the call to port_get().
         * When this happens, treat it like a timeout.
         */
        have_event = false;
    }

    if (!have_event)
        return 0;

    const int fd = event.portev_object;
    iomux_event (impl, fd, event.portev_events);

    return 1;
}


static void
iomux_restore (iomux_t *impl, struct entry *entry)
{
    /*
     * Event sources automatically get disassociated from the port
     * the first time they fire, so we must re-associate.
     */
    if (port_associate (impl->port, PORT_SOURCE_FD, entry->common.fd, entry->common.events, NULL) != 0)
        ERROR ("iomux_run: port_associate: %s", strerror (errno));
}
