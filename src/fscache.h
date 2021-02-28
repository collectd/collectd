/* ANSI-C code produced by gperf version 3.1 */
/* Command-line: gperf fscache.gperf  */
/* Computed positions: -k'5,8,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 9 "fscache.gperf"

#line 11 "fscache.gperf"
struct fscache_metric { char *key; char *name; metric_type_t type; char *help; };
/* maximum key range = 233, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
fscache_hash (register const char *str, register size_t len)
{
  static const unsigned char asso_values[] =
    {
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241,  70, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241,   5,  50,  10,
       72,   5,  90,  20,  55,  52,   2,  15,  20,  27,
        0,   0,  80,  90,  27,  85,  20,  55,  30, 241,
       40,   0, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241, 241, 241, 241,
      241, 241, 241, 241, 241, 241, 241
    };
  register unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[7]+1];
      /*FALLTHROUGH*/
      case 7:
      case 6:
      case 5:
        hval += asso_values[(unsigned char)str[4]];
        break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

const struct fscache_metric *
fscache_get_key (register const char *str, register size_t len)
{
  enum
    {
      FSCACHE_TOTAL_KEYWORDS = 101,
      FSCACHE_MIN_WORD_LENGTH = 5,
      FSCACHE_MAX_WORD_LENGTH = 10,
      FSCACHE_MIN_HASH_VALUE = 8,
      FSCACHE_MAX_HASH_VALUE = 240
    };

  static const unsigned char lengthtable[] =
    {
       0,  0,  0,  0,  0,  0,  0,  0,  8,  0,  0,  6,  7,  6,
       0,  0,  0,  7,  0,  0, 10,  0,  0,  0,  0,  5,  0,  7,
       8,  0, 10,  6,  0,  0,  9, 10,  0,  0,  8,  9, 10,  0,
      10,  0,  9, 10,  0, 10,  8,  9, 10,  0, 10,  8,  0, 10,
       0,  0,  6,  9,  8,  6, 10,  8,  9, 10,  0,  0,  9,  9,
      10,  0, 10,  0,  0, 10,  0, 10,  8,  9, 10,  0, 10,  0,
       7, 10,  9, 10,  8,  0, 10,  0, 10,  8,  9, 10,  6, 10,
       8,  9, 10,  0,  0,  0, 10, 10,  0, 10,  0,  0, 10,  0,
      10,  0,  9, 10,  0,  0,  0,  9, 10,  9, 10,  6,  0, 10,
       0, 10,  8,  0, 10,  0,  0,  0,  9, 10,  0,  0,  0,  0,
      10,  0,  0,  0,  9, 10,  0, 10,  0,  0, 10,  0, 10,  0,
       9,  0,  9, 10,  0,  9,  0,  0,  0,  0,  0, 10,  0,  0,
       0, 10, 10,  0,  0,  0,  9,  8,  0,  0,  0,  9, 10,  0,
       0,  0,  0, 10,  0, 10,  0,  9,  0,  0,  0,  0,  9,  0,
       0,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0,  0,
       0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
       0,  0,  9,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
       0,  0, 10
    };
  static const struct fscache_metric wordlist[] =
    {
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 42 "fscache.gperf"
      {"Relinqsn", "fscache_relinquishes_total", METRIC_TYPE_COUNTER, "Total number of relinquish cookie requests seen"},
      {""}, {""},
#line 87 "fscache.gperf"
      {"Opscan", "fscache_op_cancelled_total", METRIC_TYPE_COUNTER, "Total number of async ops cancelled"},
#line 69 "fscache.gperf"
      {"Storesn", "fscache_stores_total", METRIC_TYPE_COUNTER, "Total number of storage (write) requests seen"},
#line 88 "fscache.gperf"
      {"Opsrej", "fscache_op_rejected_total", METRIC_TYPE_COUNTER, "Total number of async ops rejected due to object lookup/create failure"},
      {""}, {""}, {""},
#line 51 "fscache.gperf"
      {"Allocsn", "fscache_allocs_total", METRIC_TYPE_COUNTER, "Total number of allocation requests seen"},
      {""}, {""},
#line 99 "fscache.gperf"
      {"CacheOpdro", "fscache_cacheop_drop_object", METRIC_TYPE_GAUGE, "Number of in-progress drop_object() cache ops"},
      {""}, {""}, {""}, {""},
#line 92 "fscache.gperf"
      {"Opsgc", "fscache_op_gc_total", METRIC_TYPE_COUNTER, "Total number of deferred-release async ops garbage collected"},
      {""},
#line 37 "fscache.gperf"
      {"Invalsn", "fscache_invalidates_total", METRIC_TYPE_COUNTER, "Total number of invalidations"},
#line 39 "fscache.gperf"
      {"Updatesn", "fscache_updates_total", METRIC_TYPE_COUNTER, "Total number of update cookie requests seen"},
      {""},
#line 43 "fscache.gperf"
      {"Relinqsnul", "fscache_relinquishes_null_total", METRIC_TYPE_COUNTER, "Total number of relinquish cookie given a NULL parent"},
#line 91 "fscache.gperf"
      {"Opsrel", "fscache_op_release_total", METRIC_TYPE_COUNTER, "Total number of async ops released (should equal ini=N when idle)"},
      {""}, {""},
#line 81 "fscache.gperf"
      {"VmScanbsy", "fscache_store_vmscan_busy_total", METRIC_TYPE_COUNTER, "Total number of release requests ignored due to in-progress store"},
#line 101 "fscache.gperf"
      {"CacheOpsyn", "fscache_cacheop_sync_cache", METRIC_TYPE_GAUGE, "Number of in-progress sync_cache() cache ops"},
      {""}, {""},
#line 59 "fscache.gperf"
      {"Retrvlsn", "fscache_retrievals_total", METRIC_TYPE_COUNTER, "Total number of retrieval (read) requests seen"},
#line 55 "fscache.gperf"
      {"Allocsint", "fscache_allocs_intr_total", METRIC_TYPE_COUNTER, "Total number of allocation requests aborted -ERESTARTSYS"},
#line 17 "fscache.gperf"
      {"Objectsnal", "fscache_object_no_alloc_total", METRIC_TYPE_COUNTER, "Total number of object allocation failures"},
      {""},
#line 94 "fscache.gperf"
      {"CacheOpluo", "fscache_cacheop_lookup_object", METRIC_TYPE_GAUGE, "Number of in-progress lookup_object() cache ops"},
      {""},
#line 75 "fscache.gperf"
      {"Storesrun", "fscache_store_calls_total", METRIC_TYPE_COUNTER, "Total number of store requests granted CPU time"},
#line 98 "fscache.gperf"
      {"CacheOpupo", "fscache_cacheop_update_object", METRIC_TYPE_GAUGE, "Number of in-progress update_object() cache ops"},
      {""},
#line 97 "fscache.gperf"
      {"CacheOpinv", "fscache_cacheop_invalidate_object" , METRIC_TYPE_GAUGE, "Number of in-progress invalidate_object() cache ops"},
#line 70 "fscache.gperf"
      {"Storesok", "fscache_stores_ok_total", METRIC_TYPE_COUNTER, "Total number of successful store requests"},
#line 58 "fscache.gperf"
      {"Allocsabt", "fscache_allocs_object_dead_total", METRIC_TYPE_COUNTER, "Total number of allocation requests aborted due to object death"},
#line 40 "fscache.gperf"
      {"Updatesnul", "fscache_updates_null_total", METRIC_TYPE_COUNTER, "Total number of update requests given a NULL parent"},
      {""},
#line 95 "fscache.gperf"
      {"CacheOpluc", "fscache_cacheop_lookup_complete", METRIC_TYPE_GAUGE, "Number of in-progress lookup_complete() cache ops"},
#line 52 "fscache.gperf"
      {"Allocsok", "fscache_allocs_ok_total", METRIC_TYPE_COUNTER, "Total number of successful allocation requests"},
      {""},
#line 111 "fscache.gperf"
      {"CacheEvstl", "fscache_cache_stale_objects_total", METRIC_TYPE_COUNTER, "Total number of stale objects deleted"},
      {""}, {""},
#line 89 "fscache.gperf"
      {"Opsini", "fscache_op_initialised_total", METRIC_TYPE_COUNTER, "Total number of async ops initialised"},
#line 38 "fscache.gperf"
      {"Invalsrun", "fscache_invalidates_run_total", METRIC_TYPE_COUNTER, "Total number of invalidations granted CPU time"},
#line 26 "fscache.gperf"
      {"Acquiren", "fscache_acquires_total", METRIC_TYPE_COUNTER, "Total number of acquire cookie requests seen"},
#line 85 "fscache.gperf"
      {"Opsrun", "fscache_op_run_total", METRIC_TYPE_COUNTER, "Total number of times async ops given CPU time"},
#line 64 "fscache.gperf"
      {"Retrvlsint", "fscache_retrievals_intr_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests aborted -ERESTARTSYS"},
#line 32 "fscache.gperf"
      {"Lookupsn", "fscache_object_lookups_total", METRIC_TYPE_COUNTER, "Total number of lookup calls made on cache backends"},
#line 82 "fscache.gperf"
      {"VmScancan", "fscache_store_vmscan_cancelled_total", METRIC_TYPE_COUNTER, "Total number of page stores cancelled due to release request"},
#line 93 "fscache.gperf"
      {"CacheOpalo", "fscache_cacheop_alloc_object", METRIC_TYPE_GAUGE, "Number of in-progress alloc_object() cache ops"},
      {""}, {""},
#line 78 "fscache.gperf"
      {"Storesolm", "fscache_store_pages_over_limit_total", METRIC_TYPE_COUNTER, "Total number of store requests over store limit"},
#line 71 "fscache.gperf"
      {"Storesagn", "fscache_stores_again_total", METRIC_TYPE_COUNTER, "Total number of store requests on a page already pending storage"},
#line 96 "fscache.gperf"
      {"CacheOpgro", "fscache_cacheop_grab_object", METRIC_TYPE_GAUGE, "Number of in-progress grab_object() cache ops"},
      {""},
#line 28 "fscache.gperf"
      {"Acquirenoc", "fscache_acquires_no_cache_total", METRIC_TYPE_COUNTER, "Total number of acquire requests rejected due to no cache available"},
      {""}, {""},
#line 102 "fscache.gperf"
      {"CacheOpatc", "fscache_cacheop_attr_changed", METRIC_TYPE_GAUGE, "Number of in-progress attr_changed() cache ops"},
      {""},
#line 44 "fscache.gperf"
      {"Relinqswcr", "fscache_relinquishes_waitcrt_total", METRIC_TYPE_COUNTER, "Total number of relinquish cookie waited on completion of creation"},
#line 46 "fscache.gperf"
      {"AttrChgn", "fscache_attr_changed_total", METRIC_TYPE_COUNTER, "Total number of attribute changed requests seen"},
#line 57 "fscache.gperf"
      {"Allocsowt", "fscache_alloc_op_waits_total", METRIC_TYPE_COUNTER, "Total number of allocation requests waited for CPU time"},
#line 16 "fscache.gperf"
      {"Objectsalc", "fscache_object_alloc_total", METRIC_TYPE_COUNTER, "Total number of objects allocated"},
      {""},
#line 27 "fscache.gperf"
      {"Acquirenul", "fscache_acquires_null_total", METRIC_TYPE_COUNTER, "Total number of acquire requests given a NULL parent"},
      {""},
#line 84 "fscache.gperf"
      {"Opspend", "fscache_op_pending_total", METRIC_TYPE_COUNTER, "Total number of times async ops added to pending queues"},
#line 33 "fscache.gperf"
      {"Lookupsneg", "fscache_object_lookups_negative_total", METRIC_TYPE_COUNTER, "Total number of negative lookups made"},
#line 77 "fscache.gperf"
      {"Storesrxd", "fscache_store_radix_deletes_total", METRIC_TYPE_COUNTER, "Total number of store requests deleted from tracking tree"},
#line 14 "fscache.gperf"
      {"Cookiesdat", "fscache_cookie_data_total", METRIC_TYPE_COUNTER, "Total number of data storage cookies allocated"},
#line 83 "fscache.gperf"
      {"VmScanwt", "fscache_store_vmscan_wait_total", METRIC_TYPE_COUNTER, "Total number of page stores waited for CPU time"},
      {""},
#line 18 "fscache.gperf"
      {"Objectsavl", "fscache_object_avail_total", METRIC_TYPE_COUNTER, "Total number of objects that reached the available state"},
      {""},
#line 15 "fscache.gperf"
      {"Cookiesspc", "fscache_cookie_special_total", METRIC_TYPE_COUNTER, "Total number of special cookies allocated"},
#line 53 "fscache.gperf"
      {"Allocswt", "fscache_allocs_wait_total", METRIC_TYPE_COUNTER, "Total number of allocation requests that waited on lookup completion"},
#line 80 "fscache.gperf"
      {"VmScangon", "fscache_store_vmscan_gone_total", METRIC_TYPE_COUNTER, "Total number of release requests against pages stored by time lock granted"},
#line 110 "fscache.gperf"
      {"CacheEvnsp", "fscache_cache_no_space_reject_total", METRIC_TYPE_COUNTER, "Total number of object lookups/creations rejected due to lack of space"},
#line 86 "fscache.gperf"
      {"Opsenq", "fscache_op_enqueue_total", METRIC_TYPE_COUNTER, "Total number of times async ops queued for processing"},
#line 19 "fscache.gperf"
      {"Objectsded", "fscache_object_dead_total", METRIC_TYPE_COUNTER, "Total mumber of objects that reached the dead state"},
#line 21 "fscache.gperf"
      {"ChkAuxok", "fscache_checkaux_okay_total", METRIC_TYPE_COUNTER, "Total number of objects that passed a coherency check"},
#line 61 "fscache.gperf"
      {"Retrvlswt", "fscache_retrievals_wait_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests that waited on lookup completion"},
#line 109 "fscache.gperf"
      {"CacheOpdsp", "fscache_cacheop_dissociate_pages", METRIC_TYPE_GAUGE, "Number of in-progress dissociate_pages() cache ops"},
      {""}, {""}, {""},
#line 13 "fscache.gperf"
      {"Cookiesidx", "fscache_cookie_index_total", METRIC_TYPE_COUNTER, "Total number of index cookies allocated"},
#line 100 "fscache.gperf"
      {"CacheOppto", "fscache_cacheop_put_object", METRIC_TYPE_GAUGE, "Number of in-progress put_object() cache ops"},
      {""},
#line 113 "fscache.gperf"
      {"CacheEvcul", "fscache_cache_culled_objects_total", METRIC_TYPE_COUNTER, "Total number of objects culled"},
      {""}, {""},
#line 68 "fscache.gperf"
      {"Retrvlsabt", "fscache_retrievals_object_dead_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests aborted due to object death"},
      {""},
#line 62 "fscache.gperf"
      {"Retrvlsnod", "fscache_retrievals_nodata_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests returned -ENODATA"},
      {""},
#line 72 "fscache.gperf"
      {"Storesnbf", "fscache_stores_nobufs_total", METRIC_TYPE_COUNTER, "Total number of store requests rejected -ENOBUFS"},
#line 41 "fscache.gperf"
      {"Updatesrun", "fscache_updates_run_total", METRIC_TYPE_COUNTER, "Total number of update requests granted CPU time"},
      {""}, {""}, {""},
#line 54 "fscache.gperf"
      {"Allocsnbf", "fscache_allocs_nobufs_total", METRIC_TYPE_COUNTER, "Total number of allocation requests rejected -ENOBUFS"},
#line 36 "fscache.gperf"
      {"Lookupstmo", "fscache_object_lookups_timed_out_total", METRIC_TYPE_COUNTER, "Total number of lookups timed out and requeued"},
#line 73 "fscache.gperf"
      {"Storesoom", "fscache_stores_oom_total", METRIC_TYPE_COUNTER, "Total number of store requests failed -ENOMEM"},
#line 45 "fscache.gperf"
      {"Relinqsrtr", "fscache_relinquishes_retire_total", METRIC_TYPE_COUNTER, "Total number of relinquish retries"},
#line 90 "fscache.gperf"
      {"Opsdfr", "fscache_op_deferred_release_total", METRIC_TYPE_COUNTER, "Total number of async ops queued for deferred release"},
      {""},
#line 108 "fscache.gperf"
      {"CacheOpucp", "fscache_cacheop_uncache_page", METRIC_TYPE_GAUGE, "Number of in-progress uncache_page() cache ops"},
      {""},
#line 112 "fscache.gperf"
      {"CacheEvrtr", "fscache_cache_retired_objects_total", METRIC_TYPE_COUNTER, "Total number of objects retired when relinquished"},
#line 24 "fscache.gperf"
      {"Pagesmrk", "fscache_marks_total", METRIC_TYPE_COUNTER, "Total number of pages marked as being cached"},
      {""},
#line 63 "fscache.gperf"
      {"Retrvlsnbf", "fscache_retrievals_nobufs_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests rejected -ENOBUFS"},
      {""}, {""}, {""},
#line 60 "fscache.gperf"
      {"Retrvlsok", "fscache_retrievals_ok_total", METRIC_TYPE_COUNTER, "Total number of successful retrieval requests"},
#line 107 "fscache.gperf"
      {"CacheOpwrp", "fscache_cacheop_write_page", METRIC_TYPE_GAUGE, "Number of in-progress write_page() cache ops"},
      {""}, {""}, {""}, {""},
#line 67 "fscache.gperf"
      {"Retrvlsowt", "fscache_retrieval_op_waits_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests waited for CPU time"},
      {""}, {""}, {""},
#line 20 "fscache.gperf"
      {"ChkAuxnon", "fscache_checkaux_none_total", METRIC_TYPE_COUNTER, "Total number of objects that didn't have a coherency check"},
#line 105 "fscache.gperf"
      {"CacheOpalp", "fscache_cacheop_allocate_page", METRIC_TYPE_GAUGE, "Number of in-progress allocate_page() cache ops"},
      {""},
#line 65 "fscache.gperf"
      {"Retrvlsoom", "fscache_retrievals_nomem_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests failed -ENOMEM"},
      {""}, {""},
#line 106 "fscache.gperf"
      {"CacheOpals", "fscache_cacheop_allocate_pages", METRIC_TYPE_GAUGE, "Number of in-progress allocate_pages() cache ops"},
      {""},
#line 30 "fscache.gperf"
      {"Acquirenbf", "fscache_acquires_nobufs_total", METRIC_TYPE_COUNTER, "Total number of acquire requests rejected due to error"},
      {""},
#line 76 "fscache.gperf"
      {"Storespgs", "fscache_store_pages_total", METRIC_TYPE_COUNTER, "Total number of pages given store requests processing time"},
      {""},
#line 29 "fscache.gperf"
      {"Acquireok", "fscache_acquires_ok_total", METRIC_TYPE_COUNTER, "Total number of acquire requests succeeded"},
#line 35 "fscache.gperf"
      {"Lookupscrt", "fscache_object_created_total", METRIC_TYPE_COUNTER, "Total number of objects created by lookup"},
      {""},
#line 23 "fscache.gperf"
      {"ChkAuxobs", "fscache_checkaux_obsolete_total", METRIC_TYPE_COUNTER, "Total number of objects that were declared obsolete"},
      {""}, {""}, {""}, {""}, {""},
#line 50 "fscache.gperf"
      {"AttrChgrun", "fscache_attr_changed_calls_total", METRIC_TYPE_COUNTER, "Total number of attribute changed ops given CPU time"},
      {""}, {""}, {""},
#line 31 "fscache.gperf"
      {"Acquireoom", "fscache_acquires_oom_total", METRIC_TYPE_COUNTER, "Total number of acquire requests failed on ENOMEM"},
#line 48 "fscache.gperf"
      {"AttrChgnbf", "fscache_attr_changed_nobufs_total", METRIC_TYPE_COUNTER, "Total number of attribute changed rejected -ENOBUFS"},
      {""}, {""}, {""},
#line 47 "fscache.gperf"
      {"AttrChgok", "fscache_attr_changed_ok_total", METRIC_TYPE_COUNTER,  "Total number of attribute changed requests queued"},
#line 25 "fscache.gperf"
      {"Pagesunc", "fscache_uncaches_total", METRIC_TYPE_COUNTER, "Total number of uncache page requests seen"},
      {""}, {""}, {""},
#line 79 "fscache.gperf"
      {"VmScannos", "fscache_store_vmscan_not_storing_total", METRIC_TYPE_COUNTER, "Total number of release requests against pages with no pending store"},
#line 103 "fscache.gperf"
      {"CacheOprap", "fscache_cacheop_read_or_alloc_page", METRIC_TYPE_GAUGE, "Number of in-progress read_or_alloc_page() cache ops"},
      {""}, {""}, {""}, {""},
#line 104 "fscache.gperf"
      {"CacheOpras", "fscache_cacheop_read_or_alloc_pages", METRIC_TYPE_GAUGE, "Number of in-progress read_or_alloc_pages() cache ops"},
      {""},
#line 49 "fscache.gperf"
      {"AttrChgoom", "fscache_attr_changed_nomem_total", METRIC_TYPE_COUNTER, "Total number of attribute changed failed -ENOMEM"},
      {""},
#line 74 "fscache.gperf"
      {"Storesops", "fscache_store_ops_total", METRIC_TYPE_COUNTER, "Total number of store requests submitted"},
      {""}, {""}, {""}, {""},
#line 56 "fscache.gperf"
      {"Allocsops", "fscache_alloc_ops_total", METRIC_TYPE_COUNTER, "Total number of allocation requests submitted"},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""},
#line 66 "fscache.gperf"
      {"Retrvlsops", "fscache_retrieval_ops_total", METRIC_TYPE_COUNTER, "Total number of retrieval requests submitted"},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""},
#line 22 "fscache.gperf"
      {"ChkAuxupd", "fscache_checkaux_update_total", METRIC_TYPE_COUNTER, "Total number of objects that needed a coherency data update"},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""},
#line 34 "fscache.gperf"
      {"Lookupspos", "fscache_object_lookups_positive_total", METRIC_TYPE_COUNTER, "Total number of positive lookups made"}
    };

  if (len <= FSCACHE_MAX_WORD_LENGTH && len >= FSCACHE_MIN_WORD_LENGTH)
    {
      register unsigned int key = fscache_hash (str, len);

      if (key <= FSCACHE_MAX_HASH_VALUE)
        if (len == lengthtable[key])
          {
            register const char *s = wordlist[key].key;

            if (*str == *s && !memcmp (str + 1, s + 1, len - 1))
              return &wordlist[key];
          }
    }
  return 0;
}
