/**
 * collectd - src/utils_gce.h
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

#ifndef UTILS_GCE_H
#define UTILS_GCE_H 1

/* gce_check returns 1 when running on Google Compute Engine (GCE) and 0
 * otherwise. */
_Bool gce_check(void);

/* gce_project_id returns the project ID of the instance, as configured when
 * creating the project.
 * For example "example-project-a". */
char *gce_project_id(void);

/* gce_instance_id returns the unique ID of the GCE instance. */
char *gce_instance_id(void);

/* gce_zone returns the zone in which the GCE instance runs. */
char *gce_zone(void);

/* gce_scope returns the list of scopes for the given service account (or the
 * default service account when NULL is passed). */
char *gce_scope(char const *email);

/* gce_access_token acquires an OAuth access token for the given service account
 * (or
 * the default service account when NULL is passed) and stores it in buffer.
 * Access tokens are automatically cached and renewed when they expire. Returns
 * zero on success, non-zero otherwise. */
int gce_access_token(char const *email, char *buffer, size_t buffer_size);

#endif
