#ifndef _CURL_REACTOR_H
#define _CURL_REACTOR_H

#include <stdbool.h>

#include <curl/curl.h>


/*
 * The CURL reactor is a wrapper around a CURLM handle that provides
 * thread safety and efficient I/O multiplexing by means of iomux_t.
 *
 * First use curl_reactor_create() to get a new instance of a CURL
 * reactor.
 *
 * Use curl_reactor_curlm() to get a pointer to the CURLM handle
 * embedded in the reactor. You can configure most CURLMOPT_*
 * parameters such as CURLMOPT_PIPELINING, CURLMOPT_MAXCONNECTS
 * etc. Don't fiddle with any of the following, though:
 * - CURLMOPT_SOCKETFUNCTION
 * - CURLMOPT_SOCKETDATA
 * - CURLMOPT_TIMERFUNCTION
 * - CURLMOPT_TIMERDATA
 * - any functions other than curl_multi_setopt()
 *
 * Add work to the reactor using curl_reactor_add(). You pass in a
 * CURL handle that you create and configure to your liking.
 * Just be aware that you cannot use the CURLOPT_PRIVATE parameter,
 * as the reactor needs it for its own business. However, the user_data
 * pointer passed to curl_reactor_add() can be retrieved from the
 * handle via CURLINFO_PRIVATE once the handle has finished processing.
 * Just be aware that you cannot access it while the transfer is in
 * progress, e.g. from the CURLOPT_WRITEFUNCTION callback. Use
 * CURLOPT_WRITEDATA for that.
 *
 * The callback passed to curl_reactor_add() will be called once the
 * handle has finished processing. At that point, you can use
 * CURLINFO_PRIVATE to retrieve the user_data pointer and query the
 * handle for any information you need. You must call curl_easy_cleanup()
 * on the handle when you're done with it.
 *
 * curl_reactor_add() is thread safe and can be called from any thread
 * at any time, on the same reactor. It will only add the given CURL
 * handle to the work queue and not do any kind of blocking I/O.
 *
 * After one or more handles have been added, some thread must "run the
 * reactor". This is indicated by the value returned in *must_run. If
 * a true value is returned, the calling thread must at some point call
 * curl_reactor_run(). You don't have to do it right away - you can keep
 * adding more handles to the reactor before you run it - but you must
 * do it eventually if you want I/O processing to happen.
 *
 * If the reactor is already being run by another thread, then the value
 * pointed to by must_run is not touched by curl_reactor_add(). So the
 * usual sequence of calls would be:
 *
 * 1. Declare a boolean flag initialized to false.
 * 2. Do one or multiple curl_reactor_add() calls each time passing a
 *    pointer to the flag as must_run.
 * 3. If the flag is true after adding all your handles, call
 *    curl_reactor_run().
 *
 * The code ensures that only one true value is given out to the callers
 * of curl_reactor_add() on an idle reactor, so if you follow the above
 * sequence there will never be multiple threads running the reactor
 * simultaneously.
 *
 * As a special case, you don't have to observe the must_run flag (and
 * can actually pass a NULL pointer) when calling curl_reactor_add()
 * from a callback of a handle within the same reactor. In that case,
 * the already running curl_reactor_run() will ensure your newly added
 * handle finishes processing before it terminates.
 */


typedef struct curl_reactor_s curl_reactor_t;

typedef void (*curl_reactor_cb_t) (CURL *, CURLcode result);


/*
 * Create a new reactor and return a pointer to it.
 * In case of failure, return NULL.
 */
curl_reactor_t *curl_reactor_create (void);

/*
 * Get pointer to embedded CURLM handle for customizing via
 * curl_multi_setopt().
 */
CURLM *curl_reactor_curlm (const curl_reactor_t *reactor);

/*
 * Add CURL handle to be processed by this reactor.
 * See above for usage details.
 * Returns zero on success, a negative value on error.
 */
int curl_reactor_add (curl_reactor_t *reactor, CURL *handle,
        curl_reactor_cb_t callback, void *user_data,
        bool *must_run);

/*
 * Run the reactor until there are no more handles in it that need
 * processing.
 * Returns zero on success, a negative value on error.
 */
int curl_reactor_run (curl_reactor_t *reactor);


#endif /* _CURL_REACTOR_H */
