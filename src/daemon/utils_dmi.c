/*
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 * Przemyslaw Szczerbik <przemyslawx.szczerbik@intel.com>
 * Roman Ulan <romanx.ulan@intel.com>
 *
 */

#include "collectd.h"

#include "plugin.h"
#include "common.h"
#include "utils_dmi.h"

#define DMI_FOR_EACH(fun, dmi_reader, dmi_t, len, ...)                         \
  int rval = 0;                                                                \
  TRACE();                                                                     \
  if (!dmi_reader || !dmi_t) {                                                 \
    ERROR("%s: NULL pointer", __func__);                                       \
    return -1;                                                                 \
  }                                                                            \
  for (int i = 0; i < len; ++i) {                                              \
    rval += fun(__VA_ARGS__);                                                  \
  }                                                                            \
  if (len != rval)                                                             \
    ERROR("%s: Failed to get all DMI settings", __func__);                     \
  return rval;

#define DMIDECODE_CMD_FMT_LEN (256 + DMI_MAX_NAME_LEN)
#define DMIDECODE_CMD_FMT                                                      \
  "dmidecode -t %d | grep \"%s\" | cut -d ':' -f 2 | sed -e 's/^[ \\t]*//' "   \
  "2>/dev/null"
#define TRACE() DEBUG("%s:%s:%d", __FILE__, __FUNCTION__, __LINE__)

static size_t dmi_get_bulk(dmi_reader *self, dmi_t **s, size_t s_len);
static size_t dmi_get(dmi_reader *self, dmi_t *s);
static void dmi_clean(dmi_reader *self);
static void dmi_free(dmi_reader *self);

static size_t dmidecode_get_setting(dmi_type type, dmi_setting *s);
static size_t dmidecode_parse_output(FILE *output, dmi_setting *s);

void dmidecode_init(dmi_reader *self) {
  if (!self) {
    ERROR("%s: NULL pointer", __func__);
    return;
  }

  self->ctx = NULL;
  self->get = dmi_get;
  self->get_bulk = dmi_get_bulk;
  self->clean = dmi_clean;
  self->free = dmi_free;

  assert(self->get != NULL);
  assert(self->get_bulk != NULL);
  assert(self->clean != NULL);
  assert(self->free != NULL);
}

dmi_reader *dmidecode_alloc(void) {
  dmi_reader *self = (dmi_reader *)malloc(sizeof(dmi_reader));
  dmidecode_init(self);

  return self;
}

static void dmi_clean(dmi_reader *self) {
  self->get = NULL;
  self->get_bulk = NULL;
  self->clean = NULL;
  self->free = NULL;
}

static void dmi_free(dmi_reader *self) {
  self->clean(self);
  sfree(self);
}

static size_t dmidecode_parse_output(FILE *output, dmi_setting *s) {
  TRACE();

  while (fgets(s->value, (sizeof(char) * DMI_MAX_VAL_LEN), output) != NULL) {
    strstripnewline(s->value);

    /* TODO: Handle multiple lanes with same setting */
    return 1;
  }

  /* Setting not found */
  s->value[0] = '\0';
  ERROR("Failed to read DMI setting");
  return 0;
}

static size_t dmidecode_get_setting(dmi_type type, dmi_setting *s) {
  FILE *output;
  char cmd[DMIDECODE_CMD_FMT_LEN];
  size_t rval;

  TRACE();

  if (!s || !s->name || !s->value) {
    ERROR("%s: NULL pointer", __func__);
    return 0;
  }

  ssnprintf(cmd, sizeof(cmd), DMIDECODE_CMD_FMT, type, s->name);
  DEBUG("dmidecode cmd=%s", cmd);

  output = popen(cmd, "r");
  if (!output) {
    ERROR("popen failed");
    return 0;
  }

  rval = dmidecode_parse_output(output, s);
  DEBUG("%s=%s", s->name, s->value);

  pclose(output);
  return rval;
}

static size_t dmi_get(dmi_reader *self, dmi_t *s) {
  DMI_FOR_EACH(dmidecode_get_setting, self, s, s->s_len, s->type,
               &s->settings[i]);
}

static size_t dmi_get_bulk(dmi_reader *self, dmi_t **s, size_t s_len) {
  DMI_FOR_EACH(self->get, self, s, s_len, self, s[i]);
}
