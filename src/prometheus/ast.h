#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  PR_COUNTER,
  PR_GAUGE,
  PR_HISTOGRAM,
  PR_SUMMARY,
  PR_UNTYPED
} pr_metric_type_t;

typedef enum {
  PR_METRIC_ENTRY,
  PR_COMMENT_ENTRY,
  PR_TYPE_ENTRY,
  PR_HELP_ENTRY
} pr_entry_type_t;

typedef enum { PR_METRIC_FAMILY_ITEM, PR_COMMENT_ITEM } pr_item_type_t;

typedef struct pr_label_s {
  char *name;
  char *value;
  struct pr_label_s *next;
} pr_label_t;

typedef struct pr_timestamp_s {
  bool has_value;
  int64_t value;
} pr_timestamp_t;

typedef struct pr_metric_entry_s {
  char *name;
  pr_label_t *labels;
  double value;
  pr_timestamp_t *timestamp;
} pr_metric_entry_t;

typedef struct pr_comment_entry_s {
  char *text;
} pr_comment_entry_t;

typedef struct pr_type_entry_s {
  char *name;
  pr_metric_type_t tp;
} pr_type_entry_t;

typedef struct pr_help_entry_s {
  char *name;
  char *hint;
} pr_help_entry_t;

typedef struct pr_entry_s {
  pr_entry_type_t tp;
  union {
    pr_metric_entry_t *metric;
    pr_comment_entry_t *comment;
    pr_type_entry_t *type;
    pr_help_entry_t *help;
  } body;
} pr_entry_t;

typedef struct pr_item_s pr_item_t;

typedef struct pr_metric_s pr_metric_t;

typedef struct pr_metric_s {
  pr_label_t *labels;
  double value;
  pr_timestamp_t *timestamp;
  pr_metric_t *next;
} pr_metric_t;

typedef struct pr_metric_family_s {
  char *name;
  char *help;
  pr_metric_type_t tp;
  pr_metric_t *metric_list;
} pr_metric_family_t;

typedef pr_comment_entry_t pr_comment_t;

typedef struct pr_item_s {
  pr_item_type_t tp;
  union {
    pr_metric_family_t *metric_family;
    pr_comment_t *comment;
  } body;
  pr_item_t *next;
} pr_item_t;

typedef struct pr_item_list_s {
  pr_item_t *begin;
} pr_item_list_t;

pr_label_t *pr_create_label(char *name, char *value);
pr_timestamp_t *pr_create_empty_timestamp(void);
pr_timestamp_t *pr_create_value_timestamp(int64_t value);
pr_label_t *pr_add_label_to_list(pr_label_t *list, pr_label_t *label);
pr_metric_entry_t *pr_create_metric_entry(char *name, pr_label_t *labels,
                                          double value,
                                          pr_timestamp_t *timestamp);
pr_comment_entry_t *pr_create_comment_entry(char *text);
pr_type_entry_t *pr_create_type_entry(char *name, pr_metric_type_t tp);
pr_help_entry_t *pr_create_help_entry(char *name, char *hint);
pr_entry_t *pr_create_entry_from_metric(pr_metric_entry_t *metric);
pr_entry_t *pr_create_entry_from_comment(pr_comment_entry_t *comment);
pr_entry_t *pr_create_entry_from_type(pr_type_entry_t *node);
pr_entry_t *pr_create_entry_from_help(pr_help_entry_t *help);
pr_item_t *pr_create_metric_family_item(void);
pr_item_t *pr_create_comment_item(char *text);
pr_item_list_t *pr_create_item_list(void);
int pr_add_entry_to_item_list(pr_item_list_t *item_list, pr_entry_t *entry);

void pr_delete_label_list(pr_label_t *label_list);
void pr_delete_metric_entry(pr_metric_entry_t *metric);
void pr_delete_comment_entry(pr_comment_entry_t *comment);
void pr_delete_type_entry(pr_type_entry_t *type);
void pr_delete_help_entry(pr_help_entry_t *help);
void pr_delete_entry(pr_entry_t *entry);
void pr_delete_item_list(pr_item_list_t *item_list);

#endif
