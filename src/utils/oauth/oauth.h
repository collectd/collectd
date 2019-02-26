/**
 * collectd - src/utils_oauth.h
 * ISC license
 *
 * Copyright (C) 2017  Florian Forster
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#ifndef UTILS_OAUTH_H
#define UTILS_OAUTH_H

#include "collectd.h"
#include "utils_time.h"

#ifndef GOOGLE_OAUTH_URL
#define GOOGLE_OAUTH_URL "https://www.googleapis.com/oauth2/v3/token"
#endif

struct oauth_s;
typedef struct oauth_s oauth_t;

int oauth_parse_json_token(char const *json, char *out_access_token,
                           size_t access_token_size, cdtime_t *expires_in);

typedef struct {
  char *project_id;
  oauth_t *oauth;
} oauth_google_t;

/* oauth_create_google_json creates an OAuth object from JSON encoded
 * credentials. */
oauth_google_t oauth_create_google_json(char const *json, char const *scope);

/* oauth_create_google_file reads path, which contains JSON encoded service
 * account credentials, and returns an OAuth object. */
oauth_google_t oauth_create_google_file(char const *path, char const *scope);

/* oauth_create_google_default looks for service account credentials in a couple
 * of well-known places and returns an OAuth object if found. The well known
 * locations are:
 *
 *   - ${GOOGLE_APPLICATION_CREDENTIALS}
 *   - ${HOME}/.config/gcloud/application_default_credentials.json
 */
oauth_google_t oauth_create_google_default(char const *scope);

/* oauth_destroy frees all resources associated with an OAuth object. */
void oauth_destroy(oauth_t *auth);

int oauth_access_token(oauth_t *auth, char *buffer, size_t buffer_size);

#endif
