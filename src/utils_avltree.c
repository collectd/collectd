#include <stdlib.h>
#include <assert.h>

#include "utils_avltree.h"

#define BALANCE(n) ((((n)->right == NULL) ? 0 : (n)->right->height) \
				 - (((n)->left == NULL) ? 0 : (n)->left->height))

/*
 * private data types
 */
struct avl_node_s
{
	void *key;
	void *value;

	int height;
	struct avl_node_s *left;
	struct avl_node_s *right;
	struct avl_node_s *parent;
};
typedef struct avl_node_s avl_node_t;

struct avl_tree_s
{
	avl_node_t *root;
	int (*compare) (const void *, const void *);
};

struct avl_iterator_s
{
	avl_tree_t *tree;
	avl_node_t *node;
};

/*
 * private functions
 */
static void free_node (avl_node_t *n)
{
	if (n == NULL)
		return;

	if (n->left != NULL)
		free_node (n->left);
	if (n->right != NULL)
		free_node (n->right);

	free (n);
}

static avl_node_t *search (avl_tree_t *t, const void *key)
{
	avl_node_t *n;
	int cmp;

	n = t->root;
	while (n != NULL)
	{
		cmp = t->compare (key, n->key);
		if (cmp == 0)
			return (n);
		else if (cmp < 0)
			n = n->left;
		else
			n = n->right;
	}

	return (NULL);
}

static void rebalance (avl_tree_t *t, avl_node_t *n)
{
	int height_left;
	int height_right;
	int height_new;
	int cmp;

	while (n != NULL)
	{
		height_left = (n->left == NULL) ? 0 : n->left->height;
		height_right = (n->right == NULL) ? 0 : n->right->height;

		height_new = 1 + ((height_left > height_right) ? height_left : height_right);

		if (height_new == n->height)
			break;

		n->height = height_new;

		cmp = height_right - height_left;
		if (cmp < -1)
		{
			avl_node_t *l;
			avl_node_t *lr;

			l = n->left;
			lr = l->right;

			l->right = n;
			l->parent = n->parent;
			n->parent = l;
			n->left = lr;

			if (lr != NULL)
				lr->parent = n;

			if (l->parent == NULL)
			{
				assert (t->root == n);
				t->root = l;
			}
			else
			{
				assert ((l->parent->left == n) || (l->parent->right == n));
				if (l->parent->left == n)
					l->parent->left = l;
				else
					l->parent->right = l;
			}
		}
		else if (cmp > 1)
		{
			avl_node_t *r;
			avl_node_t *rl;

			r = n->right;
			rl = r->left;

			r->left = n;
			r->parent = n->parent;
			n->parent = r;
			n->right = rl;

			if (rl != NULL)
				rl->parent = n;

			if (r->parent == NULL)
			{
				assert (t->root == n);
				t->root = r;
			}
			else
			{
				assert ((r->parent->left == n) || (r->parent->right == n));
				if (r->parent->left == n)
					r->parent->left = r;
				else
					r->parent->right = r;
			}
		}
		else
		{
			n = n->parent;
		}

		assert ((n == NULL) || (n->parent == NULL)
				|| (n->parent->left == n)
				|| (n->parent->right == n));
	} /* while (n != NULL) */
} /* void rebalance */

static avl_iterator_t *avl_create_iterator (avl_tree_t *t, avl_node_t *n)
{
	avl_iterator_t *iter;

	iter = (avl_iterator_t *) malloc (sizeof (avl_iterator_t));
	if (iter == NULL)
		return (NULL);

	iter->tree = t;
	iter->node = n;

	return (iter);
}

void *avl_node_next (avl_tree_t *t, avl_node_t *n)
{
	avl_node_t *r; /* return node */

	if (n == NULL)
	{
		return (NULL);
	}
	else if (n->right == NULL)
	{

		r = n->parent;
		while (r != NULL)
		{
			/* stop if a bigger node is found */
			if (t->compare (n, r) < 0) /* n < r */
				break;
			r = r->parent;
		}
	}
	else
	{
		r = n->right;
		while (r->left != NULL)
			r = r->left;
	}

	return (r);
}

void *avl_node_prev (avl_tree_t *t, avl_node_t *n)
{
	avl_node_t *r; /* return node */

	if (n == NULL)
	{
		return (NULL);
	}
	else if (n->left == NULL)
	{

		r = n->parent;
		while (r != NULL)
		{
			/* stop if a smaller node is found */
			if (t->compare (n, r) > 0) /* n > r */
				break;
			r = r->parent;
		}
	}
	else
	{
		r = n->left;
		while (r->right != NULL)
			r = r->right;
	}

	return (r);
}

static int _remove (avl_tree_t *t, avl_node_t *n)
{
	assert ((t != NULL) && (n != NULL));

	if ((n->left == NULL) && (n->right == NULL))
	{
		/* Deleting a leave is easy */
		if (n->parent == NULL)
		{
			assert (t->root == n);
			t->root = NULL;
		}
		else
		{
			assert ((n->parent->left == n)
					|| (n->parent->right == n));
			if (n->parent->left == n)
				n->parent->left = NULL;
			else
				n->parent->right = NULL;

			rebalance (t, n->parent);
		}

		free_node (n);
	}
	else
	{
		avl_node_t *r; /* replacement node */
		if (BALANCE (n) < 0)
		{
			assert (n->left != NULL);
			r = avl_node_prev (t, n);
		}
		else
		{
			assert (n->right != NULL);
			r = avl_node_next (t, n);
		}
		n->key   = r->key;
		n->value = r->value;

		_remove (t, r);
	}

	return (0);
} /* void *_remove */

/*
 * public functions
 */
avl_tree_t *avl_create (int (*compare) (const void *, const void *))
{
	avl_tree_t *t;

	if ((t = (avl_tree_t *) malloc (sizeof (avl_tree_t))) == NULL)
		return (NULL);

	t->root = NULL;
	t->compare = compare;

	return (t);
}

void avl_destroy (avl_tree_t *t)
{
	free_node (t->root);
	free (t);
}

int avl_insert (avl_tree_t *t, void *key, void *value)
{
	avl_node_t *new;
	avl_node_t *nptr;
	int cmp;

	if ((new = (avl_node_t *) malloc (sizeof (avl_node_t))) == NULL)
		return (-1);

	new->key = key;
	new->value = value;
	new->height = 1;
	new->left = NULL;
	new->right = NULL;

	if (t->root == NULL)
	{
		new->parent = NULL;
		t->root = new;
		return (0);
	}

	nptr = t->root;
	while (42)
	{
		cmp = t->compare (nptr->key, new->key);
		if (cmp == 0)
		{
			free_node (new);
			return (-1);
		}
		else if (cmp < 0)
		{
			/* nptr < new */
			if (nptr->right == NULL)
			{
				nptr->right = new;
				new->parent = nptr;
				nptr = NULL;
				break;
			}
			else
			{
				nptr = nptr->right;
			}
		}
		else /* if (cmp > 0) */
		{
			/* nptr > new */
			if (nptr->left == NULL)
			{
				nptr->left = new;
				new->parent = nptr;
				nptr = NULL;
				break;
			}
			else
			{
				nptr = nptr->left;
			}
		}
	} /* while (42) */

	assert ((new->parent != NULL)
			&& ((new->parent->left == new)
				|| (new->parent->right == new)));

	return (0);
} /* int avl_insert */

int avl_remove (avl_tree_t *t, void *key, void **rkey, void **rvalue)
{
	avl_node_t *n;

	assert (t != NULL);

	n = search (t, key);
	if (n == NULL)
		return (-1);

	if (rkey != NULL)
		*rkey = n->key;
	if (rvalue != NULL)
		*rvalue = n->value;

	return (_remove (t, n));
} /* void *avl_remove */

int avl_get (avl_tree_t *t, const void *key, void **value)
{
	avl_node_t *n;

	assert (value != NULL);

	n = search (t, key);
	if (n == NULL)
		return (-1);

	*value = n->value;

	return (0);
}

avl_iterator_t *avl_get_iterator (avl_tree_t *t)
{
	avl_node_t *n;

	if (t == NULL)
		return (NULL);

	for (n = t->root; n != NULL; n = n->left)
		if (n->left == NULL)
			break;

	return (avl_create_iterator (t, n));
} /* avl_iterator_t *avl_get_iterator */

void *avl_iterator_next (avl_iterator_t *iter)
{
	avl_node_t *n;

	if ((iter == NULL) || (iter->node == NULL))
		return (NULL);

	n = avl_node_next (iter->tree, iter->node);

	if (n == NULL)
		return (NULL);

	iter->node = n;
	return (n);

}

void *avl_iterator_prev (avl_iterator_t *iter)
{
	avl_node_t *n;

	if ((iter == NULL) || (iter->node == NULL))
		return (NULL);

	n = avl_node_prev (iter->tree, iter->node);

	if (n == NULL)
		return (NULL);

	iter->node = n;
	return (n);

}

void avl_iterator_destroy (avl_iterator_t *iter)
{
	free (iter);
}
