#ifndef BTREE_H
#define BTREE_H

/* B-tree implementation for RP6502
 * Order: 3 (max 2 keys per node, max 3 children)
 * Suitable for 256-byte stack limit and 16-bit int
 */

typedef struct BTreeNode
{
    unsigned char keys[2];      /* Max 2 keys */
    int values[2];             /* Values associated with keys */
    struct BTreeNode *children[3]; /* Max 3 children */
    unsigned char key_count;   /* Number of keys in this node */
    unsigned char is_leaf;     /* 1 if leaf, 0 if internal node */
} BTreeNode;

typedef struct
{
    BTreeNode *root;
} BTree;

/* Initialize a new B-tree */
BTree *btree_create(void);

/* Insert a key-value pair */
void btree_insert(BTree *tree, unsigned char key, int value);

/* Search for a key, returns value or -32768 if not found */
int btree_get(BTree *tree, unsigned char key);

/* Update an existing key's value */
unsigned char btree_update(BTree *tree, unsigned char key, int new_value);

/* Delete a key from the tree */
unsigned char btree_delete(BTree *tree, unsigned char key);

/* Print tree structure (for debugging) */
void btree_print(BTree *tree);

/* Free all nodes in the tree */
void btree_free(BTree *tree);

#endif
