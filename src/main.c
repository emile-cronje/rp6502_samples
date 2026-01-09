#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include "btree.h"

void main()
{
    BTree *tree;
    int value;
    unsigned int node_count;
    unsigned int unique_key_count;
    unsigned int i;
    unsigned int seq_id;
    int random_value;
    unsigned int item_count;
    unsigned int start_id;
    unsigned char deleted_10;
    unsigned char deleted_30;

    puts("=== B-tree Demo ===\n");

    /* Create tree */
    tree = btree_create();
    if (!tree)
    {
        puts("Failed to create tree");
        return;
    }

    /* INSERT operations */
    // puts("Inserting manual values...");
    // btree_insert(tree, 10, 100);
    // btree_insert(tree, 20, 200);
    // btree_insert(tree, 30, 300);
    // btree_insert(tree, 40, 400);
    // btree_insert(tree, 50, 500);
    // btree_insert(tree, 5, 50);
    // btree_insert(tree, 25, 250);
    // puts("Manual inserts complete.\n");

    /* INSERT sequential unique entries */
    puts("Inserting sequential unique entries...");
    item_count = 5000;      /* allow full range 0-255 */
    start_id = 0;          /* start from 0 */

    for (i = 0; i < item_count; i++)
    {
        seq_id = i;
        random_value = (int)(rand() << 1);
        btree_insert(tree, seq_id, random_value);
    }
    puts("Sequential inserts complete.\n");

    // puts("Tree structure:");
    // btree_print(tree);
    // putchar('\n');

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
    deleted_10 = btree_delete(tree, 10);
    if (deleted_10)
        puts("Deleted key 10");

    deleted_30 = btree_delete(tree, 30);
    if (deleted_30)
        puts("Deleted key 30");

    value = btree_get(tree, 10);
    if (value == (int)0x8000)
        puts("\nKey 10: not found");
    putchar('\n');

    value = btree_get(tree, 30);
    if (value == (int)0x8000)
        puts("Key 30: not found");
    putchar('\n');

    unique_key_count = (unsigned int)item_count;
    if (deleted_10)
        unique_key_count = (unsigned int)(unique_key_count - 1);
    if (deleted_30)
        unique_key_count = (unsigned int)(unique_key_count - 1);
    node_count = btree_node_count(tree);
    printf("Unique key count: %u\n", unique_key_count);
    printf("Node count: %u\n", node_count);

    putchar('\n');
    puts("Demo complete! xxx");

    /* Cleanup */
    btree_free(tree);
}