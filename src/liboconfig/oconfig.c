/**
 * collectd - src/liboconfig/oconfig.c
 * Copyright (C) 2006,2007  Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 *   Mathieu Stoffel <mathieu.stoffel at atos.net>
 **/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils_llist.h"

#include "oconfig.h"

extern FILE *yyin;
extern int yyparse(void);

oconfig_item_t *ci_root;
const char *c_file;

static void yyset_in(FILE *fd) { yyin = fd; } /* void yyset_in */

static oconfig_item_t *oconfig_parse_fh(FILE *fh) {
  int status;
  oconfig_item_t *ret;

  char file[10];

  yyset_in(fh);

  if (NULL == c_file) {
    status = snprintf(file, sizeof(file), "<fd#%d>", fileno(fh));

    if ((status < 0) || (((size_t)status) >= sizeof(file))) {
      c_file = "<unknown>";
    } else {
      file[sizeof(file) - 1] = '\0';
      c_file = file;
    }
  }

  status = yyparse();
  if (status != 0) {
    fprintf(stderr, "yyparse returned error #%i\n", status);
    return NULL;
  }

  c_file = NULL;

  ret = ci_root;
  ci_root = NULL;
  yyset_in((FILE *)0);

  return ret;
} /* oconfig_item_t *oconfig_parse_fh */

oconfig_item_t *oconfig_parse_file(const char *file) {
  FILE *fh;
  oconfig_item_t *ret;

  c_file = file;

  fh = fopen(file, "r");
  if (fh == NULL) {
    fprintf(stderr, "fopen (%s) failed: %s\n", file, strerror(errno));
    return NULL;
  }

  ret = oconfig_parse_fh(fh);
  fclose(fh);

  c_file = NULL;

  return ret;
} /* oconfig_item_t *oconfig_parse_file */

oconfig_item_t *oconfig_clone(const oconfig_item_t *ci_orig) {
  oconfig_item_t *ci_copy;

  ci_copy = calloc(1, sizeof(*ci_copy));
  if (ci_copy == NULL) {
    fprintf(stderr, "calloc failed.\n");
    return NULL;
  }
  ci_copy->values = NULL;
  ci_copy->parent = NULL;
  ci_copy->children = NULL;

  ci_copy->key = strdup(ci_orig->key);
  if (ci_copy->key == NULL) {
    fprintf(stderr, "strdup failed.\n");
    free(ci_copy);
    return NULL;
  }

  if (ci_orig->values_num > 0) /* {{{ */
  {
    ci_copy->values = calloc(ci_orig->values_num, sizeof(*ci_copy->values));
    if (ci_copy->values == NULL) {
      fprintf(stderr, "calloc failed.\n");
      free(ci_copy->key);
      free(ci_copy);
      return NULL;
    }
    ci_copy->values_num = ci_orig->values_num;

    for (int i = 0; i < ci_copy->values_num; i++) {
      ci_copy->values[i].type = ci_orig->values[i].type;
      if (ci_copy->values[i].type == OCONFIG_TYPE_STRING) {
        ci_copy->values[i].value.string =
            strdup(ci_orig->values[i].value.string);
        if (ci_copy->values[i].value.string == NULL) {
          fprintf(stderr, "strdup failed.\n");
          oconfig_free(ci_copy);
          return NULL;
        }
      } else /* ci_copy->values[i].type != OCONFIG_TYPE_STRING) */
      {
        ci_copy->values[i].value = ci_orig->values[i].value;
      }
    }
  } /* }}} if (ci_orig->values_num > 0) */

  if (ci_orig->children_num > 0) /* {{{ */
  {
    ci_copy->children =
        calloc(ci_orig->children_num, sizeof(*ci_copy->children));
    if (ci_copy->children == NULL) {
      fprintf(stderr, "calloc failed.\n");
      oconfig_free(ci_copy);
      return NULL;
    }
    ci_copy->children_num = ci_orig->children_num;

    for (int i = 0; i < ci_copy->children_num; i++) {
      oconfig_item_t *child;

      child = oconfig_clone(ci_orig->children + i);
      if (child == NULL) {
        oconfig_free(ci_copy);
        return NULL;
      }
      child->parent = ci_copy;
      ci_copy->children[i] = *child;
      free(child);
    } /* for (i = 0; i < ci_copy->children_num; i++) */
  }   /* }}} if (ci_orig->children_num > 0) */

  return ci_copy;
} /* oconfig_item_t *oconfig_clone */

static void oconfig_free_all(oconfig_item_t *ci) {
  if (ci == NULL)
    return;

  if (ci->key != NULL)
    free(ci->key);

  for (int i = 0; i < ci->values_num; i++)
    if ((ci->values[i].type == OCONFIG_TYPE_STRING) &&
        (NULL != ci->values[i].value.string))
      free(ci->values[i].value.string);

  if (ci->values != NULL)
    free(ci->values);

  for (int i = 0; i < ci->children_num; i++)
    oconfig_free_all(ci->children + i);

  if (ci->children != NULL)
    free(ci->children);
}

void oconfig_free(oconfig_item_t *ci) {
  oconfig_free_all(ci);
  free(ci);
}

#if defined(COLLECT_DEBUG)
/* Fills "indent_str" with "nb_spaces" space characters, for indentation
 * purposes (plus the terminating null-character).
 * No check neither on the number of spaces nor on the string specifics is
 * performed: */
static inline void generate_indent_str(char *indent_str,
                                       const uint64_t nb_spaces) {
  memset(indent_str, ' ', nb_spaces);
  indent_str[nb_spaces] = '\0';
}

/*******/

void oconfig_print_tree(const oconfig_item_t *const ci,
                        const uint64_t indent_max_lvl,
                        const uint64_t indent_in_spaces, FILE *io_stream) {
  /**************************************************************************
   * Checking that input parameters are compliant with pre-requisites:
   **************************************************************************/
  if (ci == NULL)
    return;
  if (io_stream == NULL)
    return;

  /**************************************************************************
   * Structures:
   **************************************************************************/
  /* Encapsulates the address of the considered node which children are to be
   * processed, together with the associated indentation level and the ID of
   * the child to be processed: */
  typedef struct node_info_t {
    const oconfig_item_t *address;
    uint64_t indent_lvl;
    int child_id;
  } node_info_t;

  /**************************************************************************
   * Variables:
   **************************************************************************/
  /* In order to keep track of the indent level while parsing the
   * configuration tree: */
  char *indent = NULL;
  uint64_t indent_lvl = 0;

  /* List used as a stack to store the descending exploring path within the
   * configuration tree: */
  llist_t *node_stack = NULL;

  /* Pointer to be used to roam the configuration tree: */
  const oconfig_item_t *config_roamer = ci;

  /**************************************************************************
   * Initialisation:
   **************************************************************************/
  /* Allocating "indent": */
  indent = calloc(indent_max_lvl * indent_in_spaces + 1, sizeof(char));
  /***/
  if (indent == NULL)
    goto lbl_oconfig_print_tree_cleanup;

  /* Allocating "node_stack": */
  node_stack = llist_create();
  /***/
  if (node_stack == NULL)
    goto lbl_oconfig_print_tree_cleanup;

  /**************************************************************************
   * Core:
   **************************************************************************/
  /* Printing an header: */
  fprintf(io_stream, "\n=======[ Start of configuration tree ]=======\n\n");

  /* Roaming the configuration tree starting from its root: */
  while (config_roamer != NULL) {
    /* If the current line should be indented, generating the indentation
     * string, and printing it: */
    if (indent_lvl > 0) {
      generate_indent_str(indent, indent_lvl * indent_in_spaces);
      fprintf(io_stream, "%s", indent);
    }

    /* Printing the key of the currently considered node: */
    fprintf(io_stream, "%s:", config_roamer->key);

    /* Roaming all the values associated with the currently considered
     * node: */
    for (int i = 0; i < config_roamer->values_num; i++) {
      /* Determining the type of the currently considered value, and
       * printing it: */
      switch (config_roamer->values[i].type) {
      case OCONFIG_TYPE_NUMBER:
        fprintf(io_stream, " %f", config_roamer->values[i].value.number);
        break;

      case OCONFIG_TYPE_BOOLEAN:
        fprintf(io_stream, " %s",
                (config_roamer->values[i].value.boolean ? "true" : "false"));
        break;

      /* The default case corresponds to the value being a string: */
      default:
        fprintf(io_stream, " %s", config_roamer->values[i].value.string);
        break;
      }
    }

    /* Newline character to end the printing of the currently considered
     * node: */
    fprintf(io_stream, "%s", "\n");

    /* Determining which node is the next one to be considered.
     *
     * Branch (1)
     * The configuration tree is roamed from top to bottom.
     * After a node is processed, the subtrees of its children are processed
     * in order.
     * When going down the tree to handle the subtree associated with a
     * child of the considered node, the latter is stored in a stack (i.e.
     * "node_stack").
     * By doing so, it is possible to process the subtrees of other chidren
     * of the considered node once the considered subtree is processed.
     *
     * Branch (2)
     * When the considered node does not have children, popping out of the
     * stack the parent node to go back to.
     * Its next child, and the associated subtree, then become the node and
     * subtree to be processed.
     *
     * Branch(3)
     * When the stack is empty, and the currently considered node does not
     * have children, the whole tree was processed (i.e. terminaison
     * criteria). */

    /* Branch (1): */
    if (config_roamer->children_num > 0) {
      /* Allocating the structure in which the information associated with
       * the considered parent node are to be stored.
       * It is then appended to the stack: */
      node_info_t *node_info = malloc(sizeof(*node_info));
      if (node_info == NULL)
        goto lbl_oconfig_print_tree_cleanup;
      /***/
      node_info->address = config_roamer;
      node_info->indent_lvl = indent_lvl;
      node_info->child_id = 0;
      /***/
      llentry_t *node_entry = llentry_create(NULL, node_info);
      if (node_entry == NULL)
        goto lbl_oconfig_print_tree_cleanup;
      /***/
      llist_append(node_stack, node_entry);

      /* The next node to be considered is the first children of the
       * considered node: */
      config_roamer = &(config_roamer->children[0]);

      /* Increasing the indentation level since we go deeper into the
       * configuration tree, except if the maximum indentation level was
       * reached: */
      if (indent_lvl < indent_max_lvl)
        indent_lvl++;
    }
    /* Branch (2): */
    else if (llist_size(node_stack) > 0) {
      /* The stack will be roamed to find a parent node which still has
       * at least one child which subtree should be roamed.
       * If none is found, the tree was completely roamed (which
       * translates in "config_roamer" being set to "NULL": */
      config_roamer = NULL;

      /* Roaming all the stack until one parent node with a child which
       * subtree should be roamed: */
      do {
        /* Analysing the node on top of the stack: */
        llentry_t *node_entry = llist_tail(node_stack);
        /***/
        node_info_t *node_info = (node_info_t *)(node_entry->value);

        /* Does the considered node still have at least one child to
         * process? */
        if (node_info->address->children_num > (node_info->child_id + 1)) {
          /* The next node to process was found: */
          config_roamer =
              &(node_info->address->children[node_info->child_id + 1]);

          /* Increasing the indentation level since we go deeper into
           * the configuration tree, except if the maximum indentation
           * level was reached: */
          if (indent_lvl < indent_max_lvl) {
            indent_lvl = node_info->indent_lvl + 1;
          }

          /* Next child to be considered: */
          (node_info->child_id)++;

          break;
        } else {
          /* Since all the children of the considered parent node were
           * processed, it is no longer useful to keep it in the
           * stack, hence popping and deallocating it: */
          llist_remove(node_stack, node_entry);
          if (node_entry->value != NULL)
            free(node_entry->value);
          llentry_destroy(node_entry);
        }
      } while (llist_size(node_stack) > 0);
    }
    /* Branch (3): */
    else
      config_roamer = NULL;
  }

  /* Printing a footer: */
  fprintf(io_stream, "\n=======[ End of configuration tree ]=======\n\n");

lbl_oconfig_print_tree_cleanup:
  /* Deallocating "indent": */
  if (indent != NULL)
    free(indent);

  /* Deallocating "node_stack": */
  if (node_stack != NULL) {
    /* Deallocating the content of each node: */
    for (llentry_t *le = llist_head(node_stack); le != NULL; le = le->next) {
      if (le->value != NULL)
        free(le->value);
    }

    /* Deallocating the list itself: */
    llist_destroy(node_stack);
  }
}
#endif
