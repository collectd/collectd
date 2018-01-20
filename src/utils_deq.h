#ifndef utils_deq_h
#define utils_deq_h 1

#include <assert.h>
#include <memory.h>
#include <stdlib.h>

#define CT_ASSERT(exp)                                                         \
  { assert(exp); }

#define NEW(t) (t *)malloc(sizeof(t))
#define NEW_ARRAY(t, n) (t *)malloc(sizeof(t) * (n))
#define NEW_PTR_ARRAY(t, n) (t **)malloc(sizeof(t *) * (n))

//
// If available, use aligned_alloc for cache-line-aligned allocations. Otherwise
// fall back to plain malloc.
//
#define NEW_CACHE_ALIGNED(t, p)                                                \
  do {                                                                         \
    if (posix_memalign(                                                        \
            (void *)&(p), 64,                                                  \
            (sizeof(t) + (sizeof(t) % 64 ? 64 - (sizeof(t) % 64) : 0))) != 0)  \
      (p) = 0;                                                                 \
  } while (0)

#define ALLOC_CACHE_ALIGNED(s, p)                                              \
  do {                                                                         \
    if (posix_memalign((void *)&(p), 64,                                       \
                       (s + (s % 64 ? 64 - (s % 64) : 0))) != 0)               \
      (p) = 0;                                                                 \
  } while (0)

#define ZERO(p) memset(p, 0, sizeof(*p))

#define DEQ_DECLARE(i, d)                                                      \
  typedef struct {                                                             \
    i *head;                                                                   \
    i *tail;                                                                   \
    i *scratch;                                                                \
    size_t size;                                                               \
  } d

#define DEQ_LINKS_N(n, t)                                                      \
  t *prev##n;                                                                  \
  t *next##n
#define DEQ_LINKS(t) DEQ_LINKS_N(, t)
#define DEQ_EMPTY                                                              \
  { 0, 0, 0, 0 }

#define DEQ_INIT(d)                                                            \
  do {                                                                         \
    (d).head = 0;                                                              \
    (d).tail = 0;                                                              \
    (d).scratch = 0;                                                           \
    (d).size = 0;                                                              \
  } while (0)
#define DEQ_IS_EMPTY(d) ((d).head == 0)
#define DEQ_ITEM_INIT_N(n, i)                                                  \
  do {                                                                         \
    (i)->next##n = 0;                                                          \
    (i)->prev##n = 0;                                                          \
  } while (0)
#define DEQ_ITEM_INIT(i) DEQ_ITEM_INIT_N(, i)
#define DEQ_HEAD(d) ((d).head)
#define DEQ_TAIL(d) ((d).tail)
#define DEQ_SIZE(d) ((d).size)
#define DEQ_NEXT_N(n, i) (i)->next##n
#define DEQ_NEXT(i) DEQ_NEXT_N(, i)
#define DEQ_PREV_N(n, i) (i)->prev##n
#define DEQ_PREV(i) DEQ_PREV_N(, i)
#define DEQ_MOVE(d1, d2)                                                       \
  do {                                                                         \
    d2 = d1;                                                                   \
    DEQ_INIT(d1);                                                              \
  } while (0)
/**
 *@pre ptr points to first element of deq
 *@post ptr points to first element of deq that passes test, or 0. Test should
 *involve ptr.
 */
#define DEQ_FIND_N(n, ptr, test)                                               \
  while ((ptr) && !(test))                                                     \
    ptr = DEQ_NEXT_N(n, ptr);
#define DEQ_FIND(ptr, test) DEQ_FIND_N(, ptr, test)

#define DEQ_INSERT_HEAD_N(n, d, i)                                             \
  do {                                                                         \
    CT_ASSERT((i)->next##n == 0);                                              \
    CT_ASSERT((i)->prev##n == 0);                                              \
    if ((d).head) {                                                            \
      (i)->next##n = (d).head;                                                 \
      (d).head->prev##n = i;                                                   \
    } else {                                                                   \
      (d).tail = i;                                                            \
      (i)->next##n = 0;                                                        \
      CT_ASSERT((d).size == 0);                                                \
    }                                                                          \
    (i)->prev##n = 0;                                                          \
    (d).head = i;                                                              \
    (d).size++;                                                                \
  } while (0)
#define DEQ_INSERT_HEAD(d, i) DEQ_INSERT_HEAD_N(, d, i)

#define DEQ_INSERT_TAIL_N(n, d, i)                                             \
  do {                                                                         \
    CT_ASSERT((i)->next##n == 0);                                              \
    CT_ASSERT((i)->prev##n == 0);                                              \
    if ((d).tail) {                                                            \
      (i)->prev##n = (d).tail;                                                 \
      (d).tail->next##n = i;                                                   \
    } else {                                                                   \
      (d).head = i;                                                            \
      (i)->prev##n = 0;                                                        \
      CT_ASSERT((d).size == 0);                                                \
    }                                                                          \
    (i)->next##n = 0;                                                          \
    (d).tail = i;                                                              \
    (d).size++;                                                                \
  } while (0)
#define DEQ_INSERT_TAIL(d, i) DEQ_INSERT_TAIL_N(, d, i)

#define DEQ_REMOVE_HEAD_N(n, d)                                                \
  do {                                                                         \
    CT_ASSERT((d).head);                                                       \
    if ((d).head) {                                                            \
      (d).scratch = (d).head;                                                  \
      (d).head = (d).head->next##n;                                            \
      if ((d).head == 0) {                                                     \
        (d).tail = 0;                                                          \
        CT_ASSERT((d).size == 1);                                              \
      } else                                                                   \
        (d).head->prev##n = 0;                                                 \
      (d).size--;                                                              \
      (d).scratch->next##n = 0;                                                \
      (d).scratch->prev##n = 0;                                                \
    }                                                                          \
  } while (0)
#define DEQ_REMOVE_HEAD(d) DEQ_REMOVE_HEAD_N(, d)

#define DEQ_REMOVE_TAIL_N(n, d)                                                \
  do {                                                                         \
    CT_ASSERT((d).tail);                                                       \
    if ((d).tail) {                                                            \
      (d).scratch = (d).tail;                                                  \
      (d).tail = (d).tail->prev##n;                                            \
      if ((d).tail == 0) {                                                     \
        (d).head = 0;                                                          \
        CT_ASSERT((d).size == 1);                                              \
      } else                                                                   \
        (d).tail->next##n = 0;                                                 \
      (d).size--;                                                              \
      (d).scratch->next##n = 0;                                                \
      (d).scratch->prev##n = 0;                                                \
    }                                                                          \
  } while (0)
#define DEQ_REMOVE_TAIL(d) DEQ_REMOVE_TAIL_N(, d)

#define DEQ_INSERT_AFTER_N(n, d, i, a)                                         \
  do {                                                                         \
    CT_ASSERT((i)->next##n == 0);                                              \
    CT_ASSERT((i)->prev##n == 0);                                              \
    CT_ASSERT(a);                                                              \
    if ((a)->next##n)                                                          \
      (a)->next##n->prev##n = (i);                                             \
    else                                                                       \
      (d).tail = (i);                                                          \
    (i)->next##n = (a)->next##n;                                               \
    (i)->prev##n = (a);                                                        \
    (a)->next##n = (i);                                                        \
    (d).size++;                                                                \
  } while (0)
#define DEQ_INSERT_AFTER(d, i, a) DEQ_INSERT_AFTER_N(, d, i, a)

#define DEQ_REMOVE_N(n, d, i)                                                  \
  do {                                                                         \
    if ((i)->next##n)                                                          \
      (i)->next##n->prev##n = (i)->prev##n;                                    \
    else                                                                       \
      (d).tail = (i)->prev##n;                                                 \
    if ((i)->prev##n)                                                          \
      (i)->prev##n->next##n = (i)->next##n;                                    \
    else                                                                       \
      (d).head = (i)->next##n;                                                 \
    CT_ASSERT((d).size > 0);                                                   \
    (d).size--;                                                                \
    (i)->next##n = 0;                                                          \
    (i)->prev##n = 0;                                                          \
    CT_ASSERT((d).size || (!(d).head && !(d).tail));                           \
  } while (0)
#define DEQ_REMOVE(d, i) DEQ_REMOVE_N(, d, i)

#define DEQ_APPEND_N(n, d1, d2)                                                \
  do {                                                                         \
    if (!(d1).head)                                                            \
      (d1) = (d2);                                                             \
    else if ((d2).head) {                                                      \
      (d1).tail->next##n = (d2).head;                                          \
      (d2).head->prev##n = (d1).tail;                                          \
      (d1).tail = (d2).tail;                                                   \
      (d1).size += (d2).size;                                                  \
    }                                                                          \
    DEQ_INIT(d2);                                                              \
  } while (0)
#define DEQ_APPEND(d1, d2) DEQ_APPEND_N(, d1, d2)

#endif
