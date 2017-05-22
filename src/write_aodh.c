/**
 * collectd - src/write_aodh.c
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
 *   Red Hat NFVPE
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_complain.h"

#include <curl/curl.h>

typedef struct {
    char * os_auth_url;
    char * os_identity_api_version;
    char * os_username;
    char * os_password;
    char * os_tenant_name;
} conf_t;

/*
 * Private variables
 */

static char * auth_token;
static conf_t conf;

static int wa_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("OS_AUTH_URL", child->key) == 0)
    {
      conf.os_auth_url = malloc(sizeof(child->values[0].value.string) + 1);
      cf_util_get_string(ci, &conf.os_auth_url);
    } else if (strcasecmp("OS_IDENTITY_API_VERSION", child->key) == 0) {
      conf.os_auth_url = malloc(sizeof(child->values[0].value.string) + 1);
      cf_util_get_string(ci, &conf.os_identity_api_version);
    } else {
      ERROR("write_aodh plugin: Invalid configuration "
            "option: %s.",
            child->key);
    }
  }

  return 0;
} /* }}} int wa_config */

static int wa_init(void) /* {{{ */
{
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init(CURL_GLOBAL_SSL);
  auth_token = malloc(sizeof("blah") + 1);
  strncpy(auth_token, "blah", sizeof("blah"));
  return 0;
} /* }}} int wa_init */

static int wa_shutdown(void) /* {{{ */
{
	int status = 0;
	// TODO:
	// 1. Free config attributes

	free(conf.os_auth_url);
	free(conf.os_identity_api_version);

	return status;
} /* }}} int wa_shutdown */

void module_register(void) /* {{{ */
{
  plugin_register_complex_config("write_aodh", wa_config);
  plugin_register_init("write_aodh", wa_init);
  plugin_register_shutdown("write_aodh", wa_shutdown);
} /* }}} void module_register */