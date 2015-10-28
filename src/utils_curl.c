#include "config.h"
#include "plugin.h"
#include "utils_curl.h"
#include "utils_iomux.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <unistd.h>
#include <poll.h>


static inline short curl_to_poll (int action);
static inline int poll_to_curl (int events);


struct curl_reactor_s {
    CURLM *curlm;
    iomux_t *mux;
    bool running;
    pthread_mutex_t mutex;
};


struct user_data {
    curl_reactor_cb_t callback;
    void *data;
};


static const unsigned curl_long_timeout = 10000;


static void event_cb (int fd, int events, void *user_data);


static inline short
curl_to_poll (int action)
{
    switch (action) {
        case CURL_POLL_NONE:    return 0;
        case CURL_POLL_REMOVE:  return 0;
        case CURL_POLL_IN:      return POLLIN;
        case CURL_POLL_OUT:     return POLLOUT;
        case CURL_POLL_INOUT:   return POLLIN | POLLOUT;
        default:
            WARNING ("CURL: unknown action value: %d", action);
            return 0;
    }
}


static inline int
poll_to_curl (int events)
{
    int r = 0;

    if (events & POLLIN)
        r |= CURL_CSELECT_IN;
    if (events & POLLOUT)
        r |= CURL_CSELECT_OUT;
    if (events & (POLLERR | POLLHUP | POLLNVAL))
        r |= CURL_CSELECT_ERR;

    return r;
}


static int
sockfunc_adapter (CURL *handle, curl_socket_t fd, int action, void *user_data, void *socket_data)
{
    curl_reactor_t *const reactor = user_data;
    return iomux_setfd (reactor->mux, fd, curl_to_poll (action), event_cb, reactor);
}


static void
event_cb (int fd, int events, void *user_data)
{
    curl_reactor_t *const reactor = user_data;

    int running = 0;

    pthread_mutex_lock (&reactor->mutex);
    curl_multi_socket_action (reactor->curlm, fd, poll_to_curl (events), &running);

    if (running == 0)
        reactor->running = false;

    /*
     * If there aren't currently any handles left running in the
     * CURLM, we're not necessarily done just yet. There may be
     * callbacks left to run from finished handles, and those
     * may add new work to the reactor.
     */
    /*
     * Check for finished handles, and run the associated callbacks.
     */
    for (;;) {
        int in_queue = 0;
        CURLMsg *msg = curl_multi_info_read (reactor->curlm, &in_queue);
        if (msg == NULL)
            break;
        if (msg->msg != CURLMSG_DONE)
            continue;
        curl_multi_remove_handle (reactor->curlm, msg->easy_handle);

        pthread_mutex_unlock (&reactor->mutex);

        struct user_data *ud = NULL;
        curl_easy_getinfo (msg->easy_handle, CURLINFO_PRIVATE, (char **) &ud);
        curl_easy_setopt (msg->easy_handle, CURLOPT_PRIVATE, ud->data);
        ud->callback (msg->easy_handle, msg->data.result);
        free (ud);

        pthread_mutex_lock (&reactor->mutex);
    }

    pthread_mutex_unlock (&reactor->mutex);
}


curl_reactor_t *
curl_reactor_create (void)
{
    curl_reactor_t *reactor = malloc (sizeof (*reactor));
    if (reactor == NULL) {
        ERROR ("curl_reactor_create: out of memory");
        return NULL;
    }

    reactor->mux = iomux_create ();
    if (reactor->mux == NULL) {
        free (reactor);
        return NULL;
    }

    reactor->running = false;
    int rc = pthread_mutex_init (&reactor->mutex, NULL);
    if (rc != 0) {
        ERROR ("curl_reactor_init: pthread_mutex_initialize: %s", strerror (rc));
        iomux_free (reactor->mux);
        free (reactor);
        return NULL;
    }

    reactor->curlm = curl_multi_init ();
    if (reactor->curlm == NULL) {
        ERROR ("curl_reactor_init: Failed to get CURLM handle");
        iomux_free (reactor->mux);
        free (reactor);
        return NULL;
    }

    curl_multi_setopt (reactor->curlm, CURLMOPT_SOCKETFUNCTION, sockfunc_adapter);
    curl_multi_setopt (reactor->curlm, CURLMOPT_SOCKETDATA, reactor);

    return reactor;
}


CURLM *
curl_reactor_curlm (const curl_reactor_t *reactor)
{
    return reactor->curlm;
}


int
curl_reactor_add (curl_reactor_t *reactor, CURL *handle, curl_reactor_cb_t callback, void *user_data, bool *must_run)
{
    struct user_data *ud = malloc (sizeof (*ud));
    if (ud == NULL) {
        ERROR ("curl_reactor_add: out of memory");
        return -1;
    }

    ud->callback = callback;
    ud->data = user_data;

    curl_easy_setopt (handle, CURLOPT_PRIVATE, ud);

    pthread_mutex_lock (&reactor->mutex);

    CURLMcode cmc = curl_multi_add_handle (reactor->curlm, handle);
    if (cmc != CURLM_OK) {
        pthread_mutex_unlock (&reactor->mutex);
        ERROR ("curl_multi_add_handle: %s", curl_multi_strerror (cmc));
        curl_easy_setopt (handle, CURLOPT_PRIVATE, NULL);
        free (ud);
        return -1;
    }

    if (!reactor->running) {
        reactor->running = true;
        if (must_run != NULL)
            *must_run = true;

        /*
         * When adding a handle to an idle CURLM, it may need to be
         * "kickstarted" by signalling a timeout so it will set up
         * its initial sockets.
         */
        int running = 0;
        curl_multi_socket_action (reactor->curlm, CURL_SOCKET_TIMEOUT, 0, &running);
    }

    pthread_mutex_unlock (&reactor->mutex);
    return 0;
}


int
curl_reactor_run (curl_reactor_t *reactor)
{
    pthread_mutex_lock (&reactor->mutex);
    /*
     * "Kickstart" the CURLM once again for safety, and to make sure
     * we get a value for "running".
     */
    int running = 0;
    curl_multi_socket_action (reactor->curlm, CURL_SOCKET_TIMEOUT, 0, &running);

    reactor->running = running != 0;

    while (reactor->running) {
        long timeout = 0;
        curl_multi_timeout (reactor->curlm, &timeout);
        if (timeout < 0 || timeout > curl_long_timeout)
            timeout = curl_long_timeout;

        pthread_mutex_unlock (&reactor->mutex);
        int rc = iomux_run (reactor->mux, timeout);
        pthread_mutex_lock (&reactor->mutex);

        if (rc <= 0) {
            curl_multi_socket_action (reactor->curlm, CURL_SOCKET_TIMEOUT, 0, &running);
            reactor->running = running != 0;
        }
    }

    pthread_mutex_unlock (&reactor->mutex);
    return 0;
}
