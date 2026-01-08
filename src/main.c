#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include "btree.h"

void main()
{
    BTree *tree;
    int value;
    unsigned char i;
    unsigned char random_id;
    int random_value;
    unsigned char item_count;

    puts("=== B-tree Demo ===\n");

    /* Create tree */
    tree = btree_create();
    if (!tree)
    {
        puts("Failed to create tree");
        return;
    }

    /* INSERT operations */
    puts("Inserting manual values...");
    btree_insert(tree, 10, 100);
    btree_insert(tree, 20, 200);
    btree_insert(tree, 30, 300);
    btree_insert(tree, 40, 400);
    btree_insert(tree, 50, 500);
    btree_insert(tree, 5, 50);
    btree_insert(tree, 25, 250);
    puts("Manual inserts complete.\n");

    /* INSERT random entries */
    puts("Inserting random entries...");
    item_count = 100;
    for (i = 0; i < item_count; i++)
    {
        random_id = (unsigned char)rand();
        random_value = (int)(rand() << 1);
        btree_insert(tree, random_id, random_value);
    }
    puts("Random inserts complete.\n");

    puts("Tree structure:");
    btree_print(tree);
    putchar('\n');

    /* GET operations */
    puts("Getting values...");
    value = btree_get(tree, 20);
    if (value != (int)0x8000)
        printf("Key 20: value = %d\n", value);

    value = btree_get(tree, 25);
    if (value != (int)0x8000)
        printf("Key 25: value = %d\n", value);

    value = btree_get(tree, 99);
    if (value == (int)0x8000)
        puts("Key 99: not found");
    putchar('\n');

    /* UPDATE operations */
    puts("Updating values...");
    if (btree_update(tree, 20, 2000))
        puts("Updated key 20 to 2000");

    if (btree_update(tree, 50, 5000))
        puts("Updated key 50 to 5000");

    value = btree_get(tree, 20);
    printf("Key 20 after update: %d\n", value);
    putchar('\n');

    /* DELETE operations */
    puts("Deleting values...");
    if (btree_delete(tree, 10))
        puts("Deleted key 10");

    if (btree_delete(tree, 30))
        puts("Deleted key 30");

    putchar('\n');
    puts("Demo complete!");

    /* Cleanup */
    btree_free(tree);
}