/**
 * collectd - src/write_top.c
 * Copyright (C) 2013       Yves Mettier
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Some code portion are grabed from src/notify_file.c from Cyril Feraudet
 *
 * Authors:
 *   Yves Mettier <ymettier at free dot fr>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_cache.h"
#include "utils_parse_option.h"
#include "utils_avltree.h"
#include <zlib.h>
#include <pthread.h>

static const char *config_keys[] =
{
	"DataDir",
	"FlushWhenBiggerThanK",
	"FlushWhenOlderThanMin"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);
static char *datadir   = NULL;
static cdtime_t flushwhenolderthan = TIME_T_TO_CDTIME_T(3600);
static size_t flushwhenbiggerthan = 500000;

static pthread_t       wt_write_thread;
static pthread_t       wt_check_flush_thread;
static pthread_mutex_t wt_chunk_flush_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t wt_chunk_free_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t wt_chunk_tree_lock = PTHREAD_MUTEX_INITIALIZER;
static int             wt_write_thread_running = 0;
static int             wt_check_flush_thread_running = 0;

static int do_shutdown = 0;

typedef struct wt_chunk_s {
        char *hostname;
        time_t first_tm;
        time_t last_tm;
        cdtime_t last_notification_tm;
        short flush;
        struct wt_chunk_s *prev; /* when used in a list */
        struct wt_chunk_s *next; /* when used in a list */
        char *cur;
        size_t len;
        char *data;
} wt_chunk_t;


static wt_chunk_t *wt_chunks_free = NULL;
static wt_chunk_t *wt_chunks_flush = NULL;
static wt_chunk_t *wt_chunks_flush_last = NULL;
static c_avl_tree_t *wt_chunks_tree = NULL;
static int wt_chunk_flush_nb = 0;
static int wt_chunk_free_nb = 0;

static int submit_data (value_t v, const char *type, const  char *type_instance)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = &v;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "write_top", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
	
	return (0);
} /* int submit_data */

static int submit_gauge (unsigned int n, const char *type, const  char *type_instance)
{
	value_t value;
	value.gauge = n;

	submit_data(value, type, type_instance);
	
	return (0);
} /* int submit_derive */

static wt_chunk_t *wt_chunk_new(char *hostname) { /* {{{ */
        wt_chunk_t *ch;
        if(wt_chunks_free) {
                /* Use some already allocated chunk */
                pthread_mutex_lock(&wt_chunk_free_lock);
                ch = wt_chunks_free;
                wt_chunks_free = wt_chunks_free->next;
                wt_chunk_free_nb -= 1;
                pthread_mutex_unlock(&wt_chunk_free_lock);
        } else {
                /* No more free chunks */
                if(NULL == (ch = malloc(sizeof(*ch)))) {
                        ERROR ("write_top plugin: Not enough memory");
                        return(NULL);
                }
                if(NULL == (ch->data = malloc(flushwhenbiggerthan*sizeof(char)))) {
                        ERROR ("write_top plugin: Not enough memory");
                        free(ch);
                        return(NULL);
                }
        }
        ch->hostname = hostname;
        ch->first_tm = time(NULL);;
        ch->last_tm = 0;
        ch->last_notification_tm = 0;
        ch->flush = 0;
        ch->prev = NULL;
        ch->next = NULL;
        ch->cur = ch->data;
        ch->len = 0;
        return(ch);
} /* }}} wt_chunk_new */

static void wt_chunk_mark_as_free(wt_chunk_t *ch) { /* {{{ */
        pthread_mutex_lock(&wt_chunk_free_lock);
        ch->hostname = NULL; /* This is useless, but be sure that we do nothing with the old (still used) value */
        ch->next = wt_chunks_free;
        ch->first_tm = time(NULL);
        wt_chunks_free = ch;
        wt_chunk_free_nb += 1;
        pthread_mutex_unlock(&wt_chunk_free_lock);
} /* }}} wt_chunk_mark_as_free */

static void wt_chunk_purge_useless_free_chunks(void) { /* {{{ */
        wt_chunk_t *ch;
        wt_chunk_t *ch_prev;
        time_t tm;

        pthread_mutex_lock(&wt_chunk_free_lock);
        if(NULL == wt_chunks_free) {
                pthread_mutex_unlock(&wt_chunk_free_lock);
                return;
        }
        ch_prev = wt_chunks_free;
        ch = ch_prev->next;
        tm = time(NULL);
        while(ch) {
                /* purge free and unused chunks */
                if(ch->first_tm - flushwhenolderthan < tm) {
                        ch_prev->next = ch->next;
                        free(ch->data);
                        free(ch);
                        wt_chunk_free_nb -= 1;
                        ch = ch_prev;
                }
                ch = ch->next;
        }

        pthread_mutex_unlock(&wt_chunk_free_lock);
} /* }}} wt_chunk_purge_useless_free_chunks */

static int wt_set_filename (char *buffer, int buffer_len, const wt_chunk_t * ch) /* {{{ */
{
        int offset = 0;
        int status;
        char timebuffer[25]; /* 2^64 is a 20-digits decimal number. So 25 should be enough */
        time_t tm;
        struct tm stm;
        int n=0;
        struct stat statbuf;

        if (datadir != NULL)
        {
                status = ssnprintf (buffer, buffer_len, "%s/", datadir);
                if ((status < 1) || (status >= buffer_len ))
                        return (-1);
                offset += status;
        }

        status = ssnprintf (buffer + offset, buffer_len - offset,
                        "%s/", ch->hostname);
        if ((status < 1) || (status >= buffer_len - offset))
                return (-1);
        offset += status;

        /* TODO: Find a way to minimize the calls to `localtime_r',
         * since they are pretty expensive.. */
        tm = ch->first_tm;
        if (localtime_r (&tm, &stm) == NULL)
        {
                ERROR ("write_top plugin: localtime_r failed");
                return (1);
        }
        strftime(timebuffer, sizeof(timebuffer), "%s", &stm);
        status = ssnprintf (buffer + offset, buffer_len - offset,
                        "%1$.2s/%1$.4s/ps-%1$.6s0000-", timebuffer);
        if ((status < 1) || (status >= buffer_len - offset))
                return (-1);
        offset += status;

        do { /* find a filename ps-TM-n.gz that does not exist. Increments n until found. */
                status = ssnprintf (buffer + offset, buffer_len - offset, "%d.gz", n);
                if ((status < 1) || (status >= buffer_len - offset))
                        return (-1);
                n++;
        } while(0 == stat(buffer, &statbuf));

        if(ENOENT != errno) {
                ERROR ("write_top plugin: stat('%s') and errno is not ENOENT (%s)", buffer, strerror(errno));
                return (-1);
        }

        return (0);
} /* }}} int wt_set_filename */

static int wt_chunk_write_to_disk(wt_chunk_t *ch) { /* {{{ */
        int status=0;
        char filename[512];
        char timebuffer[64];
        char write_top_version[] = "Version 1.0\n";
        size_t timebuffer_len;
        gzFile *gfd;
        time_t tm;
        struct tm stm;

        tm = ch->last_tm;
	if (NULL == localtime_r (&tm, &stm)) {
		ERROR ("write_top plugin: localtime_r failed");
		return (-1);
	}
	strftime(timebuffer, sizeof(timebuffer), "%s %Y/%m/%d %H:%M:%S", &stm);
        timebuffer_len = strlen(timebuffer);
        timebuffer[timebuffer_len++] = '\n'; /* Append a \n */
        timebuffer[timebuffer_len] = '\0';

        status = wt_set_filename(filename, sizeof(filename), ch);
        if(0 != status) return(-1);

	if (check_create_dir (filename))
		return (-1);

	gfd = gzopen (filename, "w+");
	if (NULL == gfd)
	{
		char errbuf[1024];
		ERROR ("notify_file plugin: gzopen (%s) failed: %s", filename,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
	gzwrite(gfd, write_top_version, strlen(write_top_version));
	gzwrite(gfd, timebuffer, timebuffer_len);
	gzwrite(gfd, ch->data, ch->len);
	gzclose (gfd);

        return(0);
} /* }}} wt_chunk_write_to_disk */

static wt_chunk_t * wt_chunk_find_host_or_new(const notification_t *n) { /* {{{ */
        wt_chunk_t *ch = NULL;

        if(strcmp("top", n->plugin)) return(NULL);

        if(0 != c_avl_get(wt_chunks_tree, n->host, (void *) &ch)) {
                /* not found : we need to create a new one */
                char *hostname;
                if(NULL == (hostname = strdup(n->host))) {
                        ERROR ("write_top plugin: Not enough memory");
                        return(NULL);
                }
                if(NULL == (ch = wt_chunk_new(hostname))) {
                        return(NULL);
                }
                if(0 != (c_avl_insert(wt_chunks_tree, ch->hostname, ch))) {
                        ERROR("write_top plugin: failed to insert a chunk. This may be a bug. (%s-%s %s:%d)", PACKAGE, VERSION, __FILE__, __LINE__);
                        free(hostname);
                        free(ch);
                        return(NULL);
                }
        }

        return(ch);
} /* }}} wt_chunk_find_host_or_new */

static void wt_chunk_mark_for_flush(wt_chunk_t *ch) { /* {{{ */

        pthread_mutex_lock(&wt_chunk_flush_lock);
        ch->flush = 1;
        ch->next = wt_chunks_flush;
        ch->prev = NULL;
        wt_chunks_flush = ch;
        if(NULL == ch->next) {
                wt_chunks_flush_last = ch;
        } else {
                ch->next->prev = ch;
        }
        wt_chunk_flush_nb += 1;
        pthread_mutex_unlock(&wt_chunk_flush_lock);

} /* }}} wt_chunk_mark_for_flush */

static wt_chunk_t * wt_chunk_mark_for_flush_and_get_new(wt_chunk_t *ch) { /* {{{ */
        wt_chunk_t *ch_new;
        wt_chunk_t *ch_old;
        int status;

        wt_chunk_mark_for_flush(ch);

        ch_new = wt_chunk_new(ch->hostname);
        status = c_avl_update(wt_chunks_tree, ch->hostname, ch_new, (void *) &ch_old);

        assert(0 == status);

        return(ch_new);
} /* }}} wt_chunk_mark_for_flush_and_get_new */

static int wt_chunk_append_notification(wt_chunk_t *ch, const notification_t *n) { /* {{{ */
        time_t tm, now;
	struct tm stm;
        int nb_lines;
        size_t message_len;
        size_t len;
        int i;
        char timebuffer[64];
        size_t timebuffer_len;
        char nb_lines_buffer[20];
        size_t nb_lines_buffer_len;
        short append_eol;

        assert(NULL != ch);

        if(n->time == ch->last_notification_tm) return(0); /* duplicate */

/* Compute the data section to put into the chunk */
        now = tm = time(NULL);
	if (NULL == localtime_r (&now, &stm)) {
		ERROR ("write_top plugin: localtime_r failed");
		return (1);
	}
	strftime(timebuffer, sizeof(timebuffer), "%s %Y/%m/%d %H:%M:%S", &stm);
        timebuffer_len = strlen(timebuffer);

        message_len = strlen(n->message);
        nb_lines = 0;
        for(i=0; i<message_len; i++) {
                if(n->message[i] == '\n') nb_lines++;
        }
        if(n->message[i] != '\n') { 
                nb_lines++;
                append_eol = 1;
        } else {
                append_eol = 0;
        }
        snprintf(nb_lines_buffer, sizeof(nb_lines_buffer), "\n%d\n", nb_lines);
        nb_lines_buffer_len = strlen(nb_lines_buffer);

 /* Check if the chunk has enough space left */
        len = timebuffer_len + nb_lines_buffer_len + message_len + append_eol;
        if(ch->len + len > flushwhenbiggerthan) {
                ch = wt_chunk_mark_for_flush_and_get_new(ch);
        }

        if(0 == ch->len) ch->first_tm = tm;

        /* Note : this does not expect that tm are chronologically sorted.
         * However, some other software (or collectd plugin) may expect it.
         * In case of problems, we may expect it here too. Let's see...
         */
        if(tm > ch->last_tm) ch->last_tm = tm;

        /* keep the last notification time to avoid duplicates */
        /* Note : this system will only prevent from having 2 duplicates
         * coming at the same time for the same host.
         * For example, receiving A B B will result in keeping A B.
         * But receving A B A will result in keeping all : A B A.
         * However, this should not happen so often.
         */
        ch->last_notification_tm = n->time;

        ch->len += len;
        memcpy(ch->cur, timebuffer, timebuffer_len);
        ch->cur += timebuffer_len;
        memcpy(ch->cur, nb_lines_buffer, nb_lines_buffer_len);
        ch->cur += nb_lines_buffer_len;
        memcpy(ch->cur, n->message, message_len);
        ch->cur += message_len;
        if(append_eol) {
                ch->cur[0] = '\n';
                ch->cur += 1;
        }

        return(0);
} /* }}} wt_chunk_append_notification */

static int wt_config (const char *key, const char *value) /* {{{ */
{
	if (strcasecmp ("DataDir", key) == 0) {
		if (datadir != NULL)
			free (datadir);
		datadir = strdup (value);
		if (datadir != NULL)
		{
			int len = strlen (datadir);
			while ((len > 0) && (datadir[len - 1] == '/'))
			{
				len--;
				datadir[len] = '\0';
			}
			if (len <= 0)
			{
				free (datadir);
				datadir = NULL;
			}
		}
	} else if (strcasecmp ("FlushWhenBiggerThanK", key) == 0) {
		size_t s;
		errno=0;
		s = strtol(value, NULL, 10);
		if(0 != errno) {
				ERROR ("write_top plugin: FlushWhenBiggerThanK should be a number. Using default value %ld", flushwhenbiggerthan/1000);
		} else if(s <= 0) {
				ERROR ("write_top plugin: FlushWhenBiggerThanK should be strictly positive. Using default value %ld", flushwhenbiggerthan/1000);
		} else {
				flushwhenbiggerthan = 1000 * s;
		}
	} else if (strcasecmp ("FlushWhenOlderThanMin", key) == 0) {
		time_t m;
		errno=0;
		m = strtol(value, NULL, 10);
		if(0 != errno) {
				ERROR ("write_top plugin: FlushWhenOlderThanMin should be a number (in minutes). Using default value %ld", flushwhenolderthan/60);
		} else if(m <= 0) {
				ERROR ("write_top plugin: FlushWhenOlderThanMin should be a number (in minutes). Using default value %ld", flushwhenolderthan/60);
		} else {
				flushwhenbiggerthan = TIME_T_TO_CDTIME_T(60 * m);
		}
	} else {
		return (-1);
	}
	return (0);
} /* }}} int wt_config */

static int wt_notify (const notification_t * n, /* {{{ */
		user_data_t __attribute__((unused)) *user_data)
{
        wt_chunk_t *ch;

        if(do_shutdown) return(0);

        pthread_mutex_lock(&wt_chunk_tree_lock);
        if(NULL == (ch = wt_chunk_find_host_or_new(n))) {
                pthread_mutex_unlock(&wt_chunk_tree_lock);
                return(0);
        }

        if(0 != wt_chunk_append_notification(ch, n)) {
                pthread_mutex_unlock(&wt_chunk_tree_lock);
                return(-1);
        }
        pthread_mutex_unlock(&wt_chunk_tree_lock);

        return(0);
} /* }}} wt_notify */

static void wt_flush_and_free_chunks_tree(void) /* {{{ */
{
                c_avl_iterator_t *iter;
                char *key;
                wt_chunk_t *ch;

                iter = c_avl_get_iterator (wt_chunks_tree);
                while (c_avl_iterator_next (iter, (void *) &key, (void *) &ch) == 0)
                {
                        wt_chunk_mark_for_flush(ch);
                } /* while (c_avl_iterator_next) */
                c_avl_iterator_destroy (iter);

                c_avl_destroy(wt_chunks_tree);
                wt_chunks_tree=NULL;
        
} /* }}} wt_flush_and_free_chunks_tree */

static void wt_chunks_mark_all_for_flush (time_t olderthan, char *hostname) /* {{{ */
{
        c_avl_iterator_t *iter;
        char *key;
        wt_chunk_t *ch;
        time_t tm=0;

        if(olderthan > 0) {
                tm = time(NULL);
                tm -= olderthan;
        }

        iter = c_avl_get_iterator (wt_chunks_tree);
        while (c_avl_iterator_next (iter, (void *) &key, (void *) &ch) == 0)
        {
                if(0 != do_shutdown) break;
                if((olderthan > 0) && (ch->first_tm >= tm)) continue;
                if(hostname && strcmp(hostname, ch->hostname)) continue;

                wt_chunk_t *ch_new;
                wt_chunk_mark_for_flush(ch);
                ch_new = wt_chunk_new(ch->hostname);
                c_avl_iterator_update_value(iter, ch_new);
        } /* while (c_avl_iterator_next) */
        c_avl_iterator_destroy (iter);

} /* }}} int wt_chunks_mark_all_for_flush */

static void wt_chunks_release_all_free_chunks(void) /* {{{ */
{
        wt_chunk_t*ch;
        while(wt_chunks_free) {
                pthread_mutex_lock(&wt_chunk_free_lock);
                ch = wt_chunks_free;
                wt_chunks_free = ch->next;
                wt_chunk_free_nb -= 1;
                pthread_mutex_unlock(&wt_chunk_free_lock);

                free(ch->hostname);
                free(ch->data);
                free(ch);
        }

} /* }}} wt_chunks_release_all_free_chunks */

static void wt_clean_tree(void) /* {{{ */
{
        c_avl_iterator_t *iter;
        char *key;
        wt_chunk_t *chunks_to_purge=NULL;
        wt_chunk_t *ch;
        time_t tm=0;

        tm = time(NULL);
        tm -= flushwhenolderthan;

        pthread_mutex_lock(&wt_chunk_tree_lock);
        iter = c_avl_get_iterator (wt_chunks_tree);
        while (c_avl_iterator_next (iter, (void *) &key, (void *) &ch) == 0)
        {
                if(ch->len == 0 && (ch->first_tm < tm)) {
                        ch->next = chunks_to_purge;
                        chunks_to_purge = ch;
                }
        } /* while (c_avl_iterator_next) */
        c_avl_iterator_destroy (iter);

        for(ch = chunks_to_purge; ch; ch = ch->next) {
                void *rkey;
                void *rvalue;
                c_avl_remove(wt_chunks_tree, ch->hostname, &rkey, &rvalue);
                /* Because of locks, we prefer to free the chunk instead of
                 * adding it to the list of free chunks.
                 * This should not happen so often, so it should not have a
                 * significative impact.
                 */
                free(ch->hostname);
                free(ch->data);
                free(ch);
        }

        pthread_mutex_unlock(&wt_chunk_tree_lock);

} /* }}} wt_clean_tree */

static void *wt_thread_check_old_chunks (void __attribute__((unused)) *data) /* {{{ */
{
        time_t tm_last_clean = time(NULL);
        while(1) {
                struct timespec ts,rts;
                time_t tm;

                wt_chunks_mark_all_for_flush(flushwhenolderthan, NULL);

                submit_gauge(wt_chunk_free_nb, "nb_values", "nb_free_chunks");
                submit_gauge(wt_chunk_flush_nb, "nb_values", "nb_tops_to_flush");
                submit_gauge(c_avl_size(wt_chunks_tree), "nb_values", "nb_hosts");

                tm = time(NULL);
                if(tm - flushwhenolderthan > tm_last_clean) {
                        /* remove hosts that have not be updated for a while */
                        wt_clean_tree();
                        /* Remove free chunks that have not be reallocaed for
                         * a while */
                        wt_chunk_purge_useless_free_chunks();

                        tm_last_clean = time(NULL);
                }

                if(0 != do_shutdown) break;
                ts.tv_sec = 1;
                ts.tv_nsec = 0;
                nanosleep(&ts,&rts);
        }


        pthread_exit ((void *) 0);
        return ((void *) 0);
} /* }}} int wt_thread_check_old_chunks */

static void *wt_thread_write_chunks (void __attribute__((unused)) *data) /* {{{ */
{
        while(1) {
                wt_chunk_t *flushing_list;
                int nb = 0;

                while(NULL == wt_chunks_flush_last) {
                        struct timespec ts,rts;
                        ts.tv_sec = 1;
                        ts.tv_nsec = 0;
                        nanosleep(&ts,&rts);
                        if(0 != do_shutdown) break;
                }

                pthread_mutex_lock(&wt_chunk_flush_lock);
                flushing_list = wt_chunks_flush_last;
                wt_chunks_flush_last = NULL;
                wt_chunks_flush = NULL;
                nb = wt_chunk_flush_nb;
                wt_chunk_flush_nb = 0;
                pthread_mutex_unlock(&wt_chunk_flush_lock);

                while(NULL != flushing_list) {
                        wt_chunk_t *ch;
                        ch = flushing_list;
                        flushing_list = flushing_list->prev;

                        if(ch->len >0) wt_chunk_write_to_disk(ch);
                        wt_chunk_mark_as_free(ch);
                        nb -= 1;
                }
                if((0 != do_shutdown) && (NULL == wt_chunks_flush_last)) break;
        }

	pthread_exit ((void *) 0);
	return ((void *) 0);
} /* }}} int wt_thread_write_chunks */

static int wt_init (void) /* {{{ */
{
        int status;
        wt_chunks_tree = c_avl_create((void *) strcmp);

/* Create write thread */
	status = plugin_thread_create (&wt_write_thread, /* attr = */ NULL,
			wt_thread_write_chunks, /* args = */ NULL);
	if (status != 0)
	{
		ERROR ("write_top plugin: Cannot create write-thread.");
		return (-1);
	}
	wt_write_thread_running = 1;

/* Create check_flush_chunks thread */
	status = plugin_thread_create (&wt_check_flush_thread, /* attr = */ NULL,
			wt_thread_check_old_chunks, /* args = */ NULL);
	if (status != 0)
	{
		ERROR ("write_top plugin: Cannot create check_flush-thread.");
		return (-1);
	}
	wt_check_flush_thread_running = 1;

        return(0);
} /* }}} int wt_init */

static int wt_flush (cdtime_t timeout, /* {{{ */
                const char *identifier,
                user_data_t *user_data)
{
        if(NULL == identifier) {
                wt_chunks_mark_all_for_flush(timeout, NULL);
        } else {
                char hostname[DATA_MAX_NAME_LEN];
                char *s;
                memcpy(hostname, identifier, DATA_MAX_NAME_LEN);
                s = strchr(hostname, '/');
                if(s) s[0] = '\0';
                wt_chunks_mark_all_for_flush(timeout, hostname);
        }

        return (0);
} /* }}} int wt_flush */

static int wt_shutdown (void) /* {{{ */
{
	wt_flush_and_free_chunks_tree ();

	do_shutdown = 1;

	if ((wt_write_thread_running != 0)
			&& (wt_chunks_flush != NULL))
	{
		INFO ("write_top plugin: Shutting down the write thread. "
				"This may take a while.");
	}
	else if (wt_write_thread_running != 0)
	{
		INFO ("write_top plugin: Shutting down the write thread.");
	}

	if (wt_check_flush_thread_running != 0)
	{
		INFO ("write_top plugin: Shutting down the check_flush thread.");
	}

	/* Wait for check_flush thread */
	if (wt_check_flush_thread_running != 0)
	{
		pthread_join (wt_check_flush_thread, NULL);
		memset (&wt_check_flush_thread, 0, sizeof (wt_check_flush_thread));
		wt_check_flush_thread_running = 0;
		DEBUG ("write_top plugin: check_flush thread exited.");
	}

	/* Wait for all the values to be written to disk before returning. */
	if (wt_write_thread_running != 0)
	{
		pthread_join (wt_write_thread, NULL);
		memset (&wt_write_thread, 0, sizeof (wt_write_thread));
		wt_write_thread_running = 0;
		DEBUG ("write_top plugin: write thread exited.");
	}

        wt_chunks_release_all_free_chunks();

	return (0);
} /* }}} int rrd_shutdown */

void module_register (void)
{
	plugin_register_config ("write_top", wt_config,
			config_keys, config_keys_num);
	plugin_register_init ("write_top", wt_init);

	plugin_register_notification ("write_top", wt_notify, /* user_data = */ NULL);
	plugin_register_flush ("write_top", wt_flush, /* user_data = */ NULL);
	plugin_register_shutdown ("write_top", wt_shutdown);
} /* void module_register */

/* vim: set fdm=marker sw=8 ts=8 tw=78 et : */
