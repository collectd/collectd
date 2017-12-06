/**
 * collectd - src/configfile.c
 * Copyright (C) 2005-2011  Florian octo Forster
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
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#include "collectd.h"

#include "liboconfig/oconfig.h"

#include "common.h"
#include "configfile.h"
#include "filter_chain.h"
#include "plugin.h"
#include "types_list.h"

#if HAVE_WORDEXP_H
#include <wordexp.h>
#endif /* HAVE_WORDEXP_H */

#if HAVE_FNMATCH_H
#include <fnmatch.h>
#endif /* HAVE_FNMATCH_H */

#if HAVE_LIBGEN_H
#include <libgen.h>
#endif /* HAVE_LIBGEN_H */

#define ESCAPE_NULL(str) ((str) == NULL ? "(null)" : (str))

/*
 * Private types
 */
typedef struct cf_callback {
  const char *type;
  int (*callback)(const char *, const char *);
  const char **keys;
  int keys_num;
  plugin_ctx_t ctx;
  struct cf_callback *next;
} cf_callback_t;

typedef struct cf_complex_callback_s {
  char *type;
  int (*callback)(oconfig_item_t *);
  plugin_ctx_t ctx;
  struct cf_complex_callback_s *next;
} cf_complex_callback_t;

typedef struct cf_value_map_s {
  const char *key;
  int (*func)(oconfig_item_t *);
} cf_value_map_t;

typedef struct cf_global_option_s {
  const char *key;
  char *value;
  _Bool from_cli; /* value set from CLI */
  const char *def;
} cf_global_option_t;

/*
 * Prototypes of callback functions
 */
static int dispatch_value_typesdb(oconfig_item_t *ci);
static int dispatch_value_plugindir(oconfig_item_t *ci);
static int dispatch_loadplugin(oconfig_item_t *ci);
static int dispatch_block_plugin(oconfig_item_t *ci);

/*
 * Private variables
 */
static cf_callback_t *first_callback = NULL;
static cf_complex_callback_t *complex_callback_head = NULL;

static cf_value_map_t cf_value_map[] = {{"TypesDB", dispatch_value_typesdb},
                                        {"PluginDir", dispatch_value_plugindir},
                                        {"LoadPlugin", dispatch_loadplugin},
                                        {"Plugin", dispatch_block_plugin}};
static int cf_value_map_num = STATIC_ARRAY_SIZE(cf_value_map);

static cf_global_option_t cf_global_options[] = {
    {"BaseDir", NULL, 0, PKGLOCALSTATEDIR},
    {"PIDFile", NULL, 0, PIDFILE},
    {"Hostname", NULL, 0, NULL},
    {"FQDNLookup", NULL, 0, "true"},
    {"Interval", NULL, 0, NULL},
    {"ReadThreads", NULL, 0, "5"},
    {"WriteThreads", NULL, 0, "5"},
    {"WriteQueueLimitHigh", NULL, 0, NULL},
    {"WriteQueueLimitLow", NULL, 0, NULL},
    {"Timeout", NULL, 0, "2"},
    {"AutoLoadPlugin", NULL, 0, "false"},
    {"CollectInternalStats", NULL, 0, "false"},
    {"PreCacheChain", NULL, 0, "PreCache"},
    {"PostCacheChain", NULL, 0, "PostCache"},
    {"MaxReadInterval", NULL, 0, "86400"}};
static int cf_global_options_num = STATIC_ARRAY_SIZE(cf_global_options);

static int cf_default_typesdb = 1;

/*
 * Functions to handle register/unregister, search, and other plugin related
 * stuff
 */
static cf_callback_t *cf_search(const char *type) {
  cf_callback_t *cf_cb;

  if (type == NULL)
    return NULL;

  for (cf_cb = first_callback; cf_cb != NULL; cf_cb = cf_cb->next)
    if (strcasecmp(cf_cb->type, type) == 0)
      break;

  return cf_cb;
}

static int cf_dispatch(const char *type, const char *orig_key,
                       const char *orig_value) {
  cf_callback_t *cf_cb;
  plugin_ctx_t old_ctx;
  char *key;
  char *value;
  int ret;
  int i = 0;

  if (orig_key == NULL)
    return EINVAL;

  DEBUG("type = %s, key = %s, value = %s", ESCAPE_NULL(type), orig_key,
        ESCAPE_NULL(orig_value));

  if ((cf_cb = cf_search(type)) == NULL) {
    WARNING("Found a configuration for the `%s' plugin, but "
            "the plugin isn't loaded or didn't register "
            "a configuration callback.",
            type);
    return -1;
  }

  if ((key = strdup(orig_key)) == NULL)
    return 1;
  if ((value = strdup(orig_value)) == NULL) {
    free(key);
    return 2;
  }

  ret = -1;

  old_ctx = plugin_set_ctx(cf_cb->ctx);

  for (i = 0; i < cf_cb->keys_num; i++) {
    if ((cf_cb->keys[i] != NULL) && (strcasecmp(cf_cb->keys[i], key) == 0)) {
      ret = (*cf_cb->callback)(key, value);
      break;
    }
  }

  plugin_set_ctx(old_ctx);

  if (i >= cf_cb->keys_num)
    WARNING("Plugin `%s' did not register for value `%s'.", type, key);

  free(key);
  free(value);

  return ret;
} /* int cf_dispatch */

static int dispatch_global_option(const oconfig_item_t *ci) {
  if (ci->values_num != 1)
    return -1;
  if (ci->values[0].type == OCONFIG_TYPE_STRING)
    return global_option_set(ci->key, ci->values[0].value.string, 0);
  else if (ci->values[0].type == OCONFIG_TYPE_NUMBER) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%lf", ci->values[0].value.number);
    return global_option_set(ci->key, tmp, 0);
  } else if (ci->values[0].type == OCONFIG_TYPE_BOOLEAN) {
    if (ci->values[0].value.boolean)
      return global_option_set(ci->key, "true", 0);
    else
      return global_option_set(ci->key, "false", 0);
  }

  return -1;
} /* int dispatch_global_option */

static int dispatch_value_typesdb(oconfig_item_t *ci) {
  assert(strcasecmp(ci->key, "TypesDB") == 0);

  cf_default_typesdb = 0;

  if (ci->values_num < 1) {
    ERROR("configfile: `TypesDB' needs at least one argument.");
    return -1;
  }

  for (int i = 0; i < ci->values_num; ++i) {
    if (OCONFIG_TYPE_STRING != ci->values[i].type) {
      WARNING("configfile: TypesDB: Skipping %i. argument which "
              "is not a string.",
              i + 1);
      continue;
    }

    read_types_list(ci->values[i].value.string);
  }
  return 0;
} /* int dispatch_value_typesdb */

static int dispatch_value_plugindir(oconfig_item_t *ci) {
  assert(strcasecmp(ci->key, "PluginDir") == 0);

  if (ci->values_num != 1)
    return -1;
  if (ci->values[0].type != OCONFIG_TYPE_STRING)
    return -1;

  plugin_set_dir(ci->values[0].value.string);
  return 0;
}

static int dispatch_loadplugin(oconfig_item_t *ci) {
  const char *name;
  _Bool global = 0;
  plugin_ctx_t ctx = {0};
  plugin_ctx_t old_ctx;
  int ret_val;

  assert(strcasecmp(ci->key, "LoadPlugin") == 0);

  if (ci->values_num != 1)
    return -1;
  if (ci->values[0].type != OCONFIG_TYPE_STRING)
    return -1;

  name = ci->values[0].value.string;
  if (strcmp("libvirt", name) == 0)
    name = "virt";

  /* default to the global interval set before loading this plugin */
  ctx.interval = cf_get_default_interval();
  ctx.flush_interval = 0;
  ctx.flush_timeout = 0;

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Globals", child->key) == 0)
      cf_util_get_boolean(child, &global);
    else if (strcasecmp("Interval", child->key) == 0)
      cf_util_get_cdtime(child, &ctx.interval);
    else if (strcasecmp("FlushInterval", child->key) == 0)
      cf_util_get_cdtime(child, &ctx.flush_interval);
    else if (strcasecmp("FlushTimeout", child->key) == 0)
      cf_util_get_cdtime(child, &ctx.flush_timeout);
    else {
      WARNING("Ignoring unknown LoadPlugin option \"%s\" "
              "for plugin \"%s\"",
              child->key, ci->values[0].value.string);
    }
  }

  old_ctx = plugin_set_ctx(ctx);
  ret_val = plugin_load(name, global);
  /* reset to the "global" context */
  plugin_set_ctx(old_ctx);

  return ret_val;
} /* int dispatch_value_loadplugin */

static int dispatch_value_plugin(const char *plugin, oconfig_item_t *ci) {
  char buffer[4096];
  char *buffer_ptr;
  int buffer_free;

  buffer_ptr = buffer;
  buffer_free = sizeof(buffer);

  for (int i = 0; i < ci->values_num; i++) {
    int status = -1;

    if (ci->values[i].type == OCONFIG_TYPE_STRING)
      status =
          snprintf(buffer_ptr, buffer_free, " %s", ci->values[i].value.string);
    else if (ci->values[i].type == OCONFIG_TYPE_NUMBER)
      status =
          snprintf(buffer_ptr, buffer_free, " %lf", ci->values[i].value.number);
    else if (ci->values[i].type == OCONFIG_TYPE_BOOLEAN)
      status = snprintf(buffer_ptr, buffer_free, " %s",
                        ci->values[i].value.boolean ? "true" : "false");

    if ((status < 0) || (status >= buffer_free))
      return -1;
    buffer_free -= status;
    buffer_ptr += status;
  }
  /* skip the initial space */
  buffer_ptr = buffer + 1;

  return cf_dispatch(plugin, ci->key, buffer_ptr);
} /* int dispatch_value_plugin */

static int dispatch_value(oconfig_item_t *ci) {
  int ret = 0;

  for (int i = 0; i < cf_value_map_num; i++)
    if (strcasecmp(cf_value_map[i].key, ci->key) == 0) {
      ret = cf_value_map[i].func(ci);
      break;
    }

  for (int i = 0; i < cf_global_options_num; i++)
    if (strcasecmp(cf_global_options[i].key, ci->key) == 0) {
      ret = dispatch_global_option(ci);
      break;
    }

  return ret;
} /* int dispatch_value */

static int dispatch_block_plugin(oconfig_item_t *ci) {
  const char *name;

  if (strcasecmp(ci->key, "Plugin") != 0)
    return -1;
  if (ci->values_num < 1)
    return -1;
  if (ci->values[0].type != OCONFIG_TYPE_STRING)
    return -1;

  name = ci->values[0].value.string;
  if (strcmp("libvirt", name) == 0) {
    /* TODO(octo): Remove this legacy. */
    WARNING("The \"libvirt\" plugin has been renamed to \"virt\" to avoid "
            "problems with the build system. "
            "Your configuration is still using the old name. "
            "Please change it to use \"virt\" as soon as possible. "
            "This compatibility code will go away eventually.");
    name = "virt";
  }

  if (IS_TRUE(global_option_get("AutoLoadPlugin"))) {
    plugin_ctx_t ctx = {0};
    plugin_ctx_t old_ctx;
    int status;

    /* default to the global interval set before loading this plugin */
    ctx.interval = cf_get_default_interval();

    old_ctx = plugin_set_ctx(ctx);
    status = plugin_load(name, /* flags = */ 0);
    /* reset to the "global" context */
    plugin_set_ctx(old_ctx);

    if (status != 0) {
      ERROR("Automatically loading plugin \"%s\" failed "
            "with status %i.",
            name, status);
      return status;
    }
  }

  /* Check for a complex callback first */
  for (cf_complex_callback_t *cb = complex_callback_head; cb != NULL;
       cb = cb->next) {
    if (strcasecmp(name, cb->type) == 0) {
      plugin_ctx_t old_ctx;
      int ret_val;

      old_ctx = plugin_set_ctx(cb->ctx);
      ret_val = (cb->callback(ci));
      plugin_set_ctx(old_ctx);
      return ret_val;
    }
  }

  /* Hm, no complex plugin found. Dispatch the values one by one */
  for (int i = 0; i < ci->children_num; i++) {
    if (ci->children[i].children == NULL)
      dispatch_value_plugin(name, ci->children + i);
    else {
      WARNING("There is a `%s' block within the "
              "configuration for the %s plugin. "
              "The plugin either only expects "
              "\"simple\" configuration statements "
              "or wasn't loaded using `LoadPlugin'."
              " Please check your configuration.",
              ci->children[i].key, name);
    }
  }

  return 0;
}

static int dispatch_block(oconfig_item_t *ci) {
  if (strcasecmp(ci->key, "LoadPlugin") == 0)
    return dispatch_loadplugin(ci);
  else if (strcasecmp(ci->key, "Plugin") == 0)
    return dispatch_block_plugin(ci);
  else if (strcasecmp(ci->key, "Chain") == 0)
    return fc_configure(ci);

  return 0;
}

static int cf_ci_replace_child(oconfig_item_t *dst, oconfig_item_t *src,
                               int offset) {
  oconfig_item_t *temp;

  assert(offset >= 0);
  assert(dst->children_num > offset);

  /* Free the memory used by the replaced child. Usually that's the
   * `Include "blah"' statement. */
  temp = dst->children + offset;
  for (int i = 0; i < temp->values_num; i++) {
    if (temp->values[i].type == OCONFIG_TYPE_STRING) {
      sfree(temp->values[i].value.string);
    }
  }
  sfree(temp->values);
  temp = NULL;

  /* If (src->children_num == 0) the array size is decreased. If offset
   * is _not_ the last element, (offset < (dst->children_num - 1)), then
   * we need to move the trailing elements before resizing the array. */
  if ((src->children_num == 0) && (offset < (dst->children_num - 1))) {
    int nmemb = dst->children_num - (offset + 1);
    memmove(dst->children + offset, dst->children + offset + 1,
            sizeof(oconfig_item_t) * nmemb);
  }

  /* Resize the memory containing the children to be big enough to hold
   * all children. */
  if (dst->children_num + src->children_num - 1 == 0) {
    dst->children_num = 0;
    return 0;
  }

  temp = realloc(dst->children,
                 sizeof(oconfig_item_t) *
                     (dst->children_num + src->children_num - 1));
  if (temp == NULL) {
    ERROR("configfile: realloc failed.");
    return -1;
  }
  dst->children = temp;

  /* If there are children behind the include statement, and they have
   * not yet been moved because (src->children_num == 0), then move them
   * to the end of the list, so that the new children have room before
   * them. */
  if ((src->children_num > 0) && ((dst->children_num - (offset + 1)) > 0)) {
    int nmemb = dst->children_num - (offset + 1);
    int old_offset = offset + 1;
    int new_offset = offset + src->children_num;

    memmove(dst->children + new_offset, dst->children + old_offset,
            sizeof(oconfig_item_t) * nmemb);
  }

  /* Last but not least: If there are new children, copy them to the
   * memory reserved for them. */
  if (src->children_num > 0) {
    memcpy(dst->children + offset, src->children,
           sizeof(oconfig_item_t) * src->children_num);
  }

  /* Update the number of children. */
  dst->children_num += (src->children_num - 1);

  return 0;
} /* int cf_ci_replace_child */

static int cf_ci_append_children(oconfig_item_t *dst, oconfig_item_t *src) {
  oconfig_item_t *temp;

  if ((src == NULL) || (src->children_num == 0))
    return 0;

  temp =
      realloc(dst->children,
              sizeof(oconfig_item_t) * (dst->children_num + src->children_num));
  if (temp == NULL) {
    ERROR("configfile: realloc failed.");
    return -1;
  }
  dst->children = temp;

  memcpy(dst->children + dst->children_num, src->children,
         sizeof(oconfig_item_t) * src->children_num);
  dst->children_num += src->children_num;

  return 0;
} /* int cf_ci_append_children */

#define CF_MAX_DEPTH 8
static oconfig_item_t *cf_read_generic(const char *path, const char *pattern,
                                       int depth);

static int cf_include_all(oconfig_item_t *root, int depth) {
  for (int i = 0; i < root->children_num; i++) {
    oconfig_item_t *new;
    oconfig_item_t *old;

    char *pattern = NULL;

    if (strcasecmp(root->children[i].key, "Include") != 0)
      continue;

    old = root->children + i;

    if ((old->values_num != 1) ||
        (old->values[0].type != OCONFIG_TYPE_STRING)) {
      ERROR("configfile: `Include' needs exactly one string argument.");
      continue;
    }

    for (int j = 0; j < old->children_num; ++j) {
      oconfig_item_t *child = old->children + j;

      if (strcasecmp(child->key, "Filter") == 0)
        cf_util_get_string(child, &pattern);
      else
        ERROR("configfile: Option `%s' not allowed in <Include> block.",
              child->key);
    }

    new = cf_read_generic(old->values[0].value.string, pattern, depth + 1);
    sfree(pattern);

    if (new == NULL)
      return -1;

    /* Now replace the i'th child in `root' with `new'. */
    if (cf_ci_replace_child(root, new, i) < 0) {
      sfree(new->values);
      sfree(new);
      return -1;
    }

    /* ... and go back to the new i'th child. */
    --i;

    sfree(new->values);
    sfree(new);
  } /* for (i = 0; i < root->children_num; i++) */

  return 0;
} /* int cf_include_all */

static oconfig_item_t *cf_read_file(const char *file, const char *pattern,
                                    int depth) {
  oconfig_item_t *root;
  int status;

  assert(depth < CF_MAX_DEPTH);

  if (pattern != NULL) {
#if HAVE_FNMATCH_H && HAVE_LIBGEN_H
    char *tmp = sstrdup(file);
    char *filename = basename(tmp);

    if ((filename != NULL) && (fnmatch(pattern, filename, 0) != 0)) {
      DEBUG("configfile: Not including `%s' because it "
            "does not match pattern `%s'.",
            filename, pattern);
      free(tmp);
      return NULL;
    }

    free(tmp);
#else
    ERROR("configfile: Cannot apply pattern filter '%s' "
          "to file '%s': functions basename() and / or "
          "fnmatch() not available.",
          pattern, file);
#endif /* HAVE_FNMATCH_H && HAVE_LIBGEN_H */
  }

  root = oconfig_parse_file(file);
  if (root == NULL) {
    ERROR("configfile: Cannot read file `%s'.", file);
    return NULL;
  }

  status = cf_include_all(root, depth);
  if (status != 0) {
    oconfig_free(root);
    return NULL;
  }

  return root;
} /* oconfig_item_t *cf_read_file */

static int cf_compare_string(const void *p1, const void *p2) {
  return strcmp(*(const char **)p1, *(const char **)p2);
}

static oconfig_item_t *cf_read_dir(const char *dir, const char *pattern,
                                   int depth) {
  oconfig_item_t *root = NULL;
  DIR *dh;
  struct dirent *de;
  char **filenames = NULL;
  int filenames_num = 0;
  int status;

  assert(depth < CF_MAX_DEPTH);

  dh = opendir(dir);
  if (dh == NULL) {
    ERROR("configfile: opendir failed: %s", STRERRNO);
    return NULL;
  }

  root = calloc(1, sizeof(*root));
  if (root == NULL) {
    ERROR("configfile: calloc failed.");
    closedir(dh);
    return NULL;
  }

  while ((de = readdir(dh)) != NULL) {
    char name[1024];
    char **tmp;

    if ((de->d_name[0] == '.') || (de->d_name[0] == 0))
      continue;

    status = snprintf(name, sizeof(name), "%s/%s", dir, de->d_name);
    if ((status < 0) || ((size_t)status >= sizeof(name))) {
      ERROR("configfile: Not including `%s/%s' because its"
            " name is too long.",
            dir, de->d_name);
      closedir(dh);
      for (int i = 0; i < filenames_num; ++i)
        free(filenames[i]);
      free(filenames);
      free(root);
      return NULL;
    }

    ++filenames_num;
    tmp = realloc(filenames, filenames_num * sizeof(*filenames));
    if (tmp == NULL) {
      ERROR("configfile: realloc failed.");
      closedir(dh);
      for (int i = 0; i < filenames_num - 1; ++i)
        free(filenames[i]);
      free(filenames);
      free(root);
      return NULL;
    }
    filenames = tmp;

    filenames[filenames_num - 1] = sstrdup(name);
  }

  if (filenames == NULL) {
    closedir(dh);
    return root;
  }

  qsort((void *)filenames, filenames_num, sizeof(*filenames),
        cf_compare_string);

  for (int i = 0; i < filenames_num; ++i) {
    oconfig_item_t *temp;
    char *name = filenames[i];

    temp = cf_read_generic(name, pattern, depth);
    if (temp == NULL) {
      /* An error should already have been reported. */
      sfree(name);
      continue;
    }

    cf_ci_append_children(root, temp);
    sfree(temp->children);
    sfree(temp);

    free(name);
  }

  closedir(dh);
  free(filenames);
  return root;
} /* oconfig_item_t *cf_read_dir */

/*
 * cf_read_generic
 *
 * Path is stat'ed and either cf_read_file or cf_read_dir is called
 * accordingly.
 *
 * There are two versions of this function: If `wordexp' exists shell wildcards
 * will be expanded and the function will include all matches found. If
 * `wordexp' (or, more precisely, its header file) is not available the
 * simpler function is used which does not do any such expansion.
 */
#if HAVE_WORDEXP_H
static oconfig_item_t *cf_read_generic(const char *path, const char *pattern,
                                       int depth) {
  oconfig_item_t *root = NULL;
  int status;
  const char *path_ptr;
  wordexp_t we;

  if (depth >= CF_MAX_DEPTH) {
    ERROR("configfile: Not including `%s' because the maximum "
          "nesting depth has been reached.",
          path);
    return NULL;
  }

  status = wordexp(path, &we, WRDE_NOCMD);
  if (status != 0) {
    ERROR("configfile: wordexp (%s) failed.", path);
    return NULL;
  }

  root = calloc(1, sizeof(*root));
  if (root == NULL) {
    ERROR("configfile: calloc failed.");
    return NULL;
  }

  /* wordexp() might return a sorted list already. That's not
   * documented though, so let's make sure we get what we want. */
  qsort((void *)we.we_wordv, we.we_wordc, sizeof(*we.we_wordv),
        cf_compare_string);

  for (size_t i = 0; i < we.we_wordc; i++) {
    oconfig_item_t *temp;
    struct stat statbuf;

    path_ptr = we.we_wordv[i];

    status = stat(path_ptr, &statbuf);
    if (status != 0) {
      WARNING("configfile: stat (%s) failed: %s", path_ptr, STRERRNO);
      continue;
    }

    if (S_ISREG(statbuf.st_mode))
      temp = cf_read_file(path_ptr, pattern, depth);
    else if (S_ISDIR(statbuf.st_mode))
      temp = cf_read_dir(path_ptr, pattern, depth);
    else {
      WARNING("configfile: %s is neither a file nor a "
              "directory.",
              path);
      continue;
    }

    if (temp == NULL) {
      oconfig_free(root);
      return NULL;
    }

    cf_ci_append_children(root, temp);
    sfree(temp->children);
    sfree(temp);
  }

  wordfree(&we);

  return root;
} /* oconfig_item_t *cf_read_generic */
/* #endif HAVE_WORDEXP_H */

#else  /* if !HAVE_WORDEXP_H */
static oconfig_item_t *cf_read_generic(const char *path, const char *pattern,
                                       int depth) {
  struct stat statbuf;
  int status;

  if (depth >= CF_MAX_DEPTH) {
    ERROR("configfile: Not including `%s' because the maximum "
          "nesting depth has been reached.",
          path);
    return NULL;
  }

  status = stat(path, &statbuf);
  if (status != 0) {
    ERROR("configfile: stat (%s) failed: %s", path, STRERRNO);
    return NULL;
  }

  if (S_ISREG(statbuf.st_mode))
    return cf_read_file(path, pattern, depth);
  else if (S_ISDIR(statbuf.st_mode))
    return cf_read_dir(path, pattern, depth);

  ERROR("configfile: %s is neither a file nor a directory.", path);
  return NULL;
} /* oconfig_item_t *cf_read_generic */
#endif /* !HAVE_WORDEXP_H */

/*
 * Public functions
 */
int global_option_set(const char *option, const char *value, _Bool from_cli) {
  int i;
  DEBUG("option = %s; value = %s;", option, value);

  for (i = 0; i < cf_global_options_num; i++)
    if (strcasecmp(cf_global_options[i].key, option) == 0)
      break;

  if (i >= cf_global_options_num) {
    ERROR("configfile: Cannot set unknown global option `%s'.", option);
    return -1;
  }

  if (cf_global_options[i].from_cli && (!from_cli)) {
    DEBUG("configfile: Ignoring %s `%s' option because "
          "it was overriden by a command-line option.",
          option, value);
    return 0;
  }

  sfree(cf_global_options[i].value);

  if (value != NULL)
    cf_global_options[i].value = strdup(value);
  else
    cf_global_options[i].value = NULL;

  cf_global_options[i].from_cli = from_cli;

  return 0;
}

const char *global_option_get(const char *option) {
  int i;
  for (i = 0; i < cf_global_options_num; i++)
    if (strcasecmp(cf_global_options[i].key, option) == 0)
      break;

  if (i >= cf_global_options_num) {
    ERROR("configfile: Cannot get unknown global option `%s'.", option);
    return NULL;
  }

  return (cf_global_options[i].value != NULL) ? cf_global_options[i].value
                                              : cf_global_options[i].def;
} /* char *global_option_get */

long global_option_get_long(const char *option, long default_value) {
  const char *str;
  long value;

  str = global_option_get(option);
  if (NULL == str)
    return default_value;

  errno = 0;
  value = strtol(str, /* endptr = */ NULL, /* base = */ 0);
  if (errno != 0)
    return default_value;

  return value;
} /* char *global_option_get_long */

cdtime_t global_option_get_time(const char *name, cdtime_t def) /* {{{ */
{
  char const *optstr;
  char *endptr = NULL;
  double v;

  optstr = global_option_get(name);
  if (optstr == NULL)
    return def;

  errno = 0;
  v = strtod(optstr, &endptr);
  if ((endptr == NULL) || (*endptr != 0) || (errno != 0))
    return def;
  else if (v <= 0.0)
    return def;

  return DOUBLE_TO_CDTIME_T(v);
} /* }}} cdtime_t global_option_get_time */

cdtime_t cf_get_default_interval(void) {
  return global_option_get_time("Interval",
                                DOUBLE_TO_CDTIME_T(COLLECTD_DEFAULT_INTERVAL));
}

void cf_unregister(const char *type) {
  for (cf_callback_t *prev = NULL, *this = first_callback; this != NULL;
       prev = this, this = this->next)
    if (strcasecmp(this->type, type) == 0) {
      if (prev == NULL)
        first_callback = this->next;
      else
        prev->next = this->next;

      free(this);
      break;
    }
} /* void cf_unregister */

void cf_unregister_complex(const char *type) {
  for (cf_complex_callback_t *prev = NULL, *this = complex_callback_head;
       this != NULL; prev = this, this = this->next)
    if (strcasecmp(this->type, type) == 0) {
      if (prev == NULL)
        complex_callback_head = this->next;
      else
        prev->next = this->next;

      sfree(this->type);
      sfree(this);
      break;
    }
} /* void cf_unregister */

void cf_register(const char *type, int (*callback)(const char *, const char *),
                 const char **keys, int keys_num) {
  cf_callback_t *cf_cb;

  /* Remove this module from the list, if it already exists */
  cf_unregister(type);

  /* This pointer will be free'd in `cf_unregister' */
  if ((cf_cb = malloc(sizeof(*cf_cb))) == NULL)
    return;

  cf_cb->type = type;
  cf_cb->callback = callback;
  cf_cb->keys = keys;
  cf_cb->keys_num = keys_num;
  cf_cb->ctx = plugin_get_ctx();

  cf_cb->next = first_callback;
  first_callback = cf_cb;
} /* void cf_register */

int cf_register_complex(const char *type, int (*callback)(oconfig_item_t *)) {
  cf_complex_callback_t *new;

  new = malloc(sizeof(*new));
  if (new == NULL)
    return -1;

  new->type = strdup(type);
  if (new->type == NULL) {
    sfree(new);
    return -1;
  }

  new->callback = callback;
  new->next = NULL;

  new->ctx = plugin_get_ctx();

  if (complex_callback_head == NULL) {
    complex_callback_head = new;
  } else {
    cf_complex_callback_t *last = complex_callback_head;
    while (last->next != NULL)
      last = last->next;
    last->next = new;
  }

  return 0;
} /* int cf_register_complex */

int cf_read(const char *filename) {
  oconfig_item_t *conf;
  int ret = 0;

  conf = cf_read_generic(filename, /* pattern = */ NULL, /* depth = */ 0);
  if (conf == NULL) {
    ERROR("Unable to read config file %s.", filename);
    return -1;
  } else if (conf->children_num == 0) {
    ERROR("Configuration file %s is empty.", filename);
    oconfig_free(conf);
    return -1;
  }

  for (int i = 0; i < conf->children_num; i++) {
    if (conf->children[i].children == NULL) {
      if (dispatch_value(conf->children + i) != 0)
        ret = -1;
    } else {
      if (dispatch_block(conf->children + i) != 0)
        ret = -1;
    }
  }

  oconfig_free(conf);

  /* Read the default types.db if no `TypesDB' option was given. */
  if (cf_default_typesdb) {
    if (read_types_list(PKGDATADIR "/types.db") != 0)
      ret = -1;
  }

  return ret;

} /* int cf_read */

/* Assures the config option is a string, duplicates it and returns the copy in
 * "ret_string". If necessary "*ret_string" is freed first. Returns zero upon
 * success. */
int cf_util_get_string(const oconfig_item_t *ci, char **ret_string) /* {{{ */
{
  char *string;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("cf_util_get_string: The %s option requires "
          "exactly one string argument.",
          ci->key);
    return -1;
  }

  string = strdup(ci->values[0].value.string);
  if (string == NULL)
    return -1;

  if (*ret_string != NULL)
    sfree(*ret_string);
  *ret_string = string;

  return 0;
} /* }}} int cf_util_get_string */

/* Assures the config option is a string and copies it to the provided buffer.
 * Assures null-termination. */
int cf_util_get_string_buffer(const oconfig_item_t *ci, char *buffer, /* {{{ */
                              size_t buffer_size) {
  if ((ci == NULL) || (buffer == NULL) || (buffer_size < 1))
    return EINVAL;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("cf_util_get_string_buffer: The %s option requires "
          "exactly one string argument.",
          ci->key);
    return -1;
  }

  strncpy(buffer, ci->values[0].value.string, buffer_size);
  buffer[buffer_size - 1] = 0;

  return 0;
} /* }}} int cf_util_get_string_buffer */

/* Assures the config option is a number and returns it as an int. */
int cf_util_get_int(const oconfig_item_t *ci, int *ret_value) /* {{{ */
{
  if ((ci == NULL) || (ret_value == NULL))
    return EINVAL;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)) {
    ERROR("cf_util_get_int: The %s option requires "
          "exactly one numeric argument.",
          ci->key);
    return -1;
  }

  *ret_value = (int)ci->values[0].value.number;

  return 0;
} /* }}} int cf_util_get_int */

int cf_util_get_double(const oconfig_item_t *ci, double *ret_value) /* {{{ */
{
  if ((ci == NULL) || (ret_value == NULL))
    return EINVAL;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)) {
    ERROR("cf_util_get_double: The %s option requires "
          "exactly one numeric argument.",
          ci->key);
    return -1;
  }

  *ret_value = ci->values[0].value.number;

  return 0;
} /* }}} int cf_util_get_double */

int cf_util_get_boolean(const oconfig_item_t *ci, _Bool *ret_bool) /* {{{ */
{
  if ((ci == NULL) || (ret_bool == NULL))
    return EINVAL;

  if ((ci->values_num != 1) || ((ci->values[0].type != OCONFIG_TYPE_BOOLEAN) &&
                                (ci->values[0].type != OCONFIG_TYPE_STRING))) {
    ERROR("cf_util_get_boolean: The %s option requires "
          "exactly one boolean argument.",
          ci->key);
    return -1;
  }

  switch (ci->values[0].type) {
  case OCONFIG_TYPE_BOOLEAN:
    *ret_bool = ci->values[0].value.boolean ? 1 : 0;
    break;
  case OCONFIG_TYPE_STRING:
    WARNING("cf_util_get_boolean: Using string value `%s' for boolean option "
            "`%s' is deprecated and will be removed in future releases. "
            "Use unquoted true or false instead.",
            ci->values[0].value.string, ci->key);

    if (IS_TRUE(ci->values[0].value.string))
      *ret_bool = 1;
    else if (IS_FALSE(ci->values[0].value.string))
      *ret_bool = 0;
    else {
      ERROR("cf_util_get_boolean: Cannot parse string value `%s' of the `%s' "
            "option as a boolean value.",
            ci->values[0].value.string, ci->key);
      return -1;
    }
    break;
  }

  return 0;
} /* }}} int cf_util_get_boolean */

int cf_util_get_flag(const oconfig_item_t *ci, /* {{{ */
                     unsigned int *ret_value, unsigned int flag) {
  int status;
  _Bool b;

  if (ret_value == NULL)
    return EINVAL;

  b = 0;
  status = cf_util_get_boolean(ci, &b);
  if (status != 0)
    return status;

  if (b) {
    *ret_value |= flag;
  } else {
    *ret_value &= ~flag;
  }

  return 0;
} /* }}} int cf_util_get_flag */

/* Assures that the config option is a string or a number if the correct range
 * of 1-65535. The string is then converted to a port number using
 * `service_name_to_port_number' and returned.
 * Returns the port number in the range [1-65535] or less than zero upon
 * failure. */
int cf_util_get_port_number(const oconfig_item_t *ci) /* {{{ */
{
  int tmp;

  if ((ci->values_num != 1) || ((ci->values[0].type != OCONFIG_TYPE_STRING) &&
                                (ci->values[0].type != OCONFIG_TYPE_NUMBER))) {
    ERROR("cf_util_get_port_number: The \"%s\" option requires "
          "exactly one string argument.",
          ci->key);
    return -1;
  }

  if (ci->values[0].type == OCONFIG_TYPE_STRING)
    return service_name_to_port_number(ci->values[0].value.string);

  assert(ci->values[0].type == OCONFIG_TYPE_NUMBER);
  tmp = (int)(ci->values[0].value.number + 0.5);
  if ((tmp < 1) || (tmp > 65535)) {
    ERROR("cf_util_get_port_number: The \"%s\" option requires "
          "a service name or a port number. The number "
          "you specified, %i, is not in the valid "
          "range of 1-65535.",
          ci->key, tmp);
    return -1;
  }

  return tmp;
} /* }}} int cf_util_get_port_number */

int cf_util_get_service(const oconfig_item_t *ci, char **ret_string) /* {{{ */
{
  int port;
  char *service;
  int status;

  if (ci->values_num != 1) {
    ERROR("cf_util_get_service: The %s option requires exactly "
          "one argument.",
          ci->key);
    return -1;
  }

  if (ci->values[0].type == OCONFIG_TYPE_STRING)
    return cf_util_get_string(ci, ret_string);
  if (ci->values[0].type != OCONFIG_TYPE_NUMBER) {
    ERROR("cf_util_get_service: The %s option requires "
          "exactly one string or numeric argument.",
          ci->key);
  }

  port = 0;
  status = cf_util_get_int(ci, &port);
  if (status != 0)
    return status;
  else if ((port < 1) || (port > 65535)) {
    ERROR("cf_util_get_service: The port number given "
          "for the %s option is out of "
          "range (%i).",
          ci->key, port);
    return -1;
  }

  service = malloc(6);
  if (service == NULL) {
    ERROR("cf_util_get_service: Out of memory.");
    return -1;
  }
  snprintf(service, 6, "%i", port);

  sfree(*ret_string);
  *ret_string = service;

  return 0;
} /* }}} int cf_util_get_service */

int cf_util_get_cdtime(const oconfig_item_t *ci, cdtime_t *ret_value) /* {{{ */
{
  if ((ci == NULL) || (ret_value == NULL))
    return EINVAL;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)) {
    ERROR("cf_util_get_cdtime: The %s option requires "
          "exactly one numeric argument.",
          ci->key);
    return -1;
  }

  if (ci->values[0].value.number < 0.0) {
    ERROR("cf_util_get_cdtime: The numeric argument of the %s "
          "option must not be negative.",
          ci->key);
    return -1;
  }

  *ret_value = DOUBLE_TO_CDTIME_T(ci->values[0].value.number);

  return 0;
} /* }}} int cf_util_get_cdtime */
