/**
 * collectd - src/meta_data.c
 * Copyright (C) 2008-2011  Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "plugin.h"
#include "meta_data.h"

#include <pthread.h>

/*
 * Data types
 */
union meta_value_u
{
  char    *mv_string;
  int64_t  mv_signed_int;
  uint64_t mv_unsigned_int;
  double   mv_double;
  _Bool    mv_boolean;
};
typedef union meta_value_u meta_value_t;

struct meta_entry_s;
typedef struct meta_entry_s meta_entry_t;
struct meta_entry_s
{
  char         *key;
  meta_value_t  value;
  int           type;
  meta_entry_t *next;
};

struct meta_data_s
{
  meta_entry_t   *head;
  pthread_mutex_t lock;
};

/*
 * Private functions
 */
static char *md_strdup (const char *orig) /* {{{ */
{
  size_t sz;
  char *dest;

  if (orig == NULL)
    return (NULL);

  sz = strlen (orig) + 1;
  dest = (char *) malloc (sz);
  if (dest == NULL)
    return (NULL);

  memcpy (dest, orig, sz);

  return (dest);
} /* }}} char *md_strdup */

static meta_entry_t *md_entry_alloc (const char *key) /* {{{ */
{
  meta_entry_t *e;

  e = (meta_entry_t *) malloc (sizeof (*e));
  if (e == NULL)
  {
    ERROR ("md_entry_alloc: malloc failed.");
    return (NULL);
  }
  memset (e, 0, sizeof (*e));

  e->key = md_strdup (key);
  if (e->key == NULL)
  {
    free (e);
    ERROR ("md_entry_alloc: md_strdup failed.");
    return (NULL);
  }

  e->type = 0;
  e->next = NULL;

  return (e);
} /* }}} meta_entry_t *md_entry_alloc */

static meta_entry_t *md_entry_clone (const meta_entry_t *orig) /* {{{ */
{
  meta_entry_t *copy;

  if (orig == NULL)
    return (NULL);

  copy = md_entry_alloc (orig->key);
  copy->type = orig->type;
  if (copy->type == MD_TYPE_STRING)
    copy->value.mv_string = strdup (orig->value.mv_string);
  else
    copy->value = orig->value;

  copy->next = md_entry_clone (orig->next);
  return (copy);
} /* }}} meta_entry_t *md_entry_clone */

static void md_entry_free (meta_entry_t *e) /* {{{ */
{
  if (e == NULL)
    return;

  free (e->key);

  if (e->type == MD_TYPE_STRING)
    free (e->value.mv_string);

  if (e->next != NULL)
    md_entry_free (e->next);

  free (e);
} /* }}} void md_entry_free */

static int md_entry_insert (meta_data_t *md, meta_entry_t *e) /* {{{ */
{
  meta_entry_t *this;
  meta_entry_t *prev;

  if ((md == NULL) || (e == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  prev = NULL;
  this = md->head;
  while (this != NULL)
  {
    if (strcasecmp (e->key, this->key) == 0)
      break;

    prev = this;
    this = this->next;
  }

  if (this == NULL)
  {
    /* This key does not exist yet. */
    if (md->head == NULL)
      md->head = e;
    else
    {
      assert (prev != NULL);
      prev->next = e;
    }

    e->next = NULL;
  }
  else /* (this != NULL) */
  {
    if (prev == NULL)
      md->head = e;
    else
      prev->next = e;

    e->next = this->next;
  }

  pthread_mutex_unlock (&md->lock);

  if (this != NULL)
  {
    this->next = NULL;
    md_entry_free (this);
  }

  return (0);
} /* }}} int md_entry_insert */

/* XXX: The lock on md must be held while calling this function! */
static meta_entry_t *md_entry_lookup (meta_data_t *md, /* {{{ */
    const char *key)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (NULL);

  for (e = md->head; e != NULL; e = e->next)
    if (strcasecmp (key, e->key) == 0)
      break;

  return (e);
} /* }}} meta_entry_t *md_entry_lookup */

/*
 * Public functions
 */
meta_data_t *meta_data_create (void) /* {{{ */
{
  meta_data_t *md;

  md = (meta_data_t *) malloc (sizeof (*md));
  if (md == NULL)
  {
    ERROR ("meta_data_create: malloc failed.");
    return (NULL);
  }
  memset (md, 0, sizeof (*md));

  md->head = NULL;
  pthread_mutex_init (&md->lock, /* attr = */ NULL);

  return (md);
} /* }}} meta_data_t *meta_data_create */

meta_data_t *meta_data_clone (meta_data_t *orig) /* {{{ */
{
  meta_data_t *copy;

  if (orig == NULL)
    return (NULL);

  copy = meta_data_create ();
  if (copy == NULL)
    return (NULL);

  pthread_mutex_lock (&orig->lock);
  copy->head = md_entry_clone (orig->head);
  pthread_mutex_unlock (&orig->lock);

  return (copy);
} /* }}} meta_data_t *meta_data_clone */

void meta_data_destroy (meta_data_t *md) /* {{{ */
{
  if (md == NULL)
    return;

  md_entry_free (md->head);
  pthread_mutex_destroy (&md->lock);
  free (md);
} /* }}} void meta_data_destroy */

int meta_data_exists (meta_data_t *md, const char *key) /* {{{ */
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  for (e = md->head; e != NULL; e = e->next)
  {
    if (strcasecmp (key, e->key) == 0)
    {
      pthread_mutex_unlock (&md->lock);
      return (1);
    }
  }

  pthread_mutex_unlock (&md->lock);
  return (0);
} /* }}} int meta_data_exists */

int meta_data_type (meta_data_t *md, const char *key) /* {{{ */
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return -EINVAL;

  pthread_mutex_lock (&md->lock);

  for (e = md->head; e != NULL; e = e->next)
  {
    if (strcasecmp (key, e->key) == 0)
    {
      pthread_mutex_unlock (&md->lock);
      return e->type;
    }
  }

  pthread_mutex_unlock (&md->lock);
  return 0;
} /* }}} int meta_data_type */

int meta_data_toc (meta_data_t *md, char ***toc) /* {{{ */
{
  int i = 0, count = 0;
  meta_entry_t *e;

  if ((md == NULL) || (toc == NULL))
    return -EINVAL;

  pthread_mutex_lock (&md->lock);

  for (e = md->head; e != NULL; e = e->next)
    ++count;    

  *toc = malloc(count * sizeof(**toc));
  for (e = md->head; e != NULL; e = e->next)
    (*toc)[i++] = strdup(e->key);
  
  pthread_mutex_unlock (&md->lock);
  return count;
} /* }}} int meta_data_toc */

int meta_data_delete (meta_data_t *md, const char *key) /* {{{ */
{
  meta_entry_t *this;
  meta_entry_t *prev;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  prev = NULL;
  this = md->head;
  while (this != NULL)
  {
    if (strcasecmp (key, this->key) == 0)
      break;

    prev = this;
    this = this->next;
  }

  if (this == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  if (prev == NULL)
    md->head = this->next;
  else
    prev->next = this->next;

  pthread_mutex_unlock (&md->lock);

  this->next = NULL;
  md_entry_free (this);

  return (0);
} /* }}} int meta_data_delete */

/*
 * Add functions
 */
int meta_data_add_string (meta_data_t *md, /* {{{ */
    const char *key, const char *value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  e = md_entry_alloc (key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_string = md_strdup (value);
  if (e->value.mv_string == NULL)
  {
    ERROR ("meta_data_add_string: md_strdup failed.");
    md_entry_free (e);
    return (-ENOMEM);
  }
  e->type = MD_TYPE_STRING;

  return (md_entry_insert (md, e));
} /* }}} int meta_data_add_string */

int meta_data_add_signed_int (meta_data_t *md, /* {{{ */
    const char *key, int64_t value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc (key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_signed_int = value;
  e->type = MD_TYPE_SIGNED_INT;

  return (md_entry_insert (md, e));
} /* }}} int meta_data_add_signed_int */

int meta_data_add_unsigned_int (meta_data_t *md, /* {{{ */
    const char *key, uint64_t value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc (key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_unsigned_int = value;
  e->type = MD_TYPE_UNSIGNED_INT;

  return (md_entry_insert (md, e));
} /* }}} int meta_data_add_unsigned_int */

int meta_data_add_double (meta_data_t *md, /* {{{ */
    const char *key, double value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc (key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_double = value;
  e->type = MD_TYPE_DOUBLE;

  return (md_entry_insert (md, e));
} /* }}} int meta_data_add_double */

int meta_data_add_boolean (meta_data_t *md, /* {{{ */
    const char *key, _Bool value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL))
    return (-EINVAL);

  e = md_entry_alloc (key);
  if (e == NULL)
    return (-ENOMEM);

  e->value.mv_boolean = value;
  e->type = MD_TYPE_BOOLEAN;

  return (md_entry_insert (md, e));
} /* }}} int meta_data_add_boolean */

/*
 * Get functions
 */
int meta_data_get_string (meta_data_t *md, /* {{{ */
    const char *key, char **value)
{
  meta_entry_t *e;
  char *temp;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  e = md_entry_lookup (md, key);
  if (e == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_STRING)
  {
    ERROR ("meta_data_get_signed_int: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  temp = md_strdup (e->value.mv_string);
  if (temp == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    ERROR ("meta_data_get_string: md_strdup failed.");
    return (-ENOMEM);
  }
 
  pthread_mutex_unlock (&md->lock);

  *value = temp;

  return (0);
} /* }}} int meta_data_get_string */

int meta_data_get_signed_int (meta_data_t *md, /* {{{ */
    const char *key, int64_t *value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  e = md_entry_lookup (md, key);
  if (e == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_SIGNED_INT)
  {
    ERROR ("meta_data_get_signed_int: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_signed_int;

  pthread_mutex_unlock (&md->lock);
  return (0);
} /* }}} int meta_data_get_signed_int */

int meta_data_get_unsigned_int (meta_data_t *md, /* {{{ */
    const char *key, uint64_t *value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  e = md_entry_lookup (md, key);
  if (e == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_UNSIGNED_INT)
  {
    ERROR ("meta_data_get_unsigned_int: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_unsigned_int;

  pthread_mutex_unlock (&md->lock);
  return (0);
} /* }}} int meta_data_get_unsigned_int */

int meta_data_get_double (meta_data_t *md, /* {{{ */
    const char *key, double *value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  e = md_entry_lookup (md, key);
  if (e == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_DOUBLE)
  {
    ERROR ("meta_data_get_double: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_double;

  pthread_mutex_unlock (&md->lock);
  return (0);
} /* }}} int meta_data_get_double */

int meta_data_get_boolean (meta_data_t *md, /* {{{ */
    const char *key, _Bool *value)
{
  meta_entry_t *e;

  if ((md == NULL) || (key == NULL) || (value == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&md->lock);

  e = md_entry_lookup (md, key);
  if (e == NULL)
  {
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  if (e->type != MD_TYPE_BOOLEAN)
  {
    ERROR ("meta_data_get_boolean: Type mismatch for key `%s'", e->key);
    pthread_mutex_unlock (&md->lock);
    return (-ENOENT);
  }

  *value = e->value.mv_boolean;

  pthread_mutex_unlock (&md->lock);
  return (0);
} /* }}} int meta_data_get_boolean */

/* vim: set sw=2 sts=2 et fdm=marker : */
