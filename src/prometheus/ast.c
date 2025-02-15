#include "plugin.h"
#include "utils/common/common.h"

#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *PR_METRIC_SUFFIXES[] = {"", "_bucket", "_count", "_sum"};

void pr_delete_label_list(pr_label_t *label_list) {
  if (!label_list) {
    return;
  }
  free(label_list->name);
  free(label_list->value);
  pr_delete_label_list(label_list->next);
  free(label_list);
}

void pr_delete_metric(pr_metric_t *metric) {
  pr_delete_label_list(metric->labels);
  free(metric->timestamp);
  free(metric);
}

void pr_delete_metric_list(pr_metric_t *metric_list) {
  if (!metric_list) {
    return;
  }
  pr_delete_metric_list(metric_list->next);
  pr_delete_metric(metric_list);
}

void pr_delete_metric_family(pr_metric_family_t *metric_family) {
  free(metric_family->name);
  free(metric_family->help);
  pr_delete_metric_list(metric_family->metric_list);
  free(metric_family);
}

void pr_delete_comment_entry(pr_comment_entry_t *comment) {
  free(comment->text);
  free(comment);
}

void pr_delete_item(pr_item_t *item) {
  switch (item->tp) {
  case (PR_METRIC_FAMILY_ITEM): {
    pr_delete_metric_family(item->body.metric_family);
    break;
  }
  case (PR_COMMENT_ITEM): {
    pr_delete_comment_entry(item->body.comment);
    break;
  }
  }
  free(item);
}

void pr_delete_item_list(pr_item_list_t *item_list) {
  pr_item_t *cur_item = item_list->begin;
  while (cur_item) {
    pr_item_t *next_item = cur_item->next;
    pr_delete_item(cur_item);
    cur_item = next_item;
  }
  free(item_list);
}

void pr_delete_metric_entry(pr_metric_entry_t *metric) {
  free(metric->name);
  pr_delete_label_list(metric->labels);
  free(metric->timestamp);
  free(metric);
}

void pr_delete_type_entry(pr_type_entry_t *type) {
  free(type->name);
  free(type);
}

void pr_delete_help_entry(pr_help_entry_t *help) {
  free(help->name);
  free(help->hint);
  free(help);
}

void pr_delete_entry(pr_entry_t *entry) {
  switch (entry->tp) {
  case (PR_METRIC_ENTRY): {
    pr_delete_metric_entry(entry->body.metric);
    break;
  }
  case (PR_COMMENT_ENTRY): {
    pr_delete_comment_entry(entry->body.comment);
    break;
  }
  case (PR_TYPE_ENTRY): {
    pr_delete_type_entry(entry->body.type);
    break;
  }
  case (PR_HELP_ENTRY): {
    pr_delete_help_entry(entry->body.help);
    break;
  }
  }
  free(entry);
}

pr_label_t *pr_create_label(char *name, char *value) {
  pr_label_t *label = smalloc(sizeof(*label));
  label->name = name;
  label->value = value;
  label->next = NULL;
  return label;
}

pr_timestamp_t *pr_create_empty_timestamp(void) {
  pr_timestamp_t *timestamp = smalloc(sizeof(*timestamp));
  timestamp->has_value = false;
  return timestamp;
}

pr_timestamp_t *pr_create_value_timestamp(int64_t value) {
  pr_timestamp_t *timestamp = smalloc(sizeof(*timestamp));
  timestamp->has_value = true;
  timestamp->value = value;
  return timestamp;
}

pr_label_t *pr_add_label_to_list(pr_label_t *list, pr_label_t *label) {
  if (!list) {
    return label;
  }
  label->next = list;
  return label;
}

pr_entry_t *pr_create_entry_from_metric(pr_metric_entry_t *metric) {
  pr_entry_t *entry = smalloc(sizeof(*entry));
  entry->tp = PR_METRIC_ENTRY;
  entry->body.metric = metric;
  return entry;
}

pr_entry_t *pr_create_entry_from_comment(pr_comment_entry_t *comment) {
  pr_entry_t *entry = smalloc(sizeof(*entry));
  entry->tp = PR_COMMENT_ENTRY;
  entry->body.comment = comment;
  return entry;
}

pr_entry_t *pr_create_entry_from_type(pr_type_entry_t *type) {
  pr_entry_t *entry = smalloc(sizeof(*entry));
  entry->tp = PR_TYPE_ENTRY;
  entry->body.type = type;
  return entry;
}

pr_entry_t *pr_create_entry_from_help(pr_help_entry_t *help) {
  pr_entry_t *entry = smalloc(sizeof(*entry));
  entry->tp = PR_HELP_ENTRY;
  entry->body.help = help;
  return entry;
}

pr_metric_entry_t *pr_create_metric_entry(char *name, pr_label_t *labels,
                                          double value,
                                          pr_timestamp_t *timestamp) {
  pr_metric_entry_t *metric = smalloc(sizeof(*metric));
  metric->name = name;
  metric->labels = labels;
  metric->value = value;
  metric->timestamp = timestamp;
  return metric;
}

pr_comment_entry_t *pr_create_comment_entry(char *text) {
  pr_comment_entry_t *comment = smalloc(sizeof(*comment));
  comment->text = text;
  return comment;
}

pr_type_entry_t *pr_create_type_entry(char *name, pr_metric_type_t tp) {
  pr_type_entry_t *type = smalloc(sizeof(*type));
  type->name = name;
  type->tp = tp;
  return type;
}

pr_help_entry_t *pr_create_help_entry(char *name, char *hint) {
  pr_help_entry_t *help = smalloc(sizeof(*help));
  help->name = name;
  help->hint = hint;
  return help;
}

pr_item_list_t *pr_create_item_list(void) {
  pr_item_list_t *item_list = smalloc(sizeof(*item_list));
  item_list->begin = NULL;
  return item_list;
}

pr_item_t *pr_create_metric_family_item(void) {
  pr_item_t *item = smalloc(sizeof(*item));
  memset(item, 0, sizeof(*item));
  pr_metric_family_t *metric_family = smalloc(sizeof(*metric_family));
  metric_family->name = NULL;
  metric_family->help = NULL;
  metric_family->tp = PR_UNTYPED;
  metric_family->metric_list = NULL;
  item->tp = PR_METRIC_FAMILY_ITEM;
  item->body.metric_family = metric_family;
  item->next = NULL;
  return item;
}

pr_item_t *pr_create_comment_item(char *text) {
  pr_item_t *item = smalloc(sizeof(*item));
  memset(item, 0, sizeof(*item));
  pr_comment_entry_t *comment = smalloc(sizeof(*comment));
  memset(comment, 0, sizeof(*comment));
  comment->text = sstrdup(text);
  item->tp = PR_COMMENT_ITEM;
  item->body.comment = comment;
  item->next = NULL;
  return item;
}

char *pr_get_cur_family_name(pr_item_list_t *item_list) {
  pr_item_t *item = item_list->begin;
  if (item && item->tp == PR_METRIC_FAMILY_ITEM) {
    return item->body.metric_family->name;
  }
  return NULL;
}

void pr_add_item_to_item_list(pr_item_list_t *item_list, pr_item_t *item) {
  item->next = item_list->begin;
  item_list->begin = item;
}

pr_label_t *pr_copy_label_list(pr_label_t *label_list) {
  if (!label_list) {
    return NULL;
  }
  pr_label_t *label_list_copy = smalloc(sizeof(*label_list_copy));
  memset(label_list_copy, 0, sizeof(*label_list_copy));
  label_list_copy->name = sstrdup(label_list->name);
  label_list_copy->value = sstrdup(label_list->value);
  label_list_copy->next = pr_copy_label_list(label_list->next);
  return label_list_copy;
}

pr_timestamp_t *pr_copy_timestamp(pr_timestamp_t *timestamp) {
  pr_timestamp_t *timestamp_copy = smalloc(sizeof(*timestamp_copy));
  timestamp_copy->has_value = timestamp->has_value;
  timestamp_copy->value = timestamp->value;
  return timestamp_copy;
}

pr_metric_t *pr_create_metric_from_entry(pr_metric_entry_t *metric_entry) {
  pr_metric_t *metric = smalloc(sizeof(*metric));
  memset(metric, 0, sizeof(*metric));
  metric->labels = pr_copy_label_list(metric_entry->labels);
  metric->value = metric_entry->value;
  metric->timestamp = pr_copy_timestamp(metric_entry->timestamp);
  metric->next = NULL;
  return metric;
}

void pr_add_metric_to_metric_family(pr_metric_family_t *metric_family,
                                    pr_metric_t *metric) {
  metric->next = metric_family->metric_list;
  metric_family->metric_list = metric;
}

int pr_compare_entries_names(const char *name_x, const char *name_y) {
  size_t len_x = strlen(name_x);
  size_t len_y = strlen(name_y);
  for (size_t suff_x_id = 0;
       suff_x_id * sizeof(PR_METRIC_SUFFIXES[0]) < sizeof(PR_METRIC_SUFFIXES);
       suff_x_id++) {
    size_t len_suff_x = strlen(PR_METRIC_SUFFIXES[suff_x_id]);
    if (len_suff_x > len_x ||
        strcmp(name_x + len_x - len_suff_x, PR_METRIC_SUFFIXES[suff_x_id])) {
      continue;
    }
    for (size_t suff_y_id = 0;
         suff_y_id * sizeof(PR_METRIC_SUFFIXES[0]) < sizeof(PR_METRIC_SUFFIXES);
         suff_y_id++) {
      size_t len_suff_y = strlen(PR_METRIC_SUFFIXES[suff_y_id]);
      if (len_suff_y > len_y ||
          strcmp(name_y + len_y - len_suff_y, PR_METRIC_SUFFIXES[suff_y_id])) {
        continue;
      }
      if (len_x - len_suff_x == len_y - len_suff_y &&
          !strncmp(name_x, name_y, len_x - len_suff_x)) {
        return 1;
      }
    }
  }
  return 0;
}

void pr_update_metric_family_name(char **cur_name, char *new_name) {
  if (!*cur_name || strcmp(new_name, *cur_name) < 0) {
    free(*cur_name);
    *cur_name = sstrdup(new_name);
  }
}

int pr_metric_has_label_name(pr_metric_t *metric, const char *label_name) {
  pr_label_t *cur_label = metric->labels;
  while (cur_label) {
    if (strcmp(cur_label->name, label_name) == 0) {
      return 1;
    }
    cur_label = cur_label->next;
  }
  return 0;
}

void pr_create_label_and_add_to_metric(pr_metric_t *metric,
                                       const char *label_name,
                                       const char *label_value) {
  char *label_name_cp = sstrdup(label_name);
  char *label_value_cp = sstrdup(label_value);
  pr_label_t *new_label = pr_create_label(label_name_cp, label_value_cp);
  metric->labels = pr_add_label_to_list(metric->labels, new_label);
}

int pr_normalize_metric_fam(pr_metric_family_t *fam) {
  if (fam->tp != PR_SUMMARY && fam->tp != PR_HISTOGRAM) {
    return 0;
  }
  pr_metric_t *cur_metric = fam->metric_list;
  if (!cur_metric || !cur_metric->next) {
    ERROR("Summary and histogram must have at least two entries");
    return EXIT_FAILURE;
  }

  int is_bucket_prev = 1;
  while (cur_metric) {
    switch (fam->tp) {
    case (PR_SUMMARY): {
      if (pr_metric_has_label_name(cur_metric, "quantile")) {
        is_bucket_prev = 1;
      } else if (is_bucket_prev) {
        pr_create_label_and_add_to_metric(cur_metric, "m_suff", "sum");
        is_bucket_prev = 0;
      } else {
        pr_create_label_and_add_to_metric(cur_metric, "m_suff", "count");
        is_bucket_prev = 0;
      }
      break;
    }
    case (PR_HISTOGRAM): {
      if (pr_metric_has_label_name(cur_metric, "le")) {
        pr_create_label_and_add_to_metric(cur_metric, "m_suff", "bucket");
        is_bucket_prev = 1;
      } else if (is_bucket_prev) {
        pr_create_label_and_add_to_metric(cur_metric, "m_suff", "sum");
        is_bucket_prev = 0;
      } else {
        pr_create_label_and_add_to_metric(cur_metric, "m_suff", "count");
        is_bucket_prev = 0;
      }
      break;
    }
    default: {
      break;
    }
    }
    cur_metric = cur_metric->next;
  }
  return 0;
}

int pr_add_entry_to_item_list(pr_item_list_t *item_list, pr_entry_t *entry) {
  if (entry->tp != PR_COMMENT_ENTRY) {
    char *metric_family_name = pr_get_cur_family_name(item_list);
    if (!metric_family_name ||
        !pr_compare_entries_names(metric_family_name,
                                  entry->body.metric->name)) {
      pr_item_t *new_metric_family = pr_create_metric_family_item();
      pr_add_item_to_item_list(item_list, new_metric_family);
    }
    pr_metric_family_t *metric_family = item_list->begin->body.metric_family;
    switch (entry->tp) {
    case (PR_METRIC_ENTRY): {
      pr_metric_entry_t *metric_entry = entry->body.metric;
      pr_update_metric_family_name(&metric_family->name, metric_entry->name);
      pr_metric_t *new_metric = pr_create_metric_from_entry(metric_entry);
      pr_add_metric_to_metric_family(metric_family, new_metric);
      break;
    }
    case (PR_TYPE_ENTRY): {
      pr_type_entry_t *type_entry = entry->body.type;
      pr_update_metric_family_name(&metric_family->name, type_entry->name);
      metric_family->tp = type_entry->tp;
      if (pr_normalize_metric_fam(metric_family) != 0) {
        return EXIT_FAILURE;
      }
      break;
    }
    case (PR_HELP_ENTRY): {
      pr_help_entry_t *help_entry = entry->body.help;
      pr_update_metric_family_name(&metric_family->name, help_entry->name);
      if (!metric_family->help) {
        metric_family->help = sstrdup(help_entry->hint);
      }
      break;
    }
    case (PR_COMMENT_ENTRY): {
      break;
    }
    }
  } else {
    pr_item_t *new_comment = pr_create_comment_item(entry->body.comment->text);
    pr_add_item_to_item_list(item_list, new_comment);
  }
  return 0;
}
