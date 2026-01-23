#include "btree.h"
#include <stdlib.h>
#include <stdio.h>

static BTreeNode *node_create(unsigned char is_leaf)
{
    BTreeNode *node;
    unsigned char i;

    node = (BTreeNode *)malloc(sizeof(BTreeNode));
    if (!node)
        return NULL;

    node->key_count = 0;
    node->is_leaf = is_leaf;

    for (i = 0; i < BTREE_MAX_CHILDREN; i++)
        node->children[i] = NULL;

    return node;
}

BTree *btree_create(void)
{
    BTree *tree;

    tree = (BTree *)malloc(sizeof(BTree));
    if (!tree)
        return NULL;

    tree->root = node_create(1);
    if (!tree->root)
    {
        free(tree);
        return NULL;
    }

    return tree;
}

static void node_split_child(BTreeNode *parent, unsigned char index)
{
    BTreeNode *full_child;
    BTreeNode *new_node;
    unsigned char i;
    unsigned char move_keys;
    unsigned char move_children;
    unsigned char mid;

    full_child = parent->children[index];
    new_node = node_create(full_child->is_leaf);

    if (!new_node)
        return;
    mid = BTREE_SPLIT_INDEX;
    move_keys = (unsigned char)(BTREE_MAX_KEYS - mid - 1);
    move_children = (unsigned char)(BTREE_MAX_CHILDREN - mid - 1);

    /* Move upper half keys/values to new node */
    for (i = 0; i < move_keys; i++)
    {
        new_node->keys[i] = full_child->keys[mid + 1 + i];
        new_node->values[i] = full_child->values[mid + 1 + i];
    }
    new_node->key_count = move_keys;

    /* Move upper children if internal */
    if (!full_child->is_leaf)
    {
        for (i = 0; i < move_children; i++)
            new_node->children[i] = full_child->children[mid + 1 + i];
    }

    /* Shrink full child */
    full_child->key_count = mid;

    /* Shift parent children to make room */
    for (i = parent->key_count + 1; i > index + 1; i--)
        parent->children[i] = parent->children[i - 1];

    /* Shift parent keys/values */
    for (i = parent->key_count; i > index; i--)
    {
        parent->keys[i] = parent->keys[i - 1];
        parent->values[i] = parent->values[i - 1];
    }

    /* Promote middle key to parent */
    parent->keys[index] = full_child->keys[mid];
    parent->values[index] = full_child->values[mid];
    parent->children[index + 1] = new_node;
    parent->key_count++;
}

static void btree_insert_non_full(BTreeNode *node, unsigned int key, void *value)
{
    int i;

    i = node->key_count - 1;

    if (node->is_leaf)
    {
        /* Insert key in sorted position */
        while (i >= 0 && key < node->keys[i])
        {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }

        /* Check for duplicate */
        if (i >= 0 && key == node->keys[i])
        {
            node->values[i] = value;
            return;
        }

        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->key_count++;
    }
    else
    {
        /* Find child to insert into */
        while (i >= 0 && key < node->keys[i])
            i--;

        /* Check for duplicate */
        if (i >= 0 && key == node->keys[i])
        {
            node->values[i] = value;
            return;
        }

        i++;

        /* Split child if full */
        if (node->children[i]->key_count == BTREE_MAX_KEYS)
        {
            node_split_child(node, i);

            if (key > node->keys[i])
                i++;
        }

        btree_insert_non_full(node->children[i], key, value);
    }
}

void btree_insert(BTree *tree, unsigned int key, void *value)
{
    BTreeNode *new_root;

    if (!tree || !tree->root)
        return;

    if (tree->root->key_count == BTREE_MAX_KEYS)
    {
        /* Root is full, split it */
        new_root = node_create(0);
        if (!new_root)
            return;

        new_root->children[0] = tree->root;
        node_split_child(new_root, 0);
        tree->root = new_root;
    }

    btree_insert_non_full(tree->root, key, value);
}

static void *btree_search_node(BTreeNode *node, unsigned int key)
{
    unsigned char i;

    i = 0;
    while (i < node->key_count && key > node->keys[i])
        i++;

    if (i < node->key_count && key == node->keys[i])
        return node->values[i];

    if (node->is_leaf)
        return NULL; /* Not found */

    return btree_search_node(node->children[i], key);
}

static unsigned int btree_count_nodes_internal(BTreeNode *node)
{
    unsigned int count;
    unsigned char i;

    if (!node)
        return 0;

    count = 1;

    if (!node->is_leaf)
    {
        for (i = 0; i <= node->key_count; i++)
            count = (unsigned int)(count + btree_count_nodes_internal(node->children[i]));
    }

    return count;
}

void *btree_get(BTree *tree, unsigned int key)
{
    if (!tree || !tree->root)
        return NULL;

    return btree_search_node(tree->root, key);
}

unsigned int btree_node_count(BTree *tree)
{
    if (!tree || !tree->root)
        return 0;

    return btree_count_nodes_internal(tree->root);
}

unsigned char btree_update(BTree *tree, unsigned int key, void *new_value)
{
    BTreeNode *node;
    unsigned char i;

    if (!tree || !tree->root)
        return 0;

    node = tree->root;

    while (node)
    {
        i = 0;
        while (i < node->key_count && key > node->keys[i])
            i++;

        if (i < node->key_count && key == node->keys[i])
        {
            node->values[i] = new_value;
            return 1;
        }

        if (node->is_leaf)
            return 0;

        node = node->children[i];
    }

    return 0;
}

static void merge_nodes(BTreeNode *parent, unsigned char index)
{
    BTreeNode *left;
    BTreeNode *right;
    unsigned char i;

    left = parent->children[index];
    right = parent->children[index + 1];

    /* Bring parent separator down */
    left->keys[left->key_count] = parent->keys[index];
    left->values[left->key_count] = parent->values[index];

    /* Append right node keys */
    for (i = 0; i < right->key_count; i++)
    {
        left->keys[left->key_count + 1 + i] = right->keys[i];
        left->values[left->key_count + 1 + i] = right->values[i];
    }

    /* Append right children if internal */
    if (!left->is_leaf)
    {
        for (i = 0; i <= right->key_count; i++)
            left->children[left->key_count + 1 + i] = right->children[i];
    }

    left->key_count = (unsigned char)(left->key_count + 1 + right->key_count);

    /* Shift parent keys/children to close gap */
    for (i = index; i < parent->key_count - 1; i++)
    {
        parent->keys[i] = parent->keys[i + 1];
        parent->values[i] = parent->values[i + 1];
    }
    
    /* Shift children - need to close the gap from merged right node */
    for (i = index + 1; i < parent->key_count; i++)
    {
        parent->children[i] = parent->children[i + 1];
    }

    parent->key_count--;
    free(right);
}

static void btree_delete_node(BTreeNode *node, unsigned int key)
{
    unsigned char i;
    BTreeNode *child;
    BTreeNode *left;
    BTreeNode *right;
    unsigned char j;

    i = 0;
    while (i < node->key_count && key > node->keys[i])
        i++;

    if (i < node->key_count && key == node->keys[i])
    {
        if (node->is_leaf)
        {
            /* Simple case: key is in leaf */
            while (i < node->key_count - 1)
            {
                node->keys[i] = node->keys[i + 1];
                node->values[i] = node->values[i + 1];
                i++;
            }
            node->key_count--;
        }
        else
        {
            /* Internal node: choose predecessor or successor; otherwise merge */
            unsigned int promote_key;
            left = node->children[i];
            right = node->children[i + 1];

            if (left->key_count > BTREE_MIN_KEYS)
            {
                promote_key = left->keys[left->key_count - 1];
                node->keys[i] = promote_key;
                node->values[i] = left->values[left->key_count - 1];
                btree_delete_node(left, promote_key);
            }
            else if (right->key_count > BTREE_MIN_KEYS)
            {
                promote_key = right->keys[0];
                node->keys[i] = promote_key;
                node->values[i] = right->values[0];
                btree_delete_node(right, promote_key);
            }
            else
            {
                merge_nodes(node, i);
                btree_delete_node(left, key);
            }
        }
    }
    else if (!node->is_leaf)
    {
        child = node->children[i];

        if (child->key_count == BTREE_MIN_KEYS)
        {
            if (i > 0 && node->children[i - 1]->key_count > BTREE_MIN_KEYS)
            {
                /* Borrow from left sibling */
                left = node->children[i - 1];

                for (j = child->key_count; j > 0; j--)
                {
                    child->keys[j] = child->keys[j - 1];
                    child->values[j] = child->values[j - 1];
                }
                if (!child->is_leaf)
                    for (j = child->key_count + 1; j > 0; j--)
                        child->children[j] = child->children[j - 1];

                child->keys[0] = node->keys[i - 1];
                child->values[0] = node->values[i - 1];
                if (!child->is_leaf)
                    child->children[0] = left->children[left->key_count];

                node->keys[i - 1] = left->keys[left->key_count - 1];
                node->values[i - 1] = left->values[left->key_count - 1];

                left->key_count--;
                child->key_count++;
            }
            else if (i < node->key_count && node->children[i + 1]->key_count > BTREE_MIN_KEYS)
            {
                /* Borrow from right sibling */
                right = node->children[i + 1];

                child->keys[child->key_count] = node->keys[i];
                child->values[child->key_count] = node->values[i];
                if (!child->is_leaf)
                    child->children[child->key_count + 1] = right->children[0];

                node->keys[i] = right->keys[0];
                node->values[i] = right->values[0];

                for (j = 0; j < right->key_count - 1; j++)
                {
                    right->keys[j] = right->keys[j + 1];
                    right->values[j] = right->values[j + 1];
                }
                if (!right->is_leaf)
                {
                    for (j = 0; j < right->key_count; j++)
                        right->children[j] = right->children[j + 1];
                }

                right->key_count--;
                child->key_count++;
            }
            else
            {
                /* Merge with sibling */
                if (i < node->key_count)
                {
                    merge_nodes(node, i);
                }
                else
                {
                    merge_nodes(node, (unsigned char)(i - 1));
                    i = (unsigned char)(i - 1);
                }

                child = node->children[i];
            }
        }

        btree_delete_node(child, key);
    }
}

unsigned char btree_delete(BTree *tree, unsigned int key)
{
    if (!tree || !tree->root)
        return 0;

    if (btree_get(tree, key) == NULL)
        return 0; /* Key not found */

    btree_delete_node(tree->root, key);

    if (tree->root->key_count == 0 && !tree->root->is_leaf && tree->root->children[0])
    {
        BTreeNode *old_root;
        old_root = tree->root;
        tree->root = old_root->children[0];
        free(old_root);
    }

    /* Verify deletion was successful */
    if (btree_get(tree, key) != NULL)
        return 0; /* Delete failed - key still exists */

    return 1;
}

static void btree_print_node(BTreeNode *node, int depth)
{
    unsigned char i;
    int j;

    if (!node)
        return;

    for (j = 0; j < depth; j++)
        putchar(' ');

    printf("Node: ");
    for (i = 0; i < node->key_count; i++)
        printf("[%d:%d] ", node->keys[i], node->values[i]);
    putchar('\n');

    if (!node->is_leaf)
        for (i = 0; i <= node->key_count; i++)
            btree_print_node(node->children[i], depth + 2);
}

void btree_print(BTree *tree)
{
    if (!tree || !tree->root)
    {
        puts("Empty tree");
        return;
    }

    puts("B-tree structure:");
    btree_print_node(tree->root, 0);
}

static void btree_free_node(BTreeNode *node)
{
    unsigned char i;

    if (!node)
        return;

    if (!node->is_leaf)
        for (i = 0; i <= node->key_count; i++)
            btree_free_node(node->children[i]);

    free(node);
}

void btree_free(BTree *tree)
{
    if (!tree)
        return;

    btree_free_node(tree->root);
    free(tree);
}
