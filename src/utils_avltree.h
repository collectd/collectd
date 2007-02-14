#ifndef UTILS_AVLTREE_H
#define UTILS_AVLTREE_H 1

struct avl_tree_s;
typedef struct avl_tree_s avl_tree_t;

struct avl_iterator_s;
typedef struct avl_iterator_s avl_iterator_t;

struct avl_keyval_s
{
	void *key;
	void *value;
};

avl_tree_t *avl_create (int (*compare) (const void *, const void *));
void avl_destroy (avl_tree_t *t);

int avl_insert (avl_tree_t *t, void *key, void *value);
int avl_remove (avl_tree_t *t, void *key, void **rkey, void **rvalue);

int avl_get (avl_tree_t *t, const void *key, void **value);

avl_iterator_t *avl_get_iterator (avl_tree_t *t);
void *avl_iterator_next (avl_iterator_t *iter);
void *avl_iterator_prev (avl_iterator_t *iter);
void avl_iterator_destroy (avl_iterator_t *iter);

#endif /* UTILS_AVLTREE_H */
