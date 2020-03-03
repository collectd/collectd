/**
 * collectd - src/curl_jolokia.c
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2006-2013  Florian octo Forster
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
 *   Doug MacEachern <dougm at hyperic.com>
 *   Florian octo Forster <octo at collectd.org>
 *   Wilfried dothebart Goesgens <dothebart at citadel.org>
 **/

#include "collectd.h"

#include "utils/common/common.h"
#include "configfile.h"
#include "plugin.h"
#include "utils_complain.h"
#include "utils_llist.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <curl/curl.h>

#include <yajl/yajl_parse.h>

#define CJO_DEFAULT_HOST "localhost"

struct cjo_attribute_s /* {{{ */
{
  char *attribute_name;
  char *attribute_match;
  size_t attribute_match_len;
  char *type;
};
typedef struct cjo_attribute_s cjo_attribute_t;
/* }}} */

struct cjo_bean_s /* {{{ */
{
  char *bean_name;
  char *mbean_match;
  char *mbean_namespace;
  size_t mbean_match_len;
  llist_t *attributes; /* list of cjo_attribute_t */
};
typedef struct cjo_bean_s cjo_bean_t;
/* }}} */

typedef enum { eNone = 0, eValue, eMBean } cjo_expect_token;

/* {{{ */
struct cjo_attribute_values_s {
  const char *json_value;
  size_t json_value_len;
  const char *json_name;
  size_t json_name_len;
};
typedef struct cjo_attribute_values_s cjo_attribute_values_t;
/* }}} */

struct cjo_membuffer_s {
  char *buffer;
  size_t size;
  size_t used;
};

typedef struct cjo_membuffer_s cjo_membuffer_t;

struct cjo_s /* {{{ */
{
  char *instance;
  char *host;

  char *sock;

  char *url;
  char *user;
  char *pass;
  char *credentials;

  const cjo_bean_t *match_this_bean;
  cjo_attribute_values_t *curr_attribute_value; /* points to the pool below. */
  cjo_attribute_values_t *attributepool;
  int attribute_pool_used;

  const char *json_key;
  size_t json_key_len;

  cjo_expect_token expect;

  _Bool verify_peer;
  _Bool verify_host;
  char *cacert;
  struct curl_slist *headers;
  char *post_body;
  size_t post_body_len;
  _Bool expect_escapes;
  cdtime_t interval;
  int timeout;

  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  const unsigned char *start, *end;
  cjo_membuffer_t replybuffer;
  cjo_membuffer_t itembuffer;

  size_t poolsize;
  char *pool;
  const char *poolmax;
  char *pool_ptr;

  yajl_handle yajl;
  llist_t *bean_configs;
  int max_attribute_count;
  int max_value_len;
};
typedef struct cjo_s cjo_t; /* }}} */

static void cjo_init_buffer(cjo_membuffer_t *buf, size_t initial_size) {
  buf->buffer = calloc(1, initial_size);
  buf->used = 0;
  buf->size = initial_size;
}

static void cjo_append_buffer(cjo_membuffer_t *buf, const char *append,
                              size_t appendlen) {
  if (buf->used + appendlen + 1 > buf->size) {
    size_t newsize = buf->size * 2;
    char *newbuf = calloc(1, newsize);
    memcpy(newbuf, buf->buffer, buf->used);
    free(buf->buffer);
    buf->buffer = newbuf;
    buf->size = newsize;
  }
  memcpy(buf->buffer + buf->used, append, appendlen);
  buf->used += appendlen;
}

static void cjo_release_buffercontent(cjo_membuffer_t *buf) {
  sfree(buf->buffer);
  buf->size = 0;
  buf->used = 0;
}

static void cjo_remember_value(cjo_membuffer_t *should_be_in_here,
                               cjo_membuffer_t *buffer_here_if_not,
                               const char *ptr, size_t len,
                               const char **SetThisOne, size_t *SetThisLen) {
  if (!((ptr > should_be_in_here->buffer) &&
        (ptr < should_be_in_here->buffer + should_be_in_here->used))) {
    char *tptr = buffer_here_if_not->buffer + buffer_here_if_not->used;
    cjo_append_buffer(buffer_here_if_not, ptr, len);
    ptr = tptr;
  }

  *SetThisOne = ptr;
  *SetThisLen = len;
}

/*
 * llist lookup methods
 */
static int cjo_cfg_compare_attribute(llentry_t *match, void *vdb) {
  cjo_t *db = (cjo_t *)vdb;
  cjo_attribute_t *pcfg = (cjo_attribute_t *)match->value;

  if (pcfg->attribute_match_len != db->curr_attribute_value->json_name_len)
    return -1;
  else
    return strncmp(pcfg->attribute_match, db->curr_attribute_value->json_name,
                   db->curr_attribute_value->json_name_len);
}
static int cjo_cfg_compare_bean(llentry_t *match, void *vdb) {
  cjo_t *db = (cjo_t *)vdb;
  cjo_bean_t *pbeancfg = (cjo_bean_t *)match->value;

  if (pbeancfg->mbean_match_len != db->json_key_len)
    return -1;
  return strncmp(pbeancfg->mbean_match, db->json_key, db->json_key_len);
}

/*
 * Aligning data with Configs & Sending it
 */

static int cjo_get_type(cjo_attribute_t *attribute) {
  if (attribute->type != NULL) {
    return -EINVAL;
  }

  const data_set_t *ds = plugin_get_ds(attribute->type);
  if (ds == NULL) {
    static char type[DATA_MAX_NAME_LEN] = "/--invalid--/";

    if (strcmp(type, attribute->type) != 0) {
      ERROR("curl_jolokia plugin: Unable to look up DS type \"%s\".",
            attribute->type);
      sstrncpy(type, attribute->type, sizeof(type));
    }

    return -1;
  } else if (ds->ds_num > 1) {
    static c_complain_t complaint = C_COMPLAIN_INIT_STATIC;

    c_complain_once(
        LOG_WARNING, &complaint,
        "curl_jolokia plugin: The type \"%s\" has more than one data source. "
        "This is currently not supported. I will return the type of the "
        "first data source, but this will likely lead to problems later on.",
        attribute->type);
  }

  return ds->ds[0].type;
}

static void cjo_submit(cjo_t *db) /* {{{ */
{
  if ((db->match_this_bean == NULL) || (db->attribute_pool_used == 0))
    return; /* nothing to do yet. */

  for (int i = 0; i < db->attribute_pool_used; i++) {
    value_list_t vl = VALUE_LIST_INIT;
    cjo_attribute_values_t *CAV = db->curr_attribute_value =
        &db->attributepool[i];
    llentry_t *cfgptr = llist_search_custom(db->match_this_bean->attributes,
                                            cjo_cfg_compare_attribute, db);
    if (cfgptr == NULL) {
      char attribute[CAV->json_name_len + 1];

      memcpy(attribute, CAV->json_name, CAV->json_name_len);
      attribute[CAV->json_name_len] = '\0';
      ERROR("curl_jolokia plugin: failed to locate attribute [%s:\"%s\"]",
            db->match_this_bean->bean_name, attribute);

      continue; /* we don't know this! */
    }
    cjo_attribute_t *curr_attribute = cfgptr->value;

    /* Create a null-terminated version of the string. */
    int ds_type = cjo_get_type(curr_attribute);
    if (ds_type < 0) {
      char attribute[CAV->json_name_len + 1];

      memcpy(attribute, CAV->json_name, CAV->json_name_len);
      attribute[CAV->json_name_len] = '\0';

      ERROR("curl_jolokia plugin: failed to map type for [%s:%s:%s]",
            db->match_this_bean->bean_name, attribute, curr_attribute->type);
      continue;
    }

    value_t ret_value = {0};

    char buffer[db->max_value_len + 1];
    memcpy(buffer, CAV->json_value, CAV->json_value_len);

    buffer[CAV->json_value_len] = '\0';

    if (parse_value(buffer, &ret_value, ds_type) != 0) {
      char attribute[CAV->json_name_len + 1];

      memcpy(attribute, CAV->json_name, CAV->json_name_len);
      attribute[CAV->json_name_len] = '\0';

      WARNING("curl_jolokia plugin: Unable to parse number: [%s:%s:\"%s\"]",
              db->match_this_bean->bean_name, attribute, buffer);
      continue;
    }

    vl.values = &ret_value;
    vl.values_len = 1;

    char *host;
    if ((db->host == NULL) || (strcmp("", db->host) == 0) ||
        (strcmp(CJO_DEFAULT_HOST, db->host) == 0))
      host = "";
    else
      host = db->host;

    sstrncpy(vl.type_instance, curr_attribute->attribute_name,
             sizeof(vl.type_instance));
    sstrncpy(vl.plugin_instance, db->match_this_bean->bean_name,
             sizeof(vl.type_instance));

    sstrncpy(vl.host, host, sizeof(vl.host));
    if (db->match_this_bean->mbean_namespace == NULL)
      sstrncpy(vl.plugin, "jolokia", sizeof(vl.plugin));
    else
      sstrncpy(vl.plugin, db->match_this_bean->mbean_namespace,
               sizeof(vl.plugin));
    sstrncpy(vl.type, curr_attribute->type, sizeof(vl.type));

    plugin_dispatch_values(&vl);
  }

  /* flush values */
  db->match_this_bean = NULL;
  db->attribute_pool_used = 0;

  memset(db->attributepool, 0,
         sizeof(*db->attributepool) * db->max_attribute_count);

} /* }}} int cjo_submit */

/* yajl callbacks */
#define CJO_CB_ABORT 0
#define CJO_CB_CONTINUE 1

static int cjo_cb_string(void *ctx, const unsigned char *val, size_t len) {
  cjo_t *db = (cjo_t *)ctx;

  switch (db->expect) {
  case eValue:
    cjo_remember_value(&db->replybuffer, &db->itembuffer, (const char *)val,
                       len, &db->curr_attribute_value->json_value,
                       &db->curr_attribute_value->json_value_len);

    if (len > db->max_value_len)
      db->max_value_len = len;

    break;
  case eMBean:
    db->json_key = (const char *)val;
    db->json_key_len = len;

    llentry_t *cfgptr =
        llist_search_custom(db->bean_configs, cjo_cfg_compare_bean, db);
    if (cfgptr != NULL) {
      db->match_this_bean = cfgptr->value;
      cjo_submit(db);
    }
    db->expect = eNone;
    break;
  case eNone:
    break; /* Don't care... */
  }

  /* Handle the string as if it was a number. */
  return CJO_CB_CONTINUE;
} /* int cjo_cb_string */

static int cjo_cb_number(void *ctx, const char *number, size_t number_len) {
  cjo_t *db = (cjo_t *)ctx;

  switch (db->expect) {
  case eValue:
    cjo_remember_value(&db->replybuffer, &db->itembuffer, number, number_len,
                       &db->curr_attribute_value->json_value,
                       &db->curr_attribute_value->json_value_len);

    if (number_len > db->max_value_len)
      db->max_value_len = number_len;
    break;
  case eMBean:
  case eNone:
    db->expect = eNone;
    break; /* Don't care... */
  }

  /* are we complete yet? */

  return CJO_CB_CONTINUE;
} /* int cjo_cb_number */

static int cjo_cb_map_key(void *ctx, unsigned char const *in_name,
                          size_t in_name_len) {
  cjo_t *db = (cjo_t *)ctx;

  if ((in_name_len == 5) && !strncmp((const char *)in_name, "value", 5)) {
    db->expect = eValue;
  } else if ((in_name_len == 5) &&
             !strncmp((const char *)in_name, "mbean", 5)) {
    db->expect = eMBean;
  } else if (db->expect == eValue) {
    if (db->attribute_pool_used <= db->max_attribute_count) {
      db->curr_attribute_value = &db->attributepool[db->attribute_pool_used];
      db->attribute_pool_used++;
    } else {
      char buffer[in_name_len + 1];

      memcpy(buffer, in_name, in_name_len);
      buffer[in_name_len] = '\0';
      ERROR("curl_jolokia plugin: attribute pool[%d/%d] [%s] exhausted! We may "
            "loose values!",
            db->attribute_pool_used, db->max_attribute_count, buffer);
    }
    cjo_remember_value(&db->replybuffer, &db->itembuffer, (const char *)in_name,
                       in_name_len, &db->curr_attribute_value->json_name,
                       &db->curr_attribute_value->json_name_len);
  }
  return CJO_CB_CONTINUE;
}

static int cjo_cb_end_map(void *ctx) {
  cjo_t *db = (cjo_t *)ctx;

  if (db->expect == eValue) {
    cjo_submit(db);
    db->expect = eNone;
  }
  return CJO_CB_CONTINUE;
}

static yajl_callbacks ycallbacks = {
    NULL, /* null */
    NULL, /* boolean */
    NULL, /* integer */
    NULL, /* double */
    cjo_cb_number,  cjo_cb_string, NULL, cjo_cb_map_key,
    cjo_cb_end_map, NULL,          NULL};

/*
 *------------------------------------------------------------------------------
 * cURL Handling
 */
static size_t cjo_curl_callback(void *buf, /* {{{ */
                                size_t size, size_t nmemb, void *user_data) {
  size_t len = size * nmemb;

  if (len <= 0)
    return len;

  cjo_t *db = user_data;
  if (db == NULL)
    return 0;

  cjo_append_buffer(&db->replybuffer, (char *)buf, len);

  return len;
} /* }}} size_t cjo_curl_callback */

static int cjo_init_curl(cjo_t *db) /* {{{ */
{
  db->curl = curl_easy_init();
  if (db->curl == NULL) {
    ERROR("curl_jolokia plugin: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(db->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(db->curl, CURLOPT_WRITEFUNCTION, cjo_curl_callback);
  curl_easy_setopt(db->curl, CURLOPT_WRITEDATA, db);
  curl_easy_setopt(db->curl, CURLOPT_USERAGENT,
                   PACKAGE_NAME "/" PACKAGE_VERSION);
  curl_easy_setopt(db->curl, CURLOPT_ERRORBUFFER, db->curl_errbuf);
  curl_easy_setopt(db->curl, CURLOPT_URL, db->url);

  if (db->user != NULL) {
    size_t credentials_size;

    credentials_size = strlen(db->user) + 2;
    if (db->pass != NULL)
      credentials_size += strlen(db->pass);

    db->credentials = (char *)calloc(1, credentials_size);
    if (db->credentials == NULL) {
      ERROR("curl_jolokia plugin: calloc failed.");
      return -1;
    }

    snprintf(db->credentials, credentials_size, "%s:%s", db->user,
             (db->pass == NULL) ? "" : db->pass);
    curl_easy_setopt(db->curl, CURLOPT_USERPWD, db->credentials);
  }

  curl_easy_setopt(db->curl, CURLOPT_SSL_VERIFYPEER, (long)db->verify_peer);
  curl_easy_setopt(db->curl, CURLOPT_SSL_VERIFYHOST, db->verify_host ? 2L : 0L);
  if (db->cacert != NULL)
    curl_easy_setopt(db->curl, CURLOPT_CAINFO, db->cacert);
  if (db->headers != NULL)
    curl_easy_setopt(db->curl, CURLOPT_HTTPHEADER, db->headers);
  if (db->post_body != NULL)
    curl_easy_setopt(db->curl, CURLOPT_POSTFIELDS, db->post_body);

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (db->timeout >= 0)
    curl_easy_setopt(db->curl, CURLOPT_TIMEOUT_MS, (long)db->timeout);
  else if (db->interval > 0)
    curl_easy_setopt(db->curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(db->interval));
  else
    curl_easy_setopt(db->curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(plugin_get_interval()));
#endif
  return 0;
} /* }}} int cjo_init_curl */

static int cjo_sock_perform(cjo_t *db) /* {{{ */
{
  char errbuf[1024];
  struct sockaddr_un sa_unix = {
      .sun_family = AF_UNIX,
  };
  sstrncpy(sa_unix.sun_path, db->sock, sizeof(sa_unix.sun_path));

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  if (connect(fd, (struct sockaddr *)&sa_unix, sizeof(sa_unix)) < 0) {
    ERROR("curl_jolokia plugin: connect(%s) failed: %s",
          (db->sock != NULL) ? db->sock : "<null>",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    close(fd);
    return -1;
  }

  ssize_t red;
  do {
    unsigned char buffer[4096];
    red = read(fd, buffer, sizeof(buffer));
    if (red < 0) {
      ERROR("curl_jolokia plugin: read(%s) failed: %s",
            (db->sock != NULL) ? db->sock : "<null>",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      close(fd);
      return -1;
    }
    if (!cjo_curl_callback(buffer, red, 1, db))
      break;
  } while (red > 0);
  close(fd);
  return 0;
} /* }}} int cjo_sock_perform */

static int cjo_curl_perform(cjo_t *db) /* {{{ */
{

  char *url = NULL;

  curl_easy_getinfo(db->curl, CURLINFO_EFFECTIVE_URL, &url);

  size_t len = db->post_body_len;
  if (len < 4096)
    len = 4096;

  cjo_init_buffer(&db->replybuffer, len * 4);
  cjo_init_buffer(&db->itembuffer, len);

  int status = curl_easy_perform(db->curl);
  if (status != CURLE_OK) {
    ERROR(
        "curl_jolokia plugin: curl_easy_perform failed with status %i: %s (%s)",
        status, db->curl_errbuf, (url != NULL) ? url : "<null>");
    cjo_release_buffercontent(&db->replybuffer);
    return -1;
  }

  long rc;
  curl_easy_getinfo(db->curl, CURLINFO_RESPONSE_CODE, &rc);

  /* The response code is zero if a non-HTTP transport was used. */
  if ((rc != 0) && (rc != 200)) {
    ERROR("curl_jolokia plugin: curl_easy_perform failed with "
          "response code %ld (%s)",
          rc, url);
    cjo_release_buffercontent(&db->replybuffer);
    return -1;
  }

  yajl_status json_status = yajl_parse(
      db->yajl, (unsigned char *)db->replybuffer.buffer, db->replybuffer.used);

  if (json_status != yajl_status_ok) {
    unsigned char *msg =
        yajl_get_error(db->yajl, /* verbose = */ 1,
                       /* jsonText = */ (unsigned char *)db->replybuffer.buffer,
                       db->replybuffer.used);
    ERROR("curl_jolokia plugin: yajl_parse failed: %s", msg);
    yajl_free_error(db->yajl, msg);
  }

  cjo_release_buffercontent(&db->replybuffer);
  cjo_release_buffercontent(&db->itembuffer);
  return 0;
} /* }}} int cjo_curl_perform */

static int cjo_perform(cjo_t *db) /* {{{ */
{
  yajl_handle yprev = db->yajl;

  db->yajl = yajl_alloc(&ycallbacks,
                        /* alloc funcs = */ NULL,
                        /* context = */ (void *)db);
  if (db->yajl == NULL) {
    ERROR("curl_jolokia plugin: yajl_alloc failed.");
    db->yajl = yprev;
    return -1;
  }

  int status;
  if (db->url)
    status = cjo_curl_perform(db);
  else
    status = cjo_sock_perform(db);
  if (status < 0) {
    yajl_free(db->yajl);
    db->yajl = yprev;
    return -1;
  }

  status = yajl_complete_parse(db->yajl);
  if (status != yajl_status_ok) {
    unsigned char *errmsg;

    errmsg = yajl_get_error(db->yajl, /* verbose = */ 0,
                            /* jsonText = */ NULL, /* jsonTextLen = */ 0);
    ERROR("curl_jolokia plugin: yajl_parse_complete failed: %s",
          (char *)errmsg);
    yajl_free_error(db->yajl, errmsg);
    yajl_free(db->yajl);
    db->yajl = yprev;
    return -1;
  }

  yajl_free(db->yajl);
  db->yajl = yprev;
  return 0;
} /* }}} int cjo_perform */

static int cjo_read(user_data_t *ud) /* {{{ */
{
  if ((ud == NULL) || (ud->data == NULL)) {
    ERROR("curl_jolokia plugin: cjo_read: Invalid user data.");
    return -1;
  }

  return cjo_perform((cjo_t *)ud->data);
} /* }}} int cjo_read */
/* end cURL callbacks */

/*----------------------------------------------------------------------------*/
/* Configuration handling functions {{{ */

static int cjo_config_append_string(const char *name,
                                    struct curl_slist **dest, /* {{{ */
                                    oconfig_item_t *ci) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("curl_jolokia plugin: `%s' needs exactly one string argument.",
            name);
    return -1;
  }

  *dest = curl_slist_append(*dest, ci->values[0].value.string);
  if (*dest == NULL)
    return -1;

  return 0;
} /* }}} int cjo_config_append_string */

void destroy_attribute_config(cjo_attribute_t *attribute) {
  sfree(attribute->attribute_name);
  sfree(attribute->attribute_match);
  sfree(attribute->type);
  sfree(attribute);
}

int cjo_get_attribute(cjo_bean_t *bean, oconfig_item_t *ci) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("curl_jolokia plugin: The `Attribute' block "
          "needs exactly one string argument.");

    return -1;
  }
  if (strcasecmp("AttributeName", ci->key)) {
    ERROR("curl_jolokia plugin: cjo_config_add_attribute: "
          "Invalid key: %s",
          ci->key);
    return -1;
  }

  cjo_attribute_t *attribute =
      (cjo_attribute_t *)calloc(1, sizeof(cjo_attribute_t));
  if (attribute == NULL) {
    ERROR("curl_jolokia plugin: calloc of bean attribute failed.");

    return -1;
  }
  memset(attribute, 0, sizeof(*attribute));

  int status = cf_util_get_string(ci, &attribute->attribute_name);
  if (status != 0) {
    ERROR("curl_jolokia plugin: failed to get attribute name.");

    destroy_attribute_config(attribute);
    return status;
  }

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Attribute", child->key) == 0) {
      status = cf_util_get_string(child, &attribute->attribute_match);
    } else if (strcasecmp("Type", child->key) == 0) {
      status = cf_util_get_string(child, &attribute->type);
    } else {
      ERROR("curl_jolokia plugin: Option `%s' not allowed in Bean Attribute.",
            child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0) {
    if ((attribute->attribute_name == NULL) ||
        (attribute->attribute_match == NULL) || (attribute->type == NULL)) {
      ERROR("curl_jolokia plugin: some attribute property is missing..");
      status = -1;
    }
  }
  if (status >= 0) {
    attribute->attribute_match_len = strlen(attribute->attribute_match);
    llentry_t *listentry = llentry_create(NULL, attribute);
    llist_append(bean->attributes, listentry);
  } else {
    destroy_attribute_config(attribute);
  }
  return status;
}

void destroy_bean_config(cjo_bean_t *bean) {
  if (bean == NULL)
    return;

  for (llentry_t *e = llist_head(bean->attributes); e != NULL; e = e->next) {
    cjo_attribute_t *attribute = (cjo_attribute_t *)e->value;
    e->value = NULL;

    destroy_attribute_config(attribute);
  }

  llist_destroy(bean->attributes);
  sfree(bean->bean_name);
  sfree(bean->mbean_match);
  sfree(bean->mbean_namespace);
  sfree(bean);
}

static int cjo_config_add_bean(cjo_t *db, /* {{{ */
                               oconfig_item_t *ci) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("curl_jolokia plugin: The `BeanName' block "
          "needs exactly one string argument.");

    return -1;
  }
  if (strcasecmp("BeanName", ci->key)) {
    ERROR("curl_jolokia plugin: cjo_config_add_bean: "
          "Invalid key: %s",
          ci->key);
    return -1;
  }

  cjo_bean_t *bean = (cjo_bean_t *)calloc(1, sizeof(cjo_bean_t));
  if (bean == NULL) {
    ERROR("curl_jolokia plugin: calloc of bean property failed.");

    return -1;
  }
  memset(bean, 0, sizeof(*bean));
  bean->attributes = llist_create();
  if (bean->attributes == NULL) {
    ERROR("curl_jolokia plugin: calloc of bean property failed.");

    destroy_bean_config(bean);
    return -1;
  }

  int status = cf_util_get_string(ci, &bean->bean_name);
  if (status != 0) {
    ERROR("curl_jolokia plugin: fetching of bean name failed.");

    destroy_bean_config(bean);
    return status;
  }

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("MBean", child->key) == 0) {
      status = cf_util_get_string(child, &bean->mbean_match);
    } else if (strcasecmp("BeanNameSpace", child->key) == 0) {
      status = cf_util_get_string(child, &bean->mbean_namespace);
    } else if (strcasecmp("AttributeName", child->key) == 0) {
      status = cjo_get_attribute(bean, child);
    } else {
      ERROR("curl_jolokia plugin: Option `%s' not allowed in Bean.",
            child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0) {
    if ((bean->bean_name == NULL) || (bean->mbean_match == NULL) ||
        (bean->attributes == NULL) || (llist_size(bean->attributes) == 0)) {
      ERROR("curl_jolokia plugin: some bean property is invalid..");
      status = -1;
    }
  }
  if (status >= 0) {
    int count;

    count = llist_size(bean->attributes);
    if (count > db->max_attribute_count)
      db->max_attribute_count = count;

    bean->mbean_match_len = strlen(bean->mbean_match);
    if (db->bean_configs == NULL)
      db->bean_configs = llist_create();

    llist_append(db->bean_configs, llentry_create(NULL, bean));
  } else {
    destroy_bean_config(bean);
  }

  return status;
} /* }}} int cjo_config_add_key */

static void cjo_destroy_bean_configs(llist_t *bean_configs) {
  llentry_t *e;

  if (bean_configs == NULL)
    return;

  for (e = llist_head(bean_configs); e != NULL; e = e->next) {
    cjo_bean_t *bean = (cjo_bean_t *)e->value;
    e->value = NULL;

    destroy_bean_config(bean);
  }

  llist_destroy(bean_configs);
}
static void cjo_free(void *arg) /* {{{ */
{
  cjo_t *db;

  DEBUG("curl_jolokia plugin: cjo_free (arg = %p);", arg);

  db = (cjo_t *)arg;

  if (db == NULL)
    return;

  if (db->curl != NULL)
    curl_easy_cleanup(db->curl);
  db->curl = NULL;

  cjo_destroy_bean_configs(db->bean_configs);
  db->bean_configs = NULL;
  sfree(db->instance);
  sfree(db->host);

  sfree(db->sock);

  sfree(db->url);
  sfree(db->user);
  sfree(db->pass);
  sfree(db->credentials);
  sfree(db->cacert);
  sfree(db->post_body);
  sfree(db->attributepool);

  curl_slist_free_all(db->headers);
  /// todo: freeme	cjo_attribute_values_t *attributepool;

  sfree(db);
} /* }}} void cjo_free */

static int cjo_config_add_url(oconfig_item_t *ci) /* {{{ */
{

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("curl_jolokia plugin: The `URL' block "
          "needs exactly one string argument.");
    return -1;
  }

  cjo_t *db = (cjo_t *)calloc(1, sizeof(cjo_t));
  if (db == NULL) {
    ERROR("curl_jolokia plugin: calloc failed.");
    return -1;
  }
  memset(db, 0, sizeof(*db));

  int status = 0;
  if (strcasecmp("URL", ci->key) == 0)
    status = cf_util_get_string(ci, &db->url);
  else if (strcasecmp("Sock", ci->key) == 0)
    status = cf_util_get_string(ci, &db->sock);
  else {
    ERROR("curl_jolokia plugin: cjo_config: "
          "Invalid key: %s",
          ci->key);
    return -1;
  }
  if (status != 0) {
    sfree(db);
    return status;
  }

  /* Fill the `cjo_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0)
      status = cf_util_get_string(child, &db->instance);
    else if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &db->host);
    else if (strcasecmp("MaxReadAttributes", child->key) == 0)
      status = cf_util_get_int(child, &db->max_attribute_count);
    else if (db->url && strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &db->user);
    else if (db->url && strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &db->pass);
    else if (db->url && strcasecmp("VerifyPeer", child->key) == 0)
      status = cf_util_get_boolean(child, &db->verify_peer);
    else if (db->url && strcasecmp("VerifyHost", child->key) == 0)
      status = cf_util_get_boolean(child, &db->verify_host);
    else if (db->url && strcasecmp("CACert", child->key) == 0)
      status = cf_util_get_string(child, &db->cacert);
    else if (db->url && strcasecmp("Header", child->key) == 0)
      status = cjo_config_append_string("Header", &db->headers, child);
    else if (db->url && strcasecmp("Post", child->key) == 0) {
      status = cf_util_get_string(child, &db->post_body);
      db->post_body_len = strlen(db->post_body);
      db->expect_escapes = (strchr(db->post_body, '\\') != NULL) ||
                           (strchr(db->post_body, '/') != NULL);
    } else if (strcasecmp("BeanName", child->key) == 0)
      status = cjo_config_add_bean(db, child);
    else {
      ERROR("curl_jolokia plugin: Option `%s' not allowed here.", child->key);

      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0) {
    if (status == 0 && db->url)
      status = cjo_init_curl(db);
  }

  /* If all went well, register this database for reading */
  if (status == 0) {
    char cb_name[DATA_MAX_NAME_LEN];

    if (db->instance == NULL)
      db->instance = strdup("default");

    DEBUG("curl_jolokia plugin: Registering new read callback: %s",
          db->instance);

    user_data_t ud = {
        .data = (void *)db,
        .free_func = cjo_free,
    };

    snprintf(cb_name, sizeof(cb_name), "curl_jolokia-%s-%s", db->instance,
             db->url ? db->url : db->sock);

    db->attributepool =
        calloc(db->max_attribute_count + 1, sizeof(*db->attributepool));

    plugin_register_complex_read(/* group = */ NULL, cb_name, cjo_read,
                                 /* interval = */ db->interval, &ud);
  } else {
    ERROR("curl_jolokia plugin: Failed to load URL");

    cjo_free(db);
    return -1;
  }

  return 0;
}
/* }}} int cjo_config_add_database */

static int cjo_config(oconfig_item_t *ci) /* {{{ */
{
  int success = 0;
  int errors = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Sock", child->key) == 0 ||
        strcasecmp("URL", child->key) == 0) {
      int status = cjo_config_add_url(child);
      if (status == 0)
        success++;
      else
        errors++;
    } else {
      WARNING("curl_jolokia plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0)) {
    ERROR("curl_jolokia plugin: All statements failed.");
    return -1;
  }

  return 0;
} /* }}} int cjo_config */

/* }}} End of configuration handling functions */

void module_register(void) {
  plugin_register_complex_config("curl_jolokia", cjo_config);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
