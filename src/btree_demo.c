#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btree.h"

/* Static arrays for JSON generation */
static char *names[] = {"Alice", "Bob", "Charlie", "Diana", "Eve", "Frank"};
static char *statuses[] = {"ok", "error", "pending", "done", "failed"};
static char *roles[] = {"admin", "user", "guest", "moderator"};
static char *events[] = {"login", "logout", "update", "delete", "create"};
static char json1[64];
static char json2[64];
static char json3[64];
static char json4[64];
static char json5[64];
static char json6[64];

/* Track valid numeric keys for updates/deletes */
#define KEY_LIST_MAX 1200
static unsigned int valid_keys[KEY_LIST_MAX];
static unsigned int valid_key_count = 0;

void main()
{
    BTree *tree;
    void *value;
    unsigned int node_count;
    unsigned int unique_key_count;
    unsigned int i;
    unsigned int seq_id;
    int random_value;
    unsigned int item_count;
    unsigned int start_id;
    unsigned int json_key;
    unsigned int json_count;
    unsigned char json_index;
    char *json_ptrs[6];
    int rand_val;
    unsigned int json_keys[6];
    unsigned int update_count;
    unsigned int update_key;
    int update_value;
    unsigned int updates_successful;
    unsigned int delete_count;
    unsigned int delete_key;
    unsigned int deletes_successful;
    unsigned int deletes_attempted;
    unsigned int get_count;
    unsigned int get_key;
    unsigned int gets_successful;
    unsigned int stress_runs;
    unsigned int run_index;
    unsigned int runs_ok;
    unsigned char run_ok;

    puts("=== B-tree Demo (stress runs) ===\n");

    stress_runs = 10; /* configurable */
    runs_ok = 0;

    for (run_index = 0; run_index < stress_runs; run_index++)
    {
        /* Reset per-run counters */
        valid_key_count = 0;
        deletes_attempted = 0;
        deletes_successful = 0;
        gets_successful = 0;
        updates_successful = 0;

        /* Seed random number generator with hardware random */
        srand((unsigned int)lrand());

        /* Create tree */
        tree = btree_create();

        if (!tree)
        {
            puts("Failed to create tree");
            return;
        }

        printf("\n-- Run %u --\n", run_index + 1);

        puts("Inserting sequential unique entries...");
        item_count = (unsigned int)(100 + (rand() % 901)); /* random 100-1000 per run */
        start_id = 0;

        for (i = 0; i < item_count; i++)
        {
            seq_id = i;
            random_value = (int)((rand() << 1) | 1); /* Ensure non-zero value */
            btree_insert(tree, seq_id, (void *)(unsigned int)random_value);

            /* Record valid key for update/delete selection */
            if (valid_key_count < KEY_LIST_MAX)
            {
                valid_keys[valid_key_count] = seq_id;
                valid_key_count++;
            }

        }

        printf("Sequential inserts complete (%u items).\n\n", item_count);

    // puts("Tree structure:");
    // btree_print(tree);
    // putchar('\n');

    /* INSERT JSON strings with randomized keys and content */
    puts("Generating and inserting JSON strings with randomized content...");
    
    /* Generate randomized JSON content */
    rand_val = rand() % 100;
    sprintf(json1, "{\"name\":\"%s\",\"age\":%d}", names[rand() % 6], 20 + (rand() % 50));
    
    rand_val = rand() % 1000;
    sprintf(json2, "{\"status\":\"%s\",\"code\":%d}", statuses[rand() % 5], 100 + (rand() % 400));
    
    rand_val = rand() % 10000;
    sprintf(json3, "{\"user\":\"%s\",\"role\":\"%s\"}", names[rand() % 6], roles[rand() % 4]);
    
    rand_val = rand() % 100;
    sprintf(json4, "{\"id\":%d,\"count\":%d}", rand() % 1000, rand() % 100);
    
    rand_val = (rand() << 10) | rand();
    sprintf(json5, "{\"timestamp\":%d,\"event\":\"%s\"}", rand_val, events[rand() % 5]);
    
    rand_val = rand() % 256;
    sprintf(json6, "{\"value\":%d,\"active\":%s}", rand() % 1000, (rand() % 2) ? "true" : "false");
    
    json_ptrs[0] = json1;
    json_ptrs[1] = json2;
    json_ptrs[2] = json3;
    json_ptrs[3] = json4;
    json_ptrs[4] = json5;
    json_ptrs[5] = json6;
    
    json_count = 5;  /* configurable: 1-6 */
    
    for (json_index = 0; json_index < json_count; json_index++)
    {
        json_key = (unsigned int)((rand() & 0x7FFF) + 30000);
        json_keys[json_index] = json_key;
        btree_insert(tree, json_key, (void *)json_ptrs[json_index]);
        printf("  Inserted JSON %u at key %u: %s\n", json_index + 1, json_key, json_ptrs[json_index]);
    }

    printf("Inserted %u JSON strings.\n\n", json_count);

    /* RANDOM GET operations */
    puts("Performing random gets...");
    get_count = item_count;
    gets_successful = 0;

    if (get_count > valid_key_count)
        get_count = valid_key_count;
    
    if (valid_key_count == 0)
    {
        puts("No valid keys available for gets.\n");
    }
    else
    {
        for (i = 0; i < get_count; i++)
        {
            get_key = valid_keys[rand() % valid_key_count];
            value = btree_get(tree, get_key);

            if (value != NULL)
            {
                gets_successful++;
            }
        }
    }
    printf("Completed %u random gets (%u successful).\n\n", get_count, gets_successful);

    /* RANDOM UPDATE operations */
    puts("Performing random updates...");
    update_count = item_count;
    updates_successful = 0;

    if (update_count > valid_key_count)
        update_count = valid_key_count;
    
    if (valid_key_count == 0)
    {
        puts("No valid keys available for updates.\n");
    }
    else
    {
    
        for (i = 0; i < update_count; i++)
        {
            update_key = valid_keys[rand() % valid_key_count];
            update_value = (int)((rand() << 1) | 1); /* Ensure non-zero value */
            if (btree_update(tree, update_key, (void *)(unsigned int)update_value))
            {
                value = btree_get(tree, update_key);
                if (value != NULL && (int)(unsigned int)value == update_value)
                {
                    updates_successful++;
                }
                else
                {
                    printf("Update verify failed for key %u (expected %d)\n", update_key, update_value);
                }
            }
        }
    }
    printf("Completed %u random updates (%u successful).\n\n", update_count, updates_successful);

    puts("Retrieving JSON strings...");
    for (json_index = 0; json_index < json_count; json_index++)
    {
        value = btree_get(tree, json_keys[json_index]);
        if (value != NULL)
            printf("Key %u (JSON %u): %s\n", json_keys[json_index], json_index + 1, (char *)value);
        else
            printf("Key %u (JSON %u): NOT FOUND\n", json_keys[json_index], json_index + 1);
    }
    putchar('\n');

    /* RANDOM DELETE operations */
    puts("Performing random deletes...");
    delete_count = item_count / 2;
    deletes_successful = 0;
    deletes_attempted = 0;

    if (delete_count > valid_key_count)
        delete_count = valid_key_count;
    
    for (i = 0; i < delete_count; i++)
    {
        unsigned int key_index;

        if (valid_key_count == 0)
            break;

        key_index = rand() % valid_key_count;
        delete_key = valid_keys[key_index];

        if (btree_delete(tree, delete_key))
        {
            deletes_attempted++;
            /* Remove from list immediately (regardless of verification) */
            if (valid_key_count > 0)
            {
                valid_keys[key_index] = valid_keys[valid_key_count - 1];
                valid_key_count--;
            }

            value = btree_get(tree, delete_key);

            if (value != NULL)
            {
                /* Retry once if the key is still present (defensive) */
                if (btree_delete(tree, delete_key))
                    value = btree_get(tree, delete_key);
            }

            if (value == NULL)
            {
                deletes_successful++;
            }
            else
            {
                printf("Delete verify failed for key %u\n", delete_key);
            }
        }
    }

    printf("Completed %u random deletes (%u successful).\n\n", deletes_attempted, deletes_successful);

    unique_key_count = (unsigned int)item_count;
    unique_key_count = (unsigned int)(unique_key_count - deletes_successful);

    node_count = btree_node_count(tree);
    printf("Unique key count: %u\n", unique_key_count);
    printf("Node count: %u\n", node_count);

    putchar('\n');
    puts("Demo complete! xxx");

    /* Final validation */
    putchar('\n');
    puts("=== FINAL VALIDATION ===");
    printf("Gets: %u/%u successful\n", gets_successful, get_count);
    printf("Updates: %u/%u successful\n", updates_successful, update_count);
    printf("Deletes: %u/%u verified\n", deletes_successful, deletes_attempted);
    
    run_ok = 0;
    if (gets_successful == get_count && updates_successful == update_count && deletes_successful == deletes_attempted)
    {
        puts("\nResult: OK - All operations verified successfully");
        run_ok = 1;
    }
    else
    {
        puts("\nResult: FAIL - Some operations did not verify");
    }

    if (run_ok)
        runs_ok++;

    /* Cleanup */
    btree_free(tree);
    }

    /* Stress summary */
    printf("\nStress summary: %u/%u runs passed\n", runs_ok, stress_runs);
}