#ifndef UTILS_HASHTABLE_H
#define UTILS_HASHTABLE_H

#include "config.h"

#include <stdbool.h>
#include <limits.h>


typedef unsigned long hash_t;


/*
 * FNV-1a hash function.
 *
 * Use HASH_INIT as the initial value, then calls to the hash_update*()
 * functions to generate the hash.
 *
 * E.g., to hash a single string "s":
 *
 *     hash_t hash = hash_update_str (HASH_INIT, s);
 *
 * Given strings "s1" and "s2", get a hash of the value "s1:s2"
 * without having to concatenate the strings in a buffer:
 *
 *     hash_t hash = HASH_INIT;
 *     hash = hash_update_str (hash, s1);
 *     hash = hash_update (hash, ':');
 *     hash = hash_update_str (hash, s2);
 *
 * The different hash_update*() functions can be arbitrarity combined.
 */

/*
 * Update hash value with a single byte.
 */
static inline hash_t hash_update (hash_t h, unsigned char input);

/*
 * Equivalent to chaining hash_update() calls for all bytes of "str",
 * not including the trailing NUL.
 */
static inline hash_t hash_update_str (hash_t h, const char *str);

/*
 * Equivalent to chaining hash_update() calls for "len" bytes of memory
 * starting at "data".
 */
static inline hash_t hash_update_mem (hash_t h, const void *data, unsigned len);


/*
 * Hash table.
 *
 * hashtable_init() initializes an empty hash table for storage of user
 * objects with the given size and alignment requirements. alignment must
 * be a power of 2 and can also be given as zero. Small alignment values
 * will be adjusted upward to a sensible default. Currently that default
 * is 16, which seems to be what most malloc() implementations use and
 * should thus be safe for most use cases.
 *
 * The "minsize_exp" specifies the initial and minimum size of the table
 * below which it will never shrink. This is given as a power of 2, e.g.
 * a value of 4 will give a minimum table size of 2^4 = 16.
 *
 * Return value is zero on success, EINVAL if an invalid alignment was
 * specified, ENOMEM if memory allocation fails.
 *
 * hashtable_count() returns the number of elements currently in the
 * hash table.
 *
 * hashtable_lookup() is the basis for all operations on the table.
 * The caller must supply the hash value of the key to be looked up,
 * and a match function to compare a given element's key to the one
 * the caller is looking for. The match function will be given a
 * pointer to an entry in the table as its first argument, and
 * match_arg as the second argument.
 *
 * If the lookup is successful, zero is returned and *data is set to
 * the entry found. The caller can use this pointer to read and update
 * the entry in-place, but must not modify the entry's key.
 *
 * The caller can use hashtable_delete() with the pointer returned to
 * delete the element from the hash table. hashtable_delete() will
 * return zero on success. If the delete triggered a rehash and the
 * rehash could not complete because of a memory allocation failure,
 * hashtable_delete() will return ENOMEM. Even in this case, the entry
 * is still deleted from the table.
 *
 * If hashtable_lookup() fails to find a matching entry, ENOENT is
 * returned and *data points to an area of memory where the caller can
 * construct an entry with the key used for the lookup, for insertion
 * into the table.
 *
 * Once the entry has been initialized, hashtable_insert() can be called
 * to add the new entry into the hash table. hashtable_insert() returns
 * zero in case of success, or ENOMEM if there was a memory allocation
 * failure during a rehash. If hashtable_insert() returns ENOMEM, the
 * new entry is NOT inserted.
 *
 * If you want to insert a new entry and you are sure that its key is
 * not currently in the table, you can pass NULL for the match function
 * (but be sure to give the correct hash value for the key you're
 * inserting).
 *
 * A call to hashtable_insert() or hashtable_delete() invalidates all
 * pointers previously returned by calls to hashtable_lookup().
 *
 * Call hashtable_start_bulk_update() to start a "bulk update". Bulk
 * updates are "recursive": If hashtable_start_bulk_update() has been
 * called n times, then hashtable_end_bulk_update() must be called
 * as many times to get the table out of bulk update mode.
 *
 * During a bulk update, calls to hashtable_delete() do not cause any
 * rehashes and thus do not invalidate pointers returned by previous
 * hashtable_lookup() calls. hashtable_insert() calls are possible
 * during a bulk update, but may still cause rehashes and do invalidate
 * pointers. A rehash may happen during hashtable_end_bulk_update(),
 * in which case ENOMEM may be returned. However, entries deleted
 * during the bulk update will stay deleted.
 *
 * hashtable_traverse() calls a callback function for each entry in the
 * table. The entry is passed as the first argument to the callback,
 * and the user_data pointer as the second argument.
 *
 * The callback cannot do anything that may cause a rehash: It cannot
 * use hashtable_insert(), and it can use hashtable_delete() only if
 * the table is in bulk update mode. It also cannot use
 * hashtable_end_bulk_update().
 *
 * If the callback returns a true value, hashtable_traverse() will
 * abort the traversal and return true immediately. Otherwise, if the
 * traversal is not aborted, hashtable_traverse() returns false.
 */
struct hashtable_s {
    char *arena;
    void *arena_handle;
    unsigned data_size;
    unsigned data_align;
    unsigned minsize_exp;

    unsigned bucket_stride, bucket_doff;

    unsigned size_exp;
    hash_t size, used, tombs;

    unsigned bulk_update;

#ifdef HASHTABLE_STATS
#define HASHTABLE_STATS_MAX 32
    unsigned long stat_iter[HASHTABLE_STATS_MAX + 1];
    unsigned long total_iter, total_ops;
#endif
};

typedef struct hashtable_s hashtable_t;


int hashtable_init (hashtable_t *table, unsigned data_size, unsigned alignment, unsigned minsize_exp);
void hashtable_destroy (hashtable_t *table);
hash_t hashtable_count (const hashtable_t *table);
int hashtable_lookup (hashtable_t *table, hash_t hash, bool (*match) (void *, void *), void *match_arg, void **data);
int hashtable_insert (hashtable_t *table, void *data);
int hashtable_delete (hashtable_t *table, void *data);
bool hashtable_traverse (hashtable_t *table, bool (*callback) (void *, void *), void *user_data);
void hashtable_start_bulk_update (hashtable_t *table);
int hashtable_end_bulk_update (hashtable_t *table);
#ifdef HASHTABLE_STATS
void hashtable_clearstats (hashtable_t *table);
#endif


/* FNV-1a hash algorithm - implementation */

#if ULONG_MAX == 4294967295UL

/* long is 32 bits */
#define HASH_INIT 2166136261UL
#define HASH_MULT 16777619UL

#elif ULONG_MAX == 18446744073709551615UL

/* long is 64 bits */
#define HASH_INIT 14695981039346656037UL
#define HASH_MULT 1099511628211UL

#else
#error ULONG_MAX must be 2^32-1 or 2^64-1
#endif


static inline hash_t
hash_update (hash_t h, unsigned char input)
{
    return (h ^ input) * HASH_MULT;
}


static inline hash_t
hash_update_str (hash_t h, const char *str)
{
    for (; *str != 0; str++)
        h = hash_update (h, (unsigned char) *str);
    return h;
}


static inline hash_t
hash_update_mem (hash_t h, const void *data, unsigned len)
{
    const unsigned char *c = data;
    unsigned i;
    for (i = 0; i < len; i++)
        h = hash_update (h, c[i]);
    return h;
}

#endif /* UTILS_HASHTABLE_H */
