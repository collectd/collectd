/**
 * collectd - src/notify_telegram.c
 * Copyright (C) 2024  Yan Anikiev
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
 *   Yan Anikiev <anikievyan@gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <microhttpd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <curl/curl.h>
#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif

#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif

#if MHD_VERSION >= 0x00097002
#define MHD_RESULT enum MHD_Result
#else
#define MHD_RESULT int
#endif

#define MAX_BUF_SIZE 1024
#define MAX_URL_SIZE 128
#define MAX_PARAMS_SIZE 2048
#define MAX_INPUT_MESSAGES_COUNT 30

#define YAJL_CB_PARSE_OK 1
#define YAJL_CB_PARSE_FAIL 0

struct response_buffer_t {
  char *response;
  size_t size;
};

struct parse_context_t {
  int depth;
  bool inside_key_ok;
  bool inside_key_update_id;
  bool inside_key_message;
  bool inside_key_message_chat;
  bool inside_key_message_chat_id;
  bool ok;
  char *max_update_id;
  int chat_id_count;
  char *chat_id[MAX_INPUT_MESSAGES_COUNT];
};

static struct plugin_config_t {
  char *bot_token;
  char *proxy_url;
  bool disable_getting_updates;
  char *webhook_url;
  char *webhook_host;
  char *webhook_port;
  char *mhd_daemon_host;
  unsigned short mhd_daemon_port;
  char **recipients;
  int recipients_len;
} plugin_config;

#if HAVE_YAJL_V2
typedef size_t yajl_len_t;
#else
typedef unsigned int yajl_len_t;
#endif

static const char *config_keys[] = {
    "BotToken",      "ProxyURL",      "DisableGettingUpdates",
    "WebhookURL",    "WebhookHost",   "WebhookPort",
    "MHDDaemonHost", "MHDDaemonPort", "RecipientChatID"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static const char *DEFAULT_PROXY_URL = "https://api.telegram.org/bot";
static const char *CONFIG_HELP_TEXT_TEMPLATE =
    "Here is the collectd configuration with your chat id:\n"
    "```\n"
    "<Plugin notify_telegram>\n"
    "    BotToken \"telegram-bot-token\"\n"
    "    RecipientChatID \"%s\"\n"
    "</Plugin>\n"
    "```\n"
    "If you want to use Local Bot API Server, specify `ProxyURL`\n"
    "If you want to use webhooks instead of long polling, specify "
    "`WebhookURL`, `WebhookHost`, `WebhookPort`, `MHDDaemonHost` and "
    "`MHDDaemonPort`\n"
    "If you do not want to send this help text, use `DisableGettingUpdates`";

static pthread_mutex_t telegram_lock = PTHREAD_MUTEX_INITIALIZER;

static struct MHD_Daemon *httpd;

static size_t tg_curl_write_callback(void *data, size_t size, size_t nmemb,
                                     void *user_data) {
  size_t realsize = size * nmemb;
  struct response_buffer_t *buf = (struct response_buffer_t *)user_data;
  char *ptr = realloc(buf->response, buf->size + realsize + 1);
  if (!ptr) {
    ERROR("notify_telegram: realloc failed.");
    return 0;
  }

  buf->response = ptr;
  memcpy(&(buf->response[buf->size]), data, realsize);
  buf->size += realsize;
  buf->response[buf->size] = 0;

  return realsize;
}

static int tg_parse_bool_callback(void *ctx, int bool_val) {
  struct parse_context_t *parse_context = (struct parse_context_t *)ctx;
  if (parse_context->inside_key_ok) {
    parse_context->ok = bool_val;
    parse_context->inside_key_ok = false;
  }

  return YAJL_CB_PARSE_OK;
}

static int tg_parse_number_callback(void *ctx, const char *number,
                                    yajl_len_t number_len) {
  struct parse_context_t *parse_context = (struct parse_context_t *)ctx;

  if (parse_context->inside_key_update_id) {
    if (parse_context->max_update_id == NULL) {
      parse_context->max_update_id = strndup(number, number_len);
      if (parse_context->max_update_id == NULL) {
        ERROR("notify_telegram: strndup failed.");
        return YAJL_CB_PARSE_FAIL;
      }
    } else {
      char number_buffer[number_len + 1];
      memcpy(number_buffer, number, number_len);
      number_buffer[number_len] = '\0';

      if (strtoull(number_buffer, NULL, 10) >
          strtoull(parse_context->max_update_id, NULL, 10)) {
        parse_context->max_update_id = strndup(number, number_len);
        if (parse_context->max_update_id == NULL) {
          ERROR("notify_telegram: strndup failed.");
          return YAJL_CB_PARSE_FAIL;
        }
      }
    }
    parse_context->inside_key_update_id = false;
  } else if (parse_context->inside_key_message_chat_id) {
    parse_context->chat_id[parse_context->chat_id_count] =
        strndup(number, number_len);
    if (parse_context->chat_id[parse_context->chat_id_count] == NULL) {
      ERROR("notify_telegram: strndup failed.");
      return YAJL_CB_PARSE_FAIL;
    }
    parse_context->chat_id_count++;
    parse_context->inside_key_message_chat_id = false;
  }

  return YAJL_CB_PARSE_OK;
}

static int tg_parse_start_map_callback(void *ctx) {
  struct parse_context_t *parse_context = (struct parse_context_t *)ctx;
  parse_context->depth++;

  return YAJL_CB_PARSE_OK;
}

static int tg_parse_map_key_callback(void *ctx, const unsigned char *key,
                                     yajl_len_t key_len) {
  struct parse_context_t *parse_context = (struct parse_context_t *)ctx;
  char key_buffer[key_len + 1];
  memcpy(key_buffer, key, key_len);
  key_buffer[sizeof(key_buffer) - 1] = '\0';

  if (parse_context->depth == 1) {
    parse_context->inside_key_ok = false;
    if (!strcmp(key_buffer, "ok")) {
      parse_context->inside_key_ok = true;
    }
  } else if (parse_context->depth == 2) {
    parse_context->inside_key_update_id = false;
    parse_context->inside_key_message = false;
    parse_context->inside_key_message_chat = false;
    parse_context->inside_key_message_chat_id = false;
    if (!strcmp(key_buffer, "update_id")) {
      parse_context->inside_key_update_id = true;
    } else if (!strcmp(key_buffer, "message")) {
      parse_context->inside_key_message = true;
    }
  } else if (parse_context->depth == 3) {
    parse_context->inside_key_message_chat = false;
    parse_context->inside_key_message_chat_id = false;
    if (parse_context->inside_key_message && !strcmp(key_buffer, "chat")) {
      parse_context->inside_key_message_chat = true;
    }
  } else if (parse_context->depth == 4) {
    parse_context->inside_key_message_chat_id = false;
    if (parse_context->inside_key_message_chat && !strcmp(key_buffer, "id")) {
      parse_context->inside_key_message_chat_id = true;
    }
  }

  return YAJL_CB_PARSE_OK;
}

static int tg_parse_end_map_callback(void *ctx) {
  struct parse_context_t *parse_context = (struct parse_context_t *)ctx;
  parse_context->depth--;

  return YAJL_CB_PARSE_OK;
}

static void free_parse_context(struct parse_context_t *parse_context) {
  if (parse_context) {
    sfree(parse_context->max_update_id);
    for (int i = 0; i < parse_context->chat_id_count; ++i) {
      sfree(parse_context->chat_id[i]);
    }
  }
}

static CURLcode
telegram_bot_api_send_request(struct response_buffer_t *response_buffer,
                              const char *request_url,
                              const char *request_params) {
  char url[MAX_URL_SIZE] = "";
  snprintf(url, sizeof(url), "%s%s/%s",
           (plugin_config.proxy_url == NULL) ? DEFAULT_PROXY_URL
                                             : plugin_config.proxy_url,
           plugin_config.bot_token, request_url);

  CURL *handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS,
                   (request_params == NULL) ? "" : request_params);
  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, tg_curl_write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)response_buffer);

  pthread_mutex_lock(&telegram_lock);
  CURLcode response_code = curl_easy_perform(handle);
  pthread_mutex_unlock(&telegram_lock);

  curl_easy_cleanup(handle);
  DEBUG("notify_telegram: curl response = %s", response_buffer->response);
  return response_code;
}

static CURLcode
telegram_bot_api_send_message(struct response_buffer_t *response_buffer,
                              const char *params, const char *message,
                              char **chat_id, int chat_id_count) {
  char url[MAX_URL_SIZE] = "";
  snprintf(url, sizeof(url), "%s%s/sendMessage",
           (plugin_config.proxy_url == NULL) ? DEFAULT_PROXY_URL
                                             : plugin_config.proxy_url,
           plugin_config.bot_token);

  CURL *handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, tg_curl_write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)response_buffer);

  char params_buf[MAX_PARAMS_SIZE] = "";
  char message_buf[MAX_BUF_SIZE] = "";
  CURLcode response_code = CURLE_OK;
  for (int i = 0; i < chat_id_count; ++i) {
    snprintf(message_buf, sizeof(message_buf), message, chat_id[i]);
    snprintf(params_buf, sizeof(params_buf), params, chat_id[i], message_buf);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, params_buf);

    pthread_mutex_lock(&telegram_lock);
    response_code = curl_easy_perform(handle);
    pthread_mutex_unlock(&telegram_lock);

    DEBUG("notify_telegram: curl response = %s", response_buffer->response);
    if (response_code != CURLE_OK) {
      curl_easy_cleanup(handle);
      return response_code;
    }
  }

  curl_easy_cleanup(handle);
  return response_code;
}

static int
telegram_bot_api_parse_response(struct response_buffer_t *response_buffer,
                                struct parse_context_t *parse_context) {
  yajl_callbacks ycallbacks = {NULL,
                               tg_parse_bool_callback,
                               NULL,
                               NULL,
                               tg_parse_number_callback,
                               NULL,
                               tg_parse_start_map_callback,
                               tg_parse_map_key_callback,
                               tg_parse_end_map_callback,
                               NULL,
                               NULL};
  yajl_handle yajl_handler = yajl_alloc(&ycallbacks,
#if HAVE_YAJL_V2
                                        NULL,
#else
                                        NULL, NULL,
#endif
                                        (void *)parse_context);
  if (yajl_handler == NULL) {
    ERROR("notify_telegram: yajl_alloc failed.");
    return -1;
  }

  yajl_status status =
      yajl_parse(yajl_handler, (unsigned char *)response_buffer->response,
                 response_buffer->size);

  if (status != yajl_status_ok) {
    ERROR("notify_telegram: yajl_parse failed. status=%d", status);
    return -1;
  }

  return 0;
}

static int notify_telegram_read(void) {
  if (plugin_config.disable_getting_updates || plugin_config.webhook_host) {
    return 0;
  }

  struct response_buffer_t buf = {0};
  char params[MAX_PARAMS_SIZE] = "";
  snprintf(params, sizeof(params), "limit=%d&allowed_updates=[\"message\"]",
           MAX_INPUT_MESSAGES_COUNT);
  CURLcode response_code =
      telegram_bot_api_send_request(&buf, "getUpdates", params);
  if (response_code != CURLE_OK) {
    ERROR("notify_telegram: telegram_bot_api_send_request getUpdates failed. "
          "response_code=%d",
          response_code);
    sfree(buf.response);
    return -1;
  }

  struct parse_context_t parse_context = {0};
  int status = telegram_bot_api_parse_response(&buf, &parse_context);
  sfree(buf.response);
  if (status != 0) {
    ERROR("notify_telegram: telegram_bot_api_parse_response failed. status=%d",
          status);
    free_parse_context(&parse_context);
    return -1;
  }

  if (!parse_context.ok) {
    ERROR("notify_telegram: not ok response from telegram api.");
    free_parse_context(&parse_context);
    return -1;
  }

  if (parse_context.chat_id_count == 0 || parse_context.max_update_id == NULL) {
    free_parse_context(&parse_context);
    return 0;
  }

  struct response_buffer_t help_response_buf = {0};
  response_code = telegram_bot_api_send_message(
      &help_response_buf, "parse_mode=MarkdownV2&chat_id=%s&text=%s",
      CONFIG_HELP_TEXT_TEMPLATE, parse_context.chat_id,
      parse_context.chat_id_count);
  sfree(help_response_buf.response);
  if (response_code != CURLE_OK) {
    ERROR("notify_telegram: telegram_bot_api_send_message with help text "
          "failed. response_code=%d",
          response_code);
    free_parse_context(&parse_context);
    return -1;
  }

  struct response_buffer_t update_response_buf = {0};
  snprintf(params, sizeof(params), "offset=%llu",
           1ULL + strtoull(parse_context.max_update_id, NULL, 10));
  free_parse_context(&parse_context);
  response_code =
      telegram_bot_api_send_request(&update_response_buf, "getUpdates", params);
  sfree(update_response_buf.response);
  if (response_code != CURLE_OK) {
    ERROR("notify_telegram: telegram_bot_api_send_request getUpdates failed. "
          "response_code=%d",
          response_code);
    return -1;
  }

  return 0;
}

static MHD_RESULT
telegram_mhd_handler(void *cls, struct MHD_Connection *connection,
                     const char *url, const char *method, const char *version,
                     const char *upload_data, size_t *upload_data_size,
                     void **connection_state) {
  DEBUG("notify_telegram: webhook triggered");

  if (strcmp(method, MHD_HTTP_METHOD_POST) != 0) {
    return MHD_NO;
  }
  if (plugin_config.webhook_url &&
      strcmp(url, plugin_config.webhook_url) != 0) {
    return MHD_NO;
  }

  static struct response_buffer_t buf;
  if (*connection_state == NULL) {
    buf.response = NULL;
    buf.size = 0;
    *connection_state = &buf;
    return MHD_YES;
  }

  if (*upload_data_size != 0) {
    char *ptr = realloc(buf.response, buf.size + *upload_data_size + 1);
    if (!ptr) {
      ERROR("notify_telegram: realloc failed.");
      return MHD_NO;
    }

    buf.response = ptr;
    memcpy(&(buf.response[buf.size]), upload_data, *upload_data_size);
    buf.size += *upload_data_size;
    buf.response[buf.size] = 0;

    *upload_data_size = 0;
    return MHD_YES;
  }

  struct parse_context_t parse_context = {0};
  parse_context.depth = 1;
  int status = telegram_bot_api_parse_response(&buf, &parse_context);
  sfree(buf.response);
  if (status != 0) {
    ERROR("notify_telegram: telegram_bot_api_parse_response failed. status=%d",
          status);
    free_parse_context(&parse_context);
    return MHD_NO;
  }

  if (parse_context.chat_id_count == 0 || parse_context.max_update_id == NULL) {
    WARNING("notify_telegram: no chat_id was found");
    free_parse_context(&parse_context);
#if MHD_VERSION >= 0x00097701
    struct MHD_Response *res = MHD_create_response_empty(MHD_RF_NONE);
#else /* if MHD_VERSION < 0x00097701 */
    struct MHD_Response *res =
        MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
#endif
    MHD_RESULT mhd_status = MHD_queue_response(connection, MHD_HTTP_OK, res);
    return mhd_status;
  }

  struct response_buffer_t help_response_buf = {0};
  CURLcode response_code = telegram_bot_api_send_message(
      &help_response_buf, "parse_mode=MarkdownV2&chat_id=%s&text=%s",
      CONFIG_HELP_TEXT_TEMPLATE, parse_context.chat_id,
      parse_context.chat_id_count);
  sfree(help_response_buf.response);
  if (response_code != CURLE_OK) {
    ERROR("notify_telegram: telegram_bot_api_send_message with help text "
          "failed. response_code=%d",
          response_code);
    free_parse_context(&parse_context);
    return MHD_NO;
  }

#if MHD_VERSION >= 0x00097701
  struct MHD_Response *res = MHD_create_response_empty(MHD_RF_NONE);
#else /* if MHD_VERSION < 0x00097701 */
  struct MHD_Response *res =
      MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
#endif
  MHD_RESULT mhd_status = MHD_queue_response(connection, MHD_HTTP_OK, res);
  return mhd_status;
}

static void telegram_mhd_logger(__attribute__((unused)) void *arg,
                                char const *fmt, va_list ap) {
  char errbuf[1024];
  vsnprintf(errbuf, sizeof(errbuf), fmt, ap);

  ERROR("notify_telegram mhd_logger: %s", errbuf);
}

#if MHD_VERSION >= 0x00090000

static int telegram_open_socket(int addrfamily) {
  /* {{{ */
  char service[NI_MAXSERV];
  ssnprintf(service, sizeof(service), "%hu", plugin_config.mhd_daemon_port);

  struct addrinfo *res;
  int status = getaddrinfo(plugin_config.mhd_daemon_host, service,
                           &(struct addrinfo){
                               .ai_flags = AI_PASSIVE,
                               .ai_family = addrfamily,
                               .ai_socktype = SOCK_STREAM,
                           },
                           &res);
  if (status != 0) {
    ERROR("notify_telegram: getaddrinfo failed. host=%s, port=%s",
          plugin_config.mhd_daemon_host, service);
    return -1;
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
    int flags = ai->ai_socktype;
#ifdef SOCK_CLOEXEC
    flags |= SOCK_CLOEXEC;
#endif

    fd = socket(ai->ai_family, flags, 0);
    if (fd == -1) {
      WARNING("notify_telegram: socket failed. socket=%d", ai->ai_family);
      continue;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
      WARNING(
          "notify_telegram: setsockopt(SO_REUSEADDR) failed. socket=%d, err=%s",
          ai->ai_family, STRERRNO);
      close(fd);
      fd = -1;
      continue;
    }

    if (bind(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
      WARNING("notify_telegram: bind failed. socket=%d", ai->ai_family);
      close(fd);
      fd = -1;
      continue;
    }

    if (listen(fd, /* backlog = */ 16) != 0) {
      WARNING("notify_telegram: listen failed. socket=%d", ai->ai_family);
      close(fd);
      fd = -1;
      continue;
    }

    char str_node[NI_MAXHOST];
    char str_service[NI_MAXSERV];

    getnameinfo(ai->ai_addr, ai->ai_addrlen, str_node, sizeof(str_node),
                str_service, sizeof(str_service),
                NI_NUMERICHOST | NI_NUMERICSERV);

    INFO("notify_telegram: Listening on [%s]:%s.", str_node, str_service);
    break;
  }

  freeaddrinfo(res);

  return fd;
} /* }}} int telegram_open_socket */

static struct MHD_Daemon *telegram_start_daemon(void) {
  /* {{{ */
  int fd = telegram_open_socket(PF_INET);
  if (fd == -1) {
    ERROR("notify_telegram: Opening a listening socket for [%s]:%hu failed.",
          (plugin_config.mhd_daemon_host != NULL)
              ? plugin_config.mhd_daemon_host
              : "::",
          plugin_config.mhd_daemon_port);
    return NULL;
  }

  unsigned int flags = MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG;
#if MHD_VERSION >= 0x00095300
  flags |= MHD_USE_INTERNAL_POLLING_THREAD;
#endif

  struct MHD_Daemon *d = MHD_start_daemon(
      flags, plugin_config.mhd_daemon_port,
      /* MHD_AcceptPolicyCallback = */ NULL,
      /* MHD_AcceptPolicyCallback arg = */ NULL, telegram_mhd_handler, NULL,
      MHD_OPTION_LISTEN_SOCKET, fd, MHD_OPTION_EXTERNAL_LOGGER,
      telegram_mhd_logger, NULL, MHD_OPTION_END);
  if (d == NULL) {
    ERROR("notify_telegram: MHD_start_daemon failed.");
    close(fd);
    return NULL;
  }

  return d;
} /* }}} struct MHD_Daemon *telegram_start_daemon */

#else /* if MHD_VERSION < 0x00090000 */

static struct MHD_Daemon *telegram_start_daemon(void) {
  /* {{{ */
  struct MHD_Daemon *d = MHD_start_daemon(
      MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG,
      plugin_config.mhd_daemon_port,
      /* MHD_AcceptPolicyCallback = */ NULL,
      /* MHD_AcceptPolicyCallback arg = */ NULL, telegram_mhd_handler, NULL,
      MHD_OPTION_EXTERNAL_LOGGER, telegram_mhd_logger, NULL, MHD_OPTION_END);
  if (d == NULL) {
    ERROR("notify_telegram: MHD_start_daemon failed.");
    return NULL;
  }

  return d;
} /* }}} struct MHD_Daemon *telegram_start_daemon */
#endif

static int notify_telegram_init(void) {
  curl_global_init(CURL_GLOBAL_SSL);

  if (plugin_config.disable_getting_updates) {
    DEBUG("notify_telegram: getting updates disabled");
    return 0;
  }

  if (plugin_config.webhook_host && !httpd) {
    httpd = telegram_start_daemon();
    if (!httpd) {
      ERROR("notify_telegram: start daemon failed.");
      return -1;
    }
    DEBUG("notify_telegram: daemon started");

    struct response_buffer_t buf = {0};
    char params[MAX_PARAMS_SIZE] = "";
    snprintf(params, sizeof(params),
             "url=%s:%s%s&allowed_updates=[\"message\"]",
             plugin_config.webhook_host,
             plugin_config.webhook_port ? plugin_config.webhook_port : "443",
             plugin_config.webhook_url ? plugin_config.webhook_url : "");
    CURLcode response_code =
        telegram_bot_api_send_request(&buf, "setWebhook", params);
    sfree(buf.response);
    if (response_code != CURLE_OK) {
      ERROR("notify_telegram: telegram_bot_api_send_request setWebhook failed. "
            "response_code=%d",
            response_code);
      return -1;
    }
  } else if (!plugin_config.webhook_host) {
    DEBUG("notify_telegram: long polling started");

    struct response_buffer_t buf = {0};
    char params[MAX_PARAMS_SIZE] = "";
    CURLcode response_code =
        telegram_bot_api_send_request(&buf, "deleteWebhook", params);
    sfree(buf.response);
    if (response_code != CURLE_OK) {
      ERROR("notify_telegram: telegram_bot_api_send_request deleteWebhook "
            "failed. response_code=%d",
            response_code);
      return -1;
    }
  }

  return notify_telegram_read();
}

static int notify_telegram_shutdown(void) {
  curl_global_cleanup();

  if (httpd) {
    MHD_stop_daemon(httpd);
    httpd = NULL;
  }

  for (int i = 0; i < plugin_config.recipients_len; ++i) {
    sfree(plugin_config.recipients[i]);
  }
  sfree(plugin_config.recipients);
  sfree(plugin_config.bot_token);
  sfree(plugin_config.proxy_url);
  sfree(plugin_config.webhook_url);
  sfree(plugin_config.webhook_host);
  sfree(plugin_config.webhook_port);
  sfree(plugin_config.mhd_daemon_host);

  return 0;
}

static int notify_telegram_config(const char *key, const char *value) {

#if MHD_VERSION < 0x00090000
  if (strcasecmp(key, "MHDDaemonHost") == 0) {
    ERROR("notify_telegram: Option `MHDDaemonHost' not supported. Please "
          "upgrade libmicrohttpd to at least 0.9.0");
    return -1;
  }
#endif

  if (strcasecmp(key, "RecipientChatID") == 0) {
    char **tmp;
    tmp = realloc(plugin_config.recipients,
                  (plugin_config.recipients_len + 1) * sizeof(char *));
    if (tmp == NULL) {
      ERROR("notify_telegram: realloc failed.");
      return -1;
    }
    plugin_config.recipients = tmp;
    plugin_config.recipients[plugin_config.recipients_len] = strdup(value);
    if (plugin_config.recipients[plugin_config.recipients_len] == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
    plugin_config.recipients_len++;
  } else if (strcasecmp(key, "BotToken") == 0) {
    sfree(plugin_config.bot_token);
    plugin_config.bot_token = strdup(value);
    if (plugin_config.bot_token == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
  } else if (strcasecmp(key, "ProxyURL") == 0) {
    sfree(plugin_config.proxy_url);
    plugin_config.proxy_url = strdup(value);
    if (plugin_config.proxy_url == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
  } else if (strcasecmp(key, "DisableGettingUpdates") == 0) {
    plugin_config.disable_getting_updates = IS_TRUE(value);
  } else if (strcasecmp(key, "WebhookURL") == 0) {
    sfree(plugin_config.webhook_url);
    plugin_config.webhook_url = strdup(value);
    if (plugin_config.webhook_url == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
  } else if (strcasecmp(key, "WebhookHost") == 0) {
    sfree(plugin_config.webhook_host);
    plugin_config.webhook_host = strdup(value);
    if (plugin_config.webhook_host == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
  } else if (strcasecmp(key, "WebhookPort") == 0) {
    sfree(plugin_config.webhook_port);
    plugin_config.webhook_port = strdup(value);
    if (plugin_config.webhook_port == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
  } else if (strcasecmp(key, "MHDDaemonHost") == 0) {
    sfree(plugin_config.mhd_daemon_host);
    plugin_config.mhd_daemon_host = strdup(value);
    if (plugin_config.mhd_daemon_host == NULL) {
      ERROR("notify_telegram: strdup failed.");
      return -1;
    }
  } else if (strcasecmp(key, "MHDDaemonPort") == 0) {
    char *endptr;
    errno = 0;
    int tmp = (int)strtol(value, &endptr, /* base = */ 10);
    if (errno != 0 || endptr == value || tmp < 1 || tmp > 65535) {
      ERROR("notify_telegram: converting MHDDaemonPort failed.");
      return -1;
    }
    plugin_config.mhd_daemon_port = (unsigned short)tmp;
  } else {
    ERROR("notify_telegram: unknown config key. key=%s", key);
    return -1;
  }
  return 0;
}

static void buffer_append(char **buf_ptr, int *buf_len, const char *key,
                          const char *value) {
  if (*buf_len > 0 && strlen(value) > 0) {
    int print_count = snprintf(*buf_ptr, *buf_len, "%s = %s\n", key, value);
    if (print_count > 0) {
      *buf_ptr += print_count;
      *buf_len -= print_count;
    }
  }
}

static int notify_telegram_notification(const notification_t *n,
                                        user_data_t __attribute__((unused)) *
                                            user_data) {
  char buf[MAX_BUF_SIZE] = "";
  char *buf_ptr = buf;
  int buf_len = sizeof(buf);

  const char *severity =
      (n->severity == NOTIF_FAILURE)
          ? "FAILURE"
          : ((n->severity == NOTIF_WARNING)
                 ? "WARNING"
                 : ((n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN"));

  buffer_append(&buf_ptr, &buf_len, "<b>Notification:</b>\nseverity", severity);
  buffer_append(&buf_ptr, &buf_len, "host", n->host);
  buffer_append(&buf_ptr, &buf_len, "plugin", n->plugin);
  buffer_append(&buf_ptr, &buf_len, "plugin_instance", n->plugin_instance);
  buffer_append(&buf_ptr, &buf_len, "type", n->type);
  buffer_append(&buf_ptr, &buf_len, "type_instance", n->type_instance);
  buffer_append(&buf_ptr, &buf_len, "message", n->message);

  buf[sizeof(buf) - 1] = '\0';

  struct response_buffer_t notify_response_buf = {0};
  CURLcode response_code = telegram_bot_api_send_message(
      &notify_response_buf, "parse_mode=HTML&chat_id=%s&text=%s", buf,
      plugin_config.recipients, plugin_config.recipients_len);
  sfree(notify_response_buf.response);
  if (response_code != CURLE_OK) {
    ERROR("notify_telegram: telegram_bot_api_send_message with notification "
          "failed. response_code=%d",
          response_code);
    return -1;
  }

  return 0;
}

void module_register(void) {
  plugin_register_init("notify_telegram", notify_telegram_init);
  plugin_register_shutdown("notify_telegram", notify_telegram_shutdown);
  plugin_register_config("notify_telegram", notify_telegram_config, config_keys,
                         config_keys_num);
  plugin_register_read("notify_telegram", notify_telegram_read);
  plugin_register_notification("notify_telegram", notify_telegram_notification,
                               /* user_data = */ NULL);
}
