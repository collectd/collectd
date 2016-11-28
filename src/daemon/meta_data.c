/**
 * collectd - src/meta_data.c
 * Copyright (C) 2008-2011  Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "meta_data.h"
#include "plugin.h"

#define MD_MAX_NONSTRING_CHARS 128

/*
 * Data types
 */
union meta_value_u {
  char *mv_string;
  int64_t mv_signed_int;
  uint64_t mv_unsigned_int;
  double mv_double;
  _Bool mv_boolean;
};
typedef union meta_value_u meta_value_t;

struct meta_entry_s;
typedef struct meta_entry_s meta_entry_t;
struct meta_entry_s {
  char *key;
  meta_value_t value;
  int type;
  meta_entry_t *next;
};

struct meta_data_s {
  meta_entry_t *head;
  pthread_mutex_t lock;
};

/*
 * Private functions
 */
static char *md_strdup(const char *orig) /* {{{ */
{
  size_t sz;
  char *dest;

  if (orig == NULL)
    return (NULL);

  sz = strlen(orig) + 1;
  dest = malloc(sz);
  if (dest == NULL)
    return (NULL);

  memcpy(dest, orig, sz);

  return (dest);
} /* }}} char *md_strdup */

static meta_entry_t *md_entry_alloc(const char *key) /* {{{ */
{
  meta_entry_t *e;

  e = calloc(1, sizeof(*e));
  if (e == NULL) {
    ERROR("md_entry_alloc: calloc failed.");
    return (NULL);
  }

  e->key = md_strdup(key);
  if (e->key == NULL) {
    free(e);
    ERROR("md_entry_alloc: md_strdup failed.");
    return (NULL);
  }

  e->type = 0;
  e->next = NULL;

  return (e);
} /* }}} meta_entry_t *md_entry_alloc */

/* XXX: The lock on md must be held while calling this function! */
static meta_entry_t *md_entry_clone_contents(const meta_entry_t *orig) /* {{{ */
{
  meta_entry_t *copy;

  /* WARNINGS :
   *  - we do not check that orig != NULL here. You should have done it before.
   *  - we do not set copy->next. DO NOT FORGET TO SET copy->next IN YOUR
   * FUNCTION
   */

  copy = md_entry_alloc(orig->key);
  if (copy == NULL)
    return (NULL);
  copy->type = orig->type;
  if (copy->type == MD_TYPE_STRING)
    copy->value.mv_string = strdup(orig->value.mv_string);
  else
    copy->value = orig->value;

  return (copy);
} /* }}} meta_entry_t *md_entry_clone_contents */

static meta_entry_t *md_entry_clone(const meta_entry_t *orig) /* {{{ */
{
  meta_entry_t *copy;

  if (orig == NULL)
    return (NULL);

  copy = md_entry_clone_contents(orig);

  copy->next = md_entry_clone(orig->next);
  return (copy);
} /* }}} meta_entry_t *md_entry_clone */

static void md_entry_free(meta_entry_t *e) /* {{{ */
{
  if (e == NULL)
    return;

  free(e->key);

  if (e->type == MD_TYPE_STRING)
    free(e->value.mv_string);

  if (e->next != NULL)
    md_entry_free(e->next);

  free(e);
} /* }}} void md_entry_free */

static int md_entry_insert(meta_data_t *md, meta_entry_t *e) /* {{{ */
{
  meta_entry_t *this;
  meta_entry_t *prev;

  if ((md == NULL) || (e == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  prev = NULL;
  this = md->head;
  while (this != NULL) {
    if (strcasecmp(e->key, this->key) == 0)
      break;

    prev = this;
    this = this->next;
  }

  if (this == NULL) {
    /* This key does not exist yet. */
    if (md->head == NULL)
      md->head = e;
    else {
      assert(prev != NULL);
      prev->next = e;
    }

    e->next = NULL;
  } else /* (this != NULL) */
  {
    if (prev == NULL)
      md->head = e;
    else
      prev->next = e;

    e->next = this->next;
  }

  pthread_mutex_unlock(&md->lock);

  if (this != NULL) {
    this->next = NULL;
    md_entry_free(this);
  }

  return (0);
} /* }}} int md_entry_insert */

/* XXX: The lock on md must be held while calling this function! */
static int md_entry_insert_clone(meta_data_t *md, meta_entry_t *orig) /* {{{ */
{
  meta_entry_t *e;
  meta_entry_t *this;
  meta_entry_t *prev;

  /* WARNINGS :
   *  - we do not check that md and e != NULL here. You should have done it
   * before.
   *  - we do not use the lock. You should have set it before.
   */

  e = md_entry_clone_contents(orig);

  prev = NULL;
  this = md->head;
  while (this != NULL) {
    if (strcasecmp(e->key, this->key) == 0)
      break;

    prev = this;
    this = this->next;
  }

  if (this == NULL) {
    /* This key does not exist yet. */
    if (md->head == NULL)
      md->head = e;
    else {
      assert(prev != NULL);
      prev->next = e;
    }

    e->next = NULL;
  } else /* (this != NULL) */
  {
    if (prev == NULL)
      md->head = e;
    else
      prev->next = e;

    e->next = this->next;
  }

  if (this != NULL) {
    this->next = NULL;
    md_entry_free(this);
  }

  return (0);
} /* }}} int md_entry_insert_clone */

/* XXX: The lock on md must be held while calling this function! */
static meta_entry_t *md_entry_lookup(meta_data_t *md, /* {{{ */
                                     const char *key) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (NULL);

  for (e = md->head; e != NULL; e = e->next)
    if (strcasecmp(key, e->key) == 0)
      break;

  return (e);
} /* }}} meta_entry_t *md_entry_lookup */

/*
 * Each value_list_t*, as it is going through the system, is handled by exactly
 * one thread. Plugins which pass a value_list_t* to another thread, e.g. the
 * rrdtool plugin, must create a copy first. The meta data within a
 * value_list_t* is not thread safe and doesn't need to be.
 *
 * The meta data associated with cache entries are a different story. There, we
 * need to ensure exclusive locking to prevent leaks and other funky business.
 * This is ensured by the uc_meta_data_get_*() functions.
 */

/*
 * Public functions
 */
meta_data_t *meta_data_create(void) /* {{{ */
{
  meta_data_t *md;

  md = calloc(1, sizeof(*md));
  if (md == NULL) {
    ERROR("meta_data_create: calloc failed.");
    return (NULL);
  }

  pthread_mutex_init(&md->lock, /* attr = */ NULL);

  return (md);
} /* }}} meta_data_t *meta_data_create */

meta_data_t *meta_data_clone(meta_data_t *orig) /* {{{ */
{
  meta_data_t *copy;

  if (orig == NULL)
    return (NULL);

  copy = meta_data_create();
  if (copy == NULL)
    return (NULL);

  pthread_mutex_lock(&orig->lock);
  copy->head = md_entry_clone(orig->head);
  pthread_mutex_unlock(&orig->lock);

  return (copy);
} /* }}} meta_data_t *meta_data_clone */

int meta_data_clone_merge(meta_data_t **dest, meta_data_t *orig) /* {{{ */
{
  if (orig == NULL)
    return (0);

  if (*dest == NULL) {
    *dest = meta_data_clone(orig);
    return (0);
  }

  pthread_mutex_lock(&orig->lock);
  for (meta_entry_t *e = orig->head; e != NULL; e = e->next) {
    md_entry_insert_clone((*dest), e);
  }
  pthread_mutex_unlock(&orig->lock);

  return (0);
} /* }}} int meta_data_clone_merge */

void meta_data_destroy(meta_data_t *md) /* {{{ */
{
  if (md == NULL)
    return;

  md_entry_free(md->head);
  pthread_mutex_destroy(&md->lock);
  free(md);
} /* }}} void meta_data_destroy */

int meta_data_exists(meta_data_t *md, const char *key) /* {{{ */
{
  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  for (meta_entry_t *e = md->head; e != NULL; e = e->next) {
    if (strcasecmp(key, e->key) == 0) {
      pthread_mutex_unlock(&md->lock);
      return (1);
    }
  }

  pthread_mutex_unlock(&md->lock);
  return (0);
} /* }}} int meta_data_exists */

int meta_data_type(meta_data_t *md, const char *key) /* {{{ */
{
  if ((md == NULL) || (key == NULL))
    return -EINVAL;

  pthread_mutex_lock(&md->lock);

  for (meta_entry_t *e = md->head; e != NULL; e = e->next) {
    if (strcasecmp(key, e->key) == 0) {
      pthread_mutex_unlock(&md->lock);
      return e->type;
    }
  }

  pthread_mutex_unlock(&md->lock);
  return 0;
} /* }}} int meta_data_type */

int meta_data_toc(meta_data_t *md, char ***toc) /* {{{ */
{
  int i = 0, count = 0;

  if ((md == NULL) || (toc == NULL))
    return -EINVAL;

  pthread_mutex_lock(&md->lock);

  for (meta_entry_t *e = md->head; e != NULL; e = e->next)
    ++count;

  if (count == 0) {
    pthread_mutex_unlock(&md->lock);
    return (count);
  }

  *toc = calloc(count, sizeof(**toc));
  for (meta_entry_t *e = md->head; e != NULL; e = e->next)
    (*toc)[i++] = strdup(e->key);

  pthread_mutex_unlock(&md->lock);
  return count;
} /* }}} int meta_data_toc */

int meta_data_delete(meta_data_t *md, const char *key) /* {{{ */
{
  meta_entry_t *this;
  meta_entry_t *prev;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  prev = NULL;
  this = md->head;
  while (this != NULL) {
    if (strcasecmp(key, this->key) == 0)
      break;

    prev = this;
    this = this->next;
  }

  if (this == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  if (prev == NULL)
    md->head = this->next;
  else
    prev->next = this->next;

  pthread_mutex_unlock(&md->lock);

  this->next = NULL;
  md_entry_free(this);

  return (0);
} /* }}} int meta_data_delete */

/*
 * Add functions
 */
int meta_data_add_string(meta_data_t *md, /* {{{ */
                         const char *key, const char *value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  e = md_entry_alloc(key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_string = md_strdup(value);
  if (e->value.mv_string == NULL) {
    ERROR("meta_data_add_string: md_strdup failed.");
    md_entry_free(e);
    return (-ENOMEM);
  }
  e->type = MD_TYPE_STRING;

  return (md_entry_insert(md, e));
} /* }}} int meta_data_add_string */

int meta_data_add_signed_int(meta_data_t *md, /* {{{ */
                             const char *key, int64_t value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc(key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_signed_int = value;
  e->type = MD_TYPE_SIGNED_INT;

  return (md_entry_insert(md, e));
} /* }}} int meta_data_add_signed_int */

int meta_data_add_unsigned_int(meta_data_t *md, /* {{{ */
                               const char *key, uint64_t value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc(key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_unsigned_int = value;
  e->type = MD_TYPE_UNSIGNED_INT;

  return (md_entry_insert(md, e));
} /* }}} int meta_data_add_unsigned_int */

int meta_data_add_double(meta_data_t *md, /* {{{ */
                         const char *key, double value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc(key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_double = value;
  e->type = MD_TYPE_DOUBLE;

  return (md_entry_insert(md, e));
} /* }}} int meta_data_add_double */

int meta_data_add_boolean(meta_data_t *md, /* {{{ */
                          const char *key, _Bool value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc(key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_boolean = value;
  e->type = MD_TYPE_BOOLEAN;

  return (md_entry_insert(md, e));
} /* }}} int meta_data_add_boolean */

/*
 * Get functions
 */
int meta_data_get_string(meta_data_t *md, /* {{{ */
                         const char *key, char **value) {
  meta_entry_t *e;
  char *temp;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  e = md_entry_lookup(md, key);
  if (e == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_STRING) {
    ERROR("meta_data_get_string: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  temp = md_strdup(e->value.mv_string);
  if (temp == NULL) {
    pthread_mutex_unlock(&md->lock);
    ERROR("meta_data_get_string: md_strdup failed.");
    return (-ENOMEM);
  }

  pthread_mutex_unlock(&md->lock);

  *value = temp;

  return (0);
} /* }}} int meta_data_get_string */

int meta_data_get_signed_int(meta_data_t *md, /* {{{ */
                             const char *key, int64_t *value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  e = md_entry_lookup(md, key);
  if (e == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_SIGNED_INT) {
    ERROR("meta_data_get_signed_int: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_signed_int;

  pthread_mutex_unlock(&md->lock);
  return (0);
} /* }}} int meta_data_get_signed_int */

int meta_data_get_unsigned_int(meta_data_t *md, /* {{{ */
                               const char *key, uint64_t *value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  e = md_entry_lookup(md, key);
  if (e == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_UNSIGNED_INT) {
    ERROR("meta_data_get_unsigned_int: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_unsigned_int;

  pthread_mutex_unlock(&md->lock);
  return (0);
} /* }}} int meta_data_get_unsigned_int */

int meta_data_get_double(meta_data_t *md, /* {{{ */
                         const char *key, double *value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  e = md_entry_lookup(md, key);
  if (e == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_DOUBLE) {
    ERROR("meta_data_get_double: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_double;

  pthread_mutex_unlock(&md->lock);
  return (0);
} /* }}} int meta_data_get_double */

int meta_data_get_boolean(meta_data_t *md, /* {{{ */
                          const char *key, _Bool *value) {
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  e = md_entry_lookup(md, key);
  if (e == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_BOOLEAN) {
    ERROR("meta_data_get_boolean: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_boolean;

  pthread_mutex_unlock(&md->lock);
  return (0);
} /* }}} int meta_data_get_boolean */

int meta_data_as_string(meta_data_t *md, /* {{{ */
                        const char *key, char **value) {
  meta_entry_t *e;
  char *actual;
  char buffer[MD_MAX_NONSTRING_CHARS]; /* For non-string types. */
  char *temp;
  int type;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock(&md->lock);

  e = md_entry_lookup(md, key);
  if (e == NULL) {
    pthread_mutex_unlock(&md->lock);
    return (-ENOENT);
  }

  type = e->type;

  switch (type) {
  case MD_TYPE_STRING:
    actual = e->value.mv_string;
    break;
  case MD_TYPE_SIGNED_INT:
    ssnprintf(buffer, sizeof(buffer), "%" PRIi64, e->value.mv_signed_int);
    actual = buffer;
    break;
  case MD_TYPE_UNSIGNED_INT:
    ssnprintf(buffer, sizeof(buffer), "%" PRIu64, e->value.mv_unsigned_int);
    actual = buffer;
    break;
  case MD_TYPE_DOUBLE:
    ssnprintf(buffer, sizeof(buffer), GAUGE_FORMAT, e->value.mv_double);
    actual = buffer;
    break;
  case MD_TYPE_BOOLEAN:
    actual = e->value.mv_boolean ? "true" : "false";
    break;
  default:
    pthread_mutex_unlock(&md->lock);
    ERROR("meta_data_as_string: unknown type %d for key `%s'", type, key);
    return (-ENOENT);
  }

  pthread_mutex_unlock(&md->lock);

  temp = md_strdup(actual);
  if (temp == NULL) {
    pthread_mutex_unlock(&md->lock);
    ERROR("meta_data_as_string: md_strdup failed for key `%s'.", key);
    return (-ENOMEM);
  }

  *value = temp;

  return (0);
} /* }}} int meta_data_as_string */

/* vim: set sw=2 sts=2 et fdm=marker : */
