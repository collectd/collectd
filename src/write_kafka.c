/**
 * collectd - src/write_kafka.c
 * Copyright (C) 2014       Pierre-Yves Ritschard
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
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_cmd_putval.h"
#include "utils_format_graphite.h"
#include "utils_format_json.h"
#include "utils_random.h"

#include <errno.h>
#include <librdkafka/rdkafka.h>
#include <stdint.h>

struct kafka_topic_context {
#define KAFKA_FORMAT_JSON 0
#define KAFKA_FORMAT_COMMAND 1
#define KAFKA_FORMAT_GRAPHITE 2
  uint8_t format;
  unsigned int graphite_flags;
  _Bool store_rates;
  rd_kafka_topic_conf_t *conf;
  rd_kafka_topic_t *topic;
  rd_kafka_conf_t *kafka_conf;
  rd_kafka_t *kafka;
  char *key;
  char *prefix;
  char *postfix;
  char escape_char;
  char *topic_name;
  pthread_mutex_t lock;
};

static int kafka_handle(struct kafka_topic_context *);
static int kafka_write(const data_set_t *, const value_list_t *, user_data_t *);
static int32_t kafka_partition(const rd_kafka_topic_t *, const void *, size_t,
                               int32_t, void *, void *);

/* Version 0.9.0 of librdkafka deprecates rd_kafka_set_logger() in favor of
 * rd_kafka_conf_set_log_cb(). This is to make sure we're not using the
 * deprecated function. */
#ifdef HAVE_LIBRDKAFKA_LOG_CB
#undef HAVE_LIBRDKAFKA_LOGGER
#endif

#if defined(HAVE_LIBRDKAFKA_LOGGER) || defined(HAVE_LIBRDKAFKA_LOG_CB)
static void kafka_log(const rd_kafka_t *, int, const char *, const char *);

static void kafka_log(const rd_kafka_t *rkt, int level, const char *fac,
                      const char *msg) {
  plugin_log(level, "%s", msg);
}
#endif

static rd_kafka_resp_err_t kafka_error() {
#if RD_KAFKA_VERSION >= 0x000b00ff
  return rd_kafka_last_error();
#else
  return rd_kafka_errno2err(errno);
#endif
}

static uint32_t kafka_hash(const char *keydata, size_t keylen) {
  uint32_t hash = 5381;
  for (; keylen > 0; keylen--)
    hash = ((hash << 5) + hash) + keydata[keylen - 1];
  return hash;
}

/* 31 bit -> 4 byte -> 8 byte hex string + null byte */
#define KAFKA_RANDOM_KEY_SIZE 9
#define KAFKA_RANDOM_KEY_BUFFER                                                \
  (char[KAFKA_RANDOM_KEY_SIZE]) { "" }
static char *kafka_random_key(char buffer[static KAFKA_RANDOM_KEY_SIZE]) {
  snprintf(buffer, KAFKA_RANDOM_KEY_SIZE, "%08" PRIX32, cdrand_u());
  return buffer;
}

static int32_t kafka_partition(const rd_kafka_topic_t *rkt, const void *keydata,
                               size_t keylen, int32_t partition_cnt, void *p,
                               void *m) {
  uint32_t key = kafka_hash(keydata, keylen);
  uint32_t target = key % partition_cnt;
  int32_t i = partition_cnt;

  while (--i > 0 && !rd_kafka_topic_partition_available(rkt, target)) {
    target = (target + 1) % partition_cnt;
  }
  return target;
}

static int kafka_handle(struct kafka_topic_context *ctx) /* {{{ */
{
  char errbuf[1024];
  rd_kafka_conf_t *conf;
  rd_kafka_topic_conf_t *topic_conf;

  if (ctx->kafka != NULL && ctx->topic != NULL)
    return 0;

  if (ctx->kafka == NULL) {
    if ((conf = rd_kafka_conf_dup(ctx->kafka_conf)) == NULL) {
      ERROR("write_kafka plugin: cannot duplicate kafka config");
      return 1;
    }

    if ((ctx->kafka = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf,
                                   sizeof(errbuf))) == NULL) {
      ERROR("write_kafka plugin: cannot create kafka handle.");
      return 1;
    }

    rd_kafka_conf_destroy(ctx->kafka_conf);
    ctx->kafka_conf = NULL;

    INFO("write_kafka plugin: created KAFKA handle : %s",
         rd_kafka_name(ctx->kafka));

#if defined(HAVE_LIBRDKAFKA_LOGGER) && !defined(HAVE_LIBRDKAFKA_LOG_CB)
    rd_kafka_set_logger(ctx->kafka, kafka_log);
#endif
  }

  if (ctx->topic == NULL) {
    if ((topic_conf = rd_kafka_topic_conf_dup(ctx->conf)) == NULL) {
      ERROR("write_kafka plugin: cannot duplicate kafka topic config");
      return 1;
    }

    if ((ctx->topic = rd_kafka_topic_new(ctx->kafka, ctx->topic_name,
                                         topic_conf)) == NULL) {
      ERROR("write_kafka plugin: cannot create topic : %s\n",
            rd_kafka_err2str(kafka_error()));
      return errno;
    }

    rd_kafka_topic_conf_destroy(ctx->conf);
    ctx->conf = NULL;

    INFO("write_kafka plugin: handle created for topic : %s",
         rd_kafka_topic_name(ctx->topic));
  }

  return 0;

} /* }}} int kafka_handle */

static int kafka_write(const data_set_t *ds, /* {{{ */
                       const value_list_t *vl, user_data_t *ud) {
  int status = 0;
  void *key;
  size_t keylen = 0;
  char buffer[8192];
  size_t bfree = sizeof(buffer);
  size_t bfill = 0;
  size_t blen = 0;
  struct kafka_topic_context *ctx = ud->data;

  if ((ds == NULL) || (vl == NULL) || (ctx == NULL))
    return EINVAL;

  pthread_mutex_lock(&ctx->lock);
  status = kafka_handle(ctx);
  pthread_mutex_unlock(&ctx->lock);
  if (status != 0)
    return status;

  bzero(buffer, sizeof(buffer));

  switch (ctx->format) {
  case KAFKA_FORMAT_COMMAND:
    status = cmd_create_putval(buffer, sizeof(buffer), ds, vl);
    if (status != 0) {
      ERROR("write_kafka plugin: cmd_create_putval failed with status %i.",
            status);
      return status;
    }
    blen = strlen(buffer);
    break;
  case KAFKA_FORMAT_JSON:
    format_json_initialize(buffer, &bfill, &bfree);
    format_json_value_list(buffer, &bfill, &bfree, ds, vl, ctx->store_rates);
    format_json_finalize(buffer, &bfill, &bfree);
    blen = strlen(buffer);
    break;
  case KAFKA_FORMAT_GRAPHITE:
    status =
        format_graphite(buffer, sizeof(buffer), ds, vl, ctx->prefix,
                        ctx->postfix, ctx->escape_char, ctx->graphite_flags);
    if (status != 0) {
      ERROR("write_kafka plugin: format_graphite failed with status %i.",
            status);
      return status;
    }
    blen = strlen(buffer);
    break;
  default:
    ERROR("write_kafka plugin: invalid format %i.", ctx->format);
    return -1;
  }

  key =
      (ctx->key != NULL) ? ctx->key : kafka_random_key(KAFKA_RANDOM_KEY_BUFFER);
  keylen = strlen(key);

  rd_kafka_produce(ctx->topic, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
                   buffer, blen, key, keylen, NULL);

  return status;
} /* }}} int kafka_write */

static void kafka_topic_context_free(void *p) /* {{{ */
{
  struct kafka_topic_context *ctx = p;

  if (ctx == NULL)
    return;

  if (ctx->topic_name != NULL)
    sfree(ctx->topic_name);
  if (ctx->topic != NULL)
    rd_kafka_topic_destroy(ctx->topic);
  if (ctx->conf != NULL)
    rd_kafka_topic_conf_destroy(ctx->conf);
  if (ctx->kafka_conf != NULL)
    rd_kafka_conf_destroy(ctx->kafka_conf);
  if (ctx->kafka != NULL)
    rd_kafka_destroy(ctx->kafka);

  sfree(ctx);
} /* }}} void kafka_topic_context_free */

static void kafka_config_topic(rd_kafka_conf_t *conf,
                               oconfig_item_t *ci) /* {{{ */
{
  int status;
  struct kafka_topic_context *tctx;
  char *key = NULL;
  char *val;
  char callback_name[DATA_MAX_NAME_LEN];
  char errbuf[1024];
  oconfig_item_t *child;
  rd_kafka_conf_res_t ret;

  if ((tctx = calloc(1, sizeof(*tctx))) == NULL) {
    ERROR("write_kafka plugin: calloc failed.");
    return;
  }

  tctx->escape_char = '.';
  tctx->store_rates = 1;
  tctx->format = KAFKA_FORMAT_JSON;
  tctx->key = NULL;

  if ((tctx->kafka_conf = rd_kafka_conf_dup(conf)) == NULL) {
    sfree(tctx);
    ERROR("write_kafka plugin: cannot allocate memory for kafka config");
    return;
  }

#ifdef HAVE_LIBRDKAFKA_LOG_CB
  rd_kafka_conf_set_log_cb(tctx->kafka_conf, kafka_log);
#endif

  if ((tctx->conf = rd_kafka_topic_conf_new()) == NULL) {
    rd_kafka_conf_destroy(tctx->kafka_conf);
    sfree(tctx);
    ERROR("write_kafka plugin: cannot create topic configuration.");
    return;
  }

  if (ci->values_num != 1) {
    WARNING("kafka topic name needed.");
    goto errout;
  }

  if (ci->values[0].type != OCONFIG_TYPE_STRING) {
    WARNING("kafka topic needs a string argument.");
    goto errout;
  }

  if ((tctx->topic_name = strdup(ci->values[0].value.string)) == NULL) {
    ERROR("write_kafka plugin: cannot copy topic name.");
    goto errout;
  }

  for (int i = 0; i < ci->children_num; i++) {
    /*
     * The code here could be simplified but makes room
     * for easy adding of new options later on.
     */
    child = &ci->children[i];
    status = 0;

    if (strcasecmp("Property", child->key) == 0) {
      if (child->values_num != 2) {
        WARNING("kafka properties need both a key and a value.");
        goto errout;
      }
      if (child->values[0].type != OCONFIG_TYPE_STRING ||
          child->values[1].type != OCONFIG_TYPE_STRING) {
        WARNING("kafka properties needs string arguments.");
        goto errout;
      }
      key = child->values[0].value.string;
      val = child->values[1].value.string;
      ret =
          rd_kafka_topic_conf_set(tctx->conf, key, val, errbuf, sizeof(errbuf));
      if (ret != RD_KAFKA_CONF_OK) {
        WARNING("cannot set kafka topic property %s to %s: %s.", key, val,
                errbuf);
        goto errout;
      }

    } else if (strcasecmp("Key", child->key) == 0) {
      if (cf_util_get_string(child, &tctx->key) != 0)
        continue;
      if (strcasecmp("Random", tctx->key) == 0) {
        sfree(tctx->key);
        tctx->key = strdup(kafka_random_key(KAFKA_RANDOM_KEY_BUFFER));
      }
    } else if (strcasecmp("Format", child->key) == 0) {
      status = cf_util_get_string(child, &key);
      if (status != 0)
        goto errout;

      assert(key != NULL);

      if (strcasecmp(key, "Command") == 0) {
        tctx->format = KAFKA_FORMAT_COMMAND;

      } else if (strcasecmp(key, "Graphite") == 0) {
        tctx->format = KAFKA_FORMAT_GRAPHITE;

      } else if (strcasecmp(key, "Json") == 0) {
        tctx->format = KAFKA_FORMAT_JSON;

      } else {
        WARNING("write_kafka plugin: Invalid format string: %s", key);
      }

      sfree(key);

    } else if (strcasecmp("StoreRates", child->key) == 0) {
      status = cf_util_get_boolean(child, &tctx->store_rates);
      (void)cf_util_get_flag(child, &tctx->graphite_flags,
                             GRAPHITE_STORE_RATES);

    } else if (strcasecmp("GraphiteSeparateInstances", child->key) == 0) {
      status = cf_util_get_flag(child, &tctx->graphite_flags,
                                GRAPHITE_SEPARATE_INSTANCES);

    } else if (strcasecmp("GraphiteAlwaysAppendDS", child->key) == 0) {
      status = cf_util_get_flag(child, &tctx->graphite_flags,
                                GRAPHITE_ALWAYS_APPEND_DS);

    } else if (strcasecmp("GraphitePreserveSeparator", child->key) == 0) {
      status = cf_util_get_flag(child, &tctx->graphite_flags,
                                GRAPHITE_PRESERVE_SEPARATOR);

    } else if (strcasecmp("GraphitePrefix", child->key) == 0) {
      status = cf_util_get_string(child, &tctx->prefix);
    } else if (strcasecmp("GraphitePostfix", child->key) == 0) {
      status = cf_util_get_string(child, &tctx->postfix);
    } else if (strcasecmp("GraphiteEscapeChar", child->key) == 0) {
      char *tmp_buff = NULL;
      status = cf_util_get_string(child, &tmp_buff);
      if (strlen(tmp_buff) > 1)
        WARNING("write_kafka plugin: The option \"GraphiteEscapeChar\" handles "
                "only one character. Others will be ignored.");
      tctx->escape_char = tmp_buff[0];
      sfree(tmp_buff);
    } else {
      WARNING("write_kafka plugin: Invalid directive: %s.", child->key);
    }

    if (status != 0)
      break;
  }

  rd_kafka_topic_conf_set_partitioner_cb(tctx->conf, kafka_partition);
  rd_kafka_topic_conf_set_opaque(tctx->conf, tctx);

  snprintf(callback_name, sizeof(callback_name), "write_kafka/%s",
           tctx->topic_name);

  status = plugin_register_write(
      callback_name, kafka_write,
      &(user_data_t){
          .data = tctx, .free_func = kafka_topic_context_free,
      });
  if (status != 0) {
    WARNING("write_kafka plugin: plugin_register_write (\"%s\") "
            "failed with status %i.",
            callback_name, status);
    goto errout;
  }

  pthread_mutex_init(&tctx->lock, /* attr = */ NULL);

  return;
errout:
  if (tctx->topic_name != NULL)
    free(tctx->topic_name);
  if (tctx->conf != NULL)
    rd_kafka_topic_conf_destroy(tctx->conf);
  if (tctx->kafka_conf != NULL)
    rd_kafka_conf_destroy(tctx->kafka_conf);
  sfree(tctx);
} /* }}} int kafka_config_topic */

static int kafka_config(oconfig_item_t *ci) /* {{{ */
{
  oconfig_item_t *child;
  rd_kafka_conf_t *conf;
  rd_kafka_conf_res_t ret;
  char errbuf[1024];

  if ((conf = rd_kafka_conf_new()) == NULL) {
    WARNING("cannot allocate kafka configuration.");
    return -1;
  }
  for (int i = 0; i < ci->children_num; i++) {
    child = &ci->children[i];

    if (strcasecmp("Topic", child->key) == 0) {
      kafka_config_topic(conf, child);
    } else if (strcasecmp(child->key, "Property") == 0) {
      char *key = NULL;
      char *val = NULL;

      if (child->values_num != 2) {
        WARNING("kafka properties need both a key and a value.");
        goto errout;
      }
      if (child->values[0].type != OCONFIG_TYPE_STRING ||
          child->values[1].type != OCONFIG_TYPE_STRING) {
        WARNING("kafka properties needs string arguments.");
        goto errout;
      }
      if ((key = strdup(child->values[0].value.string)) == NULL) {
        WARNING("cannot allocate memory for attribute key.");
        goto errout;
      }
      if ((val = strdup(child->values[1].value.string)) == NULL) {
        WARNING("cannot allocate memory for attribute value.");
        sfree(key);
        goto errout;
      }
      ret = rd_kafka_conf_set(conf, key, val, errbuf, sizeof(errbuf));
      if (ret != RD_KAFKA_CONF_OK) {
        WARNING("cannot set kafka property %s to %s: %s", key, val, errbuf);
        sfree(key);
        sfree(val);
        goto errout;
      }
      sfree(key);
      sfree(val);
    } else {
      WARNING("write_kafka plugin: Ignoring unknown "
              "configuration option \"%s\" at top level.",
              child->key);
    }
  }
  if (conf != NULL)
    rd_kafka_conf_destroy(conf);
  return 0;
errout:
  if (conf != NULL)
    rd_kafka_conf_destroy(conf);
  return -1;
} /* }}} int kafka_config */

void module_register(void) {
  plugin_register_complex_config("write_kafka", kafka_config);
}
