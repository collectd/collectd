#include "ast.h"
#include "parser.h"

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <curl/curl.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yyparse(void);
extern void set_lexer_buffer(const char *str);

extern pr_item_list_t *pr_items;

static int parse_metrics(char *lexer_buffer) {
  set_lexer_buffer(lexer_buffer);
  if (yyparse() != 0) {
    ERROR("prometheus plugin: Failed while parsing");
    return EXIT_FAILURE;
  }
  INFO("prometheus plugin: Parsing completed successfully.");
  return 0;
}

// libcurl setup is almost entirely taken from the nginx.c

static char *url;
static char *user;
static char *pass;
static char *verify_peer;
static char *verify_host;
static char *cacert;
static char *timeout;
static char *sock;
static char *jwt_token;

static CURL *curl;
static char prometheus_buffer[1048576];
static size_t prometheus_buffer_len;
static char prometheus_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] = {"URL",        "User",       "Password",
                                    "VerifyPeer", "VerifyHost", "CACert",
                                    "Timeout",    "Socket",     "JWTToken"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static size_t prometheus_write_callback(void *ptr, size_t size, size_t nmemb,
                                        void *stream) {
  size_t len = size * nmemb;

  if ((prometheus_buffer_len + len) >= sizeof(prometheus_buffer)) {
    assert(sizeof(prometheus_buffer) > prometheus_buffer_len);
    len = (sizeof(prometheus_buffer) - 1) - prometheus_buffer_len;
  }

  if (len == 0)
    return len;

  memcpy(&prometheus_buffer[prometheus_buffer_len], ptr, len);
  prometheus_buffer_len += len;
  prometheus_buffer[prometheus_buffer_len] = 0;

  return len;
}

static void config_set(char **var, const char *value) {
  if (*var != NULL) {
    free(*var);
    *var = NULL;
  }
  *var = sstrdup(value);
}

static int config(const char *key, const char *value) {
  if (strcasecmp(key, "url") == 0)
    config_set(&url, value);
  else if (strcasecmp(key, "user") == 0)
    config_set(&user, value);
  else if (strcasecmp(key, "password") == 0)
    config_set(&pass, value);
  else if (strcasecmp(key, "verifypeer") == 0)
    config_set(&verify_peer, value);
  else if (strcasecmp(key, "verifyhost") == 0)
    config_set(&verify_host, value);
  else if (strcasecmp(key, "cacert") == 0)
    config_set(&cacert, value);
  else if (strcasecmp(key, "timeout") == 0)
    config_set(&timeout, value);
  else if (strcasecmp(key, "socket") == 0)
    config_set(&sock, value);
  else if (strcasecmp(key, "jwttoken") == 0)
    config_set(&jwt_token, value);
  else
    return -1;
  return 0;
}

static int prometheus_init(void) {
  if (curl != NULL)
    curl_easy_cleanup(curl);

  if ((curl = curl_easy_init()) == NULL) {
    ERROR("prometheus plugin: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, prometheus_write_callback);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, prometheus_curl_error);

  if (user != NULL) {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt(curl, CURLOPT_USERNAME, user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, (pass == NULL) ? "" : pass);
#else
    static char credentials[1024];
    int status = ssnprintf(credentials, sizeof(credentials), "%s:%s", user,
                           pass == NULL ? "" : pass);
    if ((status < 0) || ((size_t)status >= sizeof(credentials))) {
      ERROR("prometheus plugin: Credentials would have been truncated.");
      return -1;
    }

    curl_easy_setopt(curl, CURLOPT_USERPWD, credentials);
#endif
  }

  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);

  if ((verify_peer == NULL) || IS_TRUE(verify_peer)) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  }

  if ((verify_host == NULL) || IS_TRUE(verify_host)) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (cacert != NULL) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, cacert);
  }

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (timeout != NULL) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, atol(timeout));
  } else {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(plugin_get_interval()));
  }
#endif

#ifdef HAVE_CURLOPT_UNIX_SOCKET_PATH
  if (sock != NULL) {
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, sock);
  }
#endif

  static char jwt_header[512];
  int status = ssnprintf(jwt_header, sizeof(jwt_header),
                         "Authorization: Bearer %s", jwt_token);
  if ((status < 0) || ((size_t)status >= sizeof(jwt_header))) {
    ERROR("prometheus plugin: jwt token would have been truncated.");
    return -1;
  }
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER,
                   curl_slist_append(NULL, jwt_header));

  return 0;
}

static metric_t *convert_pr_metric_to_metric(pr_metric_t *pr_metric,
                                             metric_type_t metric_type) {
  metric_t *metric = smalloc(sizeof(*metric));
  memset(metric, 0, sizeof(*metric));
  pr_label_t *cur_pr_label = pr_metric->labels;
  while (cur_pr_label) {
    if (metric_label_set(metric, cur_pr_label->name, cur_pr_label->value) !=
        0) {
      metric_reset(metric);
      free(metric);
      return NULL;
    }
    cur_pr_label = cur_pr_label->next;
  }
  switch (metric_type) {
  case (METRIC_TYPE_UNTYPED): {
    metric->value.counter_fp = pr_metric->value;
    break;
  }
  case (METRIC_TYPE_COUNTER_FP): {
    metric->value.counter_fp = pr_metric->value;
    break;
  }
  case (METRIC_TYPE_UP_DOWN_FP): {
    metric->value.up_down_fp = pr_metric->value;
    break;
  }
  default: {
    ERROR("Unsupported prometheus value representation");
    metric_reset(metric);
    free(metric);
    return NULL;
  }
  }
  if (pr_metric->timestamp->has_value) {
    metric->time = pr_metric->timestamp->value;
  }
  return metric;
}

static int convert_pr_fam_to_fam(pr_metric_family_t *pr_fam,
                                 metric_family_t *fam) {
  switch (pr_fam->tp) {
  case (PR_COUNTER): {
    fam->type = METRIC_TYPE_COUNTER_FP;
    break;
  }
  case (PR_GAUGE): {
    fam->type = METRIC_TYPE_UP_DOWN_FP;
    break;
  }
  case (PR_UNTYPED): {
    fam->type = METRIC_TYPE_UNTYPED;
    break;
  }
  case (PR_SUMMARY): {
    fam->type = METRIC_TYPE_UP_DOWN_FP;
    break;
  }
  case (PR_HISTOGRAM): {
    fam->type = METRIC_TYPE_UP_DOWN_FP;
    break;
  }
  default: {
    ERROR("Unknown prometheus metric family type");
    return EXIT_FAILURE;
  }
  }
  fam->name = sstrdup(pr_fam->name);
  if (pr_fam->help) {
    fam->help = sstrdup(pr_fam->help);
  }
  pr_metric_t *pr_cur_metric = pr_fam->metric_list;
  while (pr_cur_metric) {
    metric_t *metric = convert_pr_metric_to_metric(pr_cur_metric, fam->type);
    if (!metric) {
      return EXIT_FAILURE;
    }
    if (metric_family_metric_append(fam, *metric) != 0) {
      return EXIT_FAILURE;
    }
    if (metric_reset(metric) != 0) {
      return EXIT_FAILURE;
    }
    free(metric);
    pr_cur_metric = pr_cur_metric->next;
  }
  return 0;
}

static int dispatch_pr_items(pr_item_list_t *pr_items) {
  pr_item_t *cur_item = pr_items->begin;
  while (cur_item) {
    switch (cur_item->tp) {
    case (PR_METRIC_FAMILY_ITEM): {
      pr_metric_family_t *pr_metric_family = cur_item->body.metric_family;
      metric_family_t *fam = smalloc(sizeof(*fam));
      memset(fam, 0, sizeof(*fam));
      if (convert_pr_fam_to_fam(pr_metric_family, fam) != 0) {
        metric_family_free(fam);
        return EXIT_FAILURE;
      }
      int status = plugin_dispatch_metric_family(fam);
      if (status != 0) {
        ERROR("prometheus plugin: plugin_dispatch_metric_family failed: %s",
              STRERROR(status));
      }
      metric_family_free(fam);
      break;
    }
    case (PR_COMMENT_ITEM): {
      break;
    }
    }
    cur_item = cur_item->next;
  }
  return 0;
}

static int get_metrics(void) {
  prometheus_buffer_len = 0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  if (curl_easy_perform(curl) != CURLE_OK) {
    ERROR("curl_easy_perform() failed: %s", prometheus_curl_error);
    return EXIT_FAILURE;
  }
  if (parse_metrics(prometheus_buffer) != 0) {
    return EXIT_FAILURE;
  }
  if (dispatch_pr_items(pr_items) != 0) {
    pr_delete_item_list(pr_items);
    return EXIT_FAILURE;
  }
  pr_delete_item_list(pr_items);
  return 0;
}

static int prometheus_read(void) {
  if (get_metrics() != 0) {
    ERROR("Failed to fetch metrics from Prometheus.");
    return -1;
  }
  return 0;
}

static int prometheus_shutdown(void) { return 0; }

void module_register(void) {
  plugin_register_config("prometheus", config, config_keys, config_keys_num);
  plugin_register_init("prometheus", prometheus_init);
  plugin_register_shutdown("prometheus", prometheus_shutdown);
  plugin_register_read("prometheus", prometheus_read);
} /* void module_register (void) */
