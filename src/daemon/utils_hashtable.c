#include "utils_hashtable.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>


/*
 * Implementation notes:
 *
 * The size of the table (i.e. the number of buckets) is always a
 * power of 2.
 *
 * We use double hashing where both h1 and h2 are taken from the
 * user-supplied hash value. The low order bits go into h1, the next
 * higher ones into h2. The probe sequence is then
 * h1 + i * h2 for i = 0, 1, 2, ...
 * h2 needs to be relatively prime to the table size to ensure all
 * buckets get probed eventually. This is ensured by setting h2's
 * lowest bit to 1, making it an odd number while the size is a power
 * of 2.
 *
 * To ensure we have some entropy in the higher order bits (so we get
 * sensible values for h2), the user-supplied hash value is multiplied
 * by HASH_MULT (a relatively large prime).
 *
 * When an entry is deleted, we put a "tombstone" marked by a status of
 * BUCKET_TOMB in its location. This is to ensure the probe sequence
 * for other entries does not get interrupted.
 *
 * Tombstones must be removed eventually if they grow too many; this
 * is realized with a rehash: A new table is allocated and all entries
 * from the old table are reinserted into the new one.
 *
 * The same rehash procedure is also used to resize the table. It both
 * grows and shrinks dynamically according to the number of elements
 * it contains. For optimum performance, we maintain a load factor
 * between 1/8 and 1/2 the table size; the number of tombstones is kept
 * below 1/4 the size.
 */


#define MIN_ALIGN 16


enum bucket_status_e {
    BUCKET_EMPTY    = 0,
    BUCKET_USED     = 1,
    BUCKET_TOMB     = 2,
};

typedef enum bucket_status_e bucket_status_t;


struct bucket_s {
    hash_t hash;
    bucket_status_t status;
};

typedef struct bucket_s bucket_t;


static inline uintptr_t do_align (uintptr_t base, uintptr_t alignment);
static inline bucket_t *get_bucket (hashtable_t *table, hash_t index);
static inline bucket_t *get_bucket2 (char *arena, hash_t stride, hash_t index);
static int alloc_arena (hashtable_t *table, unsigned size_exp);
int hashtable_init (hashtable_t *table, unsigned data_size, unsigned alignment, unsigned minsize_exp);
static inline hash_t mod_size (const hashtable_t *table, hash_t x);
static inline hash_t get_h1 (const hashtable_t *table, hash_t hash);
static inline hash_t get_h2 (const hashtable_t *table, hash_t hash);
static inline void *bucket_to_user (const hashtable_t *table, bucket_t *bucket);
static inline bucket_t *user_to_bucket (const hashtable_t *table, void *user);
static int rehash (hashtable_t *table, unsigned size_exp);
static int check_grow (hashtable_t *table);
static int check_shrink (hashtable_t *table, bool bulk);
static int hashtable_lookup_internal (hashtable_t *table, hash_t hash,
        bool (*match) (void *, void *), void *match_arg, void **data);


static inline uintptr_t
do_align (uintptr_t base, uintptr_t alignment)
{
    return (base + alignment - 1) / alignment * alignment;
}


static inline bucket_t *
get_bucket (hashtable_t *table, hash_t index)
{
    return get_bucket2 (table->arena, table->bucket_stride, index);
}


static inline bucket_t *
get_bucket2 (char *arena, hash_t stride, hash_t index)
{
    return (bucket_t *) (arena + index * stride);
}


static int
alloc_arena (hashtable_t *table, unsigned size_exp)
{
    const hash_t size = (hash_t) 1 << size_exp;
    /* Add extra bytes to allocation for possible alignment correction. */
    const hash_t bytes = size * table->bucket_stride + table->data_align - 1;
    void *const handle = malloc (bytes);
    if (handle == NULL)
        return ENOMEM;

    /*
     * Correct the pointer returned by malloc() for alignment.
     * Save the original pointer in arena_handle for a later free().
     */
    table->arena = (char *) do_align ((uintptr_t) handle, table->data_align);
    table->arena_handle = handle;
    table->size_exp = size_exp;
    table->size = size;
    table->used = 0;
    table->tombs = 0;
    table->bulk_update = 0;

    hash_t i;
    for (i = 0; i < table->size; i++)
        get_bucket (table, i)->status = BUCKET_EMPTY;

    return 0;
}


int
hashtable_init (hashtable_t *table, unsigned data_size, unsigned alignment, unsigned minsize_exp)
{
    /* Alignment must be a power of 2. */
    if ((alignment & (alignment - 1)) != 0)
        return EINVAL;

    if (alignment < MIN_ALIGN)
        alignment = MIN_ALIGN;

    if ((hash_t) 1 << minsize_exp == 0)
        return EINVAL;

    table->data_size = data_size;
    table->data_align = alignment;
    table->minsize_exp = minsize_exp;

    table->bucket_doff = do_align (sizeof (bucket_t), alignment);
    table->bucket_stride = table->bucket_doff + do_align (data_size, alignment);

#ifdef HASHTABLE_STATS
    hashtable_clearstats (table);
#endif

    return alloc_arena (table, minsize_exp);
}


#ifdef HASHTABLE_STATS
void
hashtable_clearstats (hashtable_t *table)
{
    int i;
    for (i = 0; i < HASHTABLE_STATS_MAX + 1; i++)
        table->stat_iter[i] = 0;
    table->total_ops = 0;
    table->total_iter = 0;
}
#endif


void
hashtable_destroy (hashtable_t *table)
{
    free (table->arena_handle);
}


hash_t
hashtable_count (const hashtable_t *table)
{
    return table->used;
}


static inline hash_t
mod_size (const hashtable_t *table, hash_t x)
{
    return x & (table->size - 1);
}


static inline hash_t
get_h1 (const hashtable_t *table, hash_t hash)
{
    return mod_size (table, hash);
}


static inline hash_t
get_h2 (const hashtable_t *table, hash_t hash)
{
    return mod_size (table, (hash >> (table->size_exp - 1)) | 1);
}


static inline void *
bucket_to_user (const hashtable_t *table, bucket_t *bucket)
{
    return (char *) bucket + table->bucket_doff;
}


static inline bucket_t *
user_to_bucket (const hashtable_t *table, void *user)
{
    return (bucket_t *) ((char *) user - table->bucket_doff);
}


static int
rehash (hashtable_t *table, unsigned size_exp)
{
    const hash_t old_size = table->size;
    char *const old_arena = table->arena;
    void *const old_handle = table->arena_handle;

    int rc = alloc_arena (table, size_exp);
    if (rc != 0)
        return rc;

    hash_t i;
    for (i = 0; i < old_size; i++) {
        bucket_t *const old_bucket = get_bucket2 (old_arena, table->bucket_stride, i);
        if (old_bucket->status != BUCKET_USED)
            continue;

        void *data = NULL;
        rc = hashtable_lookup_internal (table, old_bucket->hash, NULL, NULL, &data);
        assert (rc == ENOENT);
        memcpy (data, bucket_to_user (table, old_bucket), table->data_size);
        user_to_bucket (table, data)->status = BUCKET_USED;
        table->used++;
    }

    free (old_handle);
    return 0;
}


int
hashtable_lookup (hashtable_t *table, hash_t hash, bool (*match) (void *, void *), void *match_arg, void **data)
{
    /*
     * Multiply user-supplied hash by HASH_MULT once to ensure we get
     * some entropy in the high-order bits even if the users supplies
     * a sucky hash value (i.e. a small number), so we have sensible
     * values for h2.
     */
    return hashtable_lookup_internal (table, hash * HASH_MULT, match, match_arg, data);
}


#ifdef HASHTABLE_STATS
static void
put_stats (hashtable_t *table, unsigned iter)
{
    table->total_iter += iter;
    table->total_ops++;
    if (iter > HASHTABLE_STATS_MAX)
        iter = HASHTABLE_STATS_MAX;
    table->stat_iter[iter]++;
}
#endif


static int
hashtable_lookup_internal (hashtable_t *table, hash_t hash, bool (*match) (void *, void *), void *match_arg, void **data)
{
    hash_t pos = get_h1 (table, hash);
    const hash_t h2 = get_h2 (table, hash);
    void *insert_here = NULL;
#ifdef HASHTABLE_STATS
    unsigned iter = 0;
#endif

    for (;;) {
        bucket_t *const bucket = get_bucket (table, pos);
        void *const user = bucket_to_user (table, bucket);

        switch (bucket->status) {
            case BUCKET_EMPTY:
                if (insert_here != NULL)
                    *data = insert_here;
                else
                    *data = user;
                user_to_bucket (table, *data)->hash = hash;
#ifdef HASHTABLE_STATS
                put_stats (table, iter);
#endif
                return ENOENT;

            case BUCKET_USED:
                /*
                 * One could check for bucket->hash == hash before
                 * calling the match function. This may give better
                 * or worse performance, depending on how expensive
                 * the match function is.
                 */
                if (match != NULL && match (user, match_arg)) {
                    *data = user;
#ifdef HASHTABLE_STATS
                    put_stats (table, iter);
#endif
                    return 0;
                }
                break;

            case BUCKET_TOMB:
                if (match == NULL) {
                    *data = user;
#ifdef HASHTABLE_STATS
                    put_stats (table, iter);
#endif
                    return ENOENT;
                }
                if (insert_here == NULL)
                    insert_here = user;
                break;
        }

        pos = mod_size (table, pos + h2);
#ifdef HASHTABLE_STATS
        iter++;
#endif
    }

    /* We can never get here. */
    assert (false);
}


static int
check_grow (hashtable_t *table)
{
    if (table->used > (table->size >> 1)) {
        DEBUG ("Rehashing to grow with used=%lu, tombs=%lu, size=%lu", table->used, table->tombs, table->size);
        return rehash (table, table->size_exp + 1);
    } else {
        return 0;
    }
}


static int
check_shrink (hashtable_t *table, bool bulk)
{
    /*
     * Only shrink if table gets below 1/8 of its capacity, so after
     * shrink it is at 1/4 its capacity. This may be wasteful, but if
     * we shrink such that the table is at 1/2 its capacity after
     * shrink, then the next insert would cause it to grow again
     * immediately, resulting in too many rehashes.
     */
    if (table->used <= (table->size >> 3) && table->size_exp > table->minsize_exp) {
        DEBUG ("Rehashing to shrink with used=%lu, tombs=%lu, size=%lu", table->used, table->tombs, table->size);
        unsigned new_exp;

        if (!bulk) {
            new_exp = table->size_exp - 1;
        } else {
            /*
             * We are at the end of a bulk update, so more than one entry
             * may have been deleted without a rehash. Determine the
             * desired new size from the number of elements left.
             */
            new_exp = table->minsize_exp;
            const hash_t min_size = table->used << 2;
            while ((hash_t) 1 << new_exp < min_size)
                new_exp++;
        }

        return rehash (table, new_exp);
    } else if (table->tombs > (table->size >> 2)) {
        DEBUG ("Rehashing to clean with used=%lu, tombs=%lu, size=%lu", table->used, table->tombs, table->size);
        return rehash (table, table->size_exp);
    } else {
        return 0;
    }
}


int
hashtable_insert (hashtable_t *table, void *data)
{
    bucket_t *const bucket = user_to_bucket (table, data);
    const bucket_status_t old_status = bucket->status;

    assert (bucket->status != BUCKET_USED);

    if (bucket->status == BUCKET_TOMB)
        table->tombs--;

    bucket->status = BUCKET_USED;
    table->used++;

    const int rc = check_grow (table);

    /*
     * We can't allow an insert if rehash fails lest the table will
     * be completely full at some point, so undo it.
     */
    if (rc != 0) {
        bucket->status = old_status;
        if (bucket->status == BUCKET_TOMB)
            table->tombs++;
        table->used--;
    }

    return rc;
}


int
hashtable_delete (hashtable_t *table, void *data)
{
    bucket_t *const bucket = user_to_bucket (table, data);

    assert (bucket->status == BUCKET_USED);

    bucket->status = BUCKET_TOMB;
    table->used--;
    table->tombs++;

    if (table->bulk_update != 0)
        return 0;
    else
        return check_shrink (table, false);
}


bool
hashtable_traverse (hashtable_t *table, bool (*callback) (void *, void *), void *user_data)
{
    hash_t i;
    for (i = 0; i < table->size; i++) {
        bucket_t *const bucket = get_bucket (table, i);
        if (bucket->status == BUCKET_USED
                && callback (bucket_to_user (table, bucket), user_data))
            return true;
    }
    return false;
}


void
hashtable_start_bulk_update (hashtable_t *table)
{
    table->bulk_update++;
}


int
hashtable_end_bulk_update (hashtable_t *table)
{
    if (--table->bulk_update != 0)
        return 0;
    else
        return check_shrink (table, true);
}
