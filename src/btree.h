#ifndef BTREE_H
#define BTREE_H

/* B-tree implementation for RP6502
 * Default order is 3 (max 2 keys per node, max 3 children)
 * Parameterize the maximum number of children with BTREE_MAX_CHILDREN.
 * Suitable for 256-byte stack limit and 16-bit int.
 */

#ifndef BTREE_MAX_CHILDREN
#define BTREE_MAX_CHILDREN 10
#endif

#if (BTREE_MAX_CHILDREN < 3)
#error "BTREE_MAX_CHILDREN must be at least 3"
#endif

#define BTREE_MAX_KEYS (BTREE_MAX_CHILDREN - 1)
#define BTREE_MIN_CHILDREN ((BTREE_MAX_CHILDREN + 1) / 2)
#define BTREE_MIN_KEYS (BTREE_MIN_CHILDREN - 1)
#define BTREE_SPLIT_INDEX (BTREE_MAX_KEYS / 2)

typedef struct BTreeNode
{
    unsigned int keys[BTREE_MAX_KEYS];      /* Key storage */
    void *values[BTREE_MAX_KEYS];           /* Generic values - can store any pointer */
    struct BTreeNode *children[BTREE_MAX_CHILDREN]; /* Child pointers */
    unsigned char key_count;   /* Number of keys in this node */
    unsigned char is_leaf;     /* 1 if leaf, 0 if internal node */
} BTreeNode;

typedef struct
{
    BTreeNode *root;
} BTree;

/* Initialize a new B-tree */
BTree *btree_create(void);

/* Insert a key-value pair (value is a void pointer) */
void btree_insert(BTree *tree, unsigned int key, void *value);

/* Search for a key, returns value pointer or NULL if not found */
void *btree_get(BTree *tree, unsigned int key);

/* Update an existing key's value */
unsigned char btree_update(BTree *tree, unsigned int key, void *new_value);

/* Delete a key from the tree */
unsigned char btree_delete(BTree *tree, unsigned int key);

/* Print tree structure (for debugging) */
void btree_print(BTree *tree);

/* Count total nodes in the tree */
unsigned int btree_node_count(BTree *tree);

/* Free all nodes in the tree */
void btree_free(BTree *tree);

#endif
