#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btree.h"

/* Static arrays for JSON generation */
static char *names[] = {"Alice", "Bob", "Charlie", "Diana", "Eve", "Frank"};
static char *statuses[] = {"ok", "error", "pending", "done", "did not complete"};
static char *roles[] = {"admin", "user", "guest", "moderator"};
static char *events[] = {"loginx", "logout", "update", "delete", "create"};
static char json1[64];
static char json2[64];
static char json3[64];
static char json4[64];
static char json5[64];
static char json6[64];

/* Track valid numeric and string keys separately */
#define KEY_LIST_MAX 1200
static unsigned int numeric_keys[KEY_LIST_MAX];
static unsigned int numeric_key_count = 0;
static unsigned int string_keys[10];
static unsigned int string_key_count = 0;

/* Track which runs failed for reporting */
#define MAX_FAILED_RUNS 10
#define MAX_FAILED_OPS 50
static unsigned int failed_runs[MAX_FAILED_RUNS];
static unsigned int failed_run_count = 0;

/* Per-run failure tracking */
struct FailedOp {
    unsigned int run_num;
    unsigned int key;
    char op_type[20];  /* "numeric_update", "string_update", "delete" */
    char reason[64];
};

static struct FailedOp failed_ops[MAX_FAILED_OPS];
static unsigned int failed_ops_count = 0;

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
    unsigned int expected_updates;
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
    unsigned int gets_failed;
    unsigned int numeric_updates_attempted;
    unsigned int numeric_updates_failed;
    unsigned int string_updates_attempted;
    unsigned int string_updates_failed;
    unsigned int stress_runs;
    unsigned int run_index;
    unsigned int runs_ok;
    unsigned char run_ok;

    puts("=== B-tree Demo (stress runs) ===\n");

    stress_runs = 50;
    runs_ok = 0;

    for (run_index = 0; run_index < stress_runs; run_index++)
    {
        /* Reset per-run counters */
        numeric_key_count = 0;
        string_key_count = 0;
        deletes_attempted = 0;
        deletes_successful = 0;
        gets_successful = 0;
        gets_failed = 0;
        updates_successful = 0;
        numeric_updates_attempted = 0;
        numeric_updates_failed = 0;
        string_updates_attempted = 0;
        string_updates_failed = 0;

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
            random_value = (int)(rand() << 1);
            btree_insert(tree, seq_id, (void *)(unsigned int)random_value);

            /* Record numeric key for update/delete selection */
            if (numeric_key_count < KEY_LIST_MAX)
            {
                numeric_keys[numeric_key_count] = seq_id;
                numeric_key_count++;
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
        
        /* Track string key separately */
        if (string_key_count < 10)
        {
            string_keys[string_key_count] = json_key;
            string_key_count++;
        }
    }

    printf("Inserted %u JSON strings.\n\n", json_count);

    /* RANDOM GET operations */
    puts("Performing random gets...");
    get_count = item_count;
    gets_successful = 0;

    if (get_count > numeric_key_count)
        get_count = numeric_key_count;
    
    if (numeric_key_count == 0)
    {
        puts("No valid keys available for gets.\n");
    }
    else
    {
        /* Track failed keys for detailed diagnostics */
        unsigned int failed_get_key;
        void *prev_val;
        void *next_val;
        unsigned int key_in_array;
        unsigned int j;
        
        failed_get_key = 0;
        
        for (i = 0; i < get_count; i++)
        {
            get_key = numeric_keys[rand() % numeric_key_count];
            value = btree_get(tree, get_key);

            if (value != NULL)
            {
                gets_successful++;
            }
            else
            {
                gets_failed++;
                failed_get_key = get_key;  /* Remember first failed key */
                printf("Get failed: key %u not found\n", get_key);
                if (failed_ops_count < MAX_FAILED_OPS)
                {
                    failed_ops[failed_ops_count].run_num = run_index + 1;
                    failed_ops[failed_ops_count].key = get_key;
                    strcpy(failed_ops[failed_ops_count].op_type, "get");
                    
                    /* Try to get adjacent keys for diagnostics */
                    prev_val = (get_key > 0) ? btree_get(tree, get_key - 1) : NULL;
                    next_val = btree_get(tree, get_key + 1);
                    
                    /* Check if key is in our tracking array */
                    key_in_array = 0;
                    for (j = 0; j < numeric_key_count; j++)
                    {
                        if (numeric_keys[j] == get_key)
                        {
                            key_in_array = 1;
                            break;
                        }
                    }
                    
                    sprintf(failed_ops[failed_ops_count].reason, 
                            "NOT FOUND (in_array:%s prev:%s next:%s)",
                            (key_in_array) ? "YES" : "NO",
                            (prev_val != NULL) ? "yes" : "no",
                            (next_val != NULL) ? "yes" : "no");
                    failed_ops_count++;
                }
            }
        }
    }
    printf("Completed %u random gets (%u successful, %u failed).\n\n", get_count, gets_successful, gets_failed);

    /* RANDOM UPDATE operations (numeric and string) */
    puts("Performing random updates...");
    update_count = item_count / 2;
    updates_successful = 0;

    /* Update numeric keys */
    if (update_count > numeric_key_count)
        update_count = numeric_key_count;
    
    expected_updates = update_count + string_key_count;
    
    if (numeric_key_count > 0)
    {
        for (i = 0; i < update_count; i++)
        {
            update_key = numeric_keys[rand() % numeric_key_count];
            update_value = (int)(rand() << 1);
            
            /* Ensure update_value is never 0 (can't be reliably stored as void pointer) */
            if (update_value == 0)
                update_value = 1;
            
            numeric_updates_attempted++;
            if (btree_update(tree, update_key, (void *)(unsigned int)update_value))
            {
                value = btree_get(tree, update_key);
                if (value != NULL && (int)(unsigned int)value == update_value)
                {
                    updates_successful++;
                }
                else
                {
                    numeric_updates_failed++;
                    printf("Numeric update verify failed for key %u (expected %d, got %d)\n", 
                           update_key, update_value, (value ? (int)(unsigned int)value : -1));
                    if (failed_ops_count < MAX_FAILED_OPS)
                    {
                        failed_ops[failed_ops_count].run_num = run_index + 1;
                        failed_ops[failed_ops_count].key = update_key;
                        strcpy(failed_ops[failed_ops_count].op_type, "numeric_update");
                        sprintf(failed_ops[failed_ops_count].reason, "verify failed: expected %d, got %d",
                                update_value, (value ? (int)(unsigned int)value : -1));
                        failed_ops_count++;
                    }
                }
            }
            else
            {
                numeric_updates_failed++;
                printf("Numeric update failed: key %u not found\n", update_key);
                if (failed_ops_count < MAX_FAILED_OPS)
                {
                    failed_ops[failed_ops_count].run_num = run_index + 1;
                    failed_ops[failed_ops_count].key = update_key;
                    strcpy(failed_ops[failed_ops_count].op_type, "numeric_update");
                    strcpy(failed_ops[failed_ops_count].reason, "key not found");
                    failed_ops_count++;
                }
            }
        }
    }
    
    /* Update string keys with new JSON content */
    if (string_key_count > 0)
    {
        for (json_index = 0; json_index < string_key_count && json_index < 6; json_index++)
        {
            /* Generate new random JSON content */
            sprintf(json_ptrs[json_index], "{\"updated\":%d,\"run\":%u}", 
                    rand() % 10000, run_index + 1);
            
            update_key = string_keys[json_index];
            string_updates_attempted++;
            if (btree_update(tree, update_key, (void *)json_ptrs[json_index]))
            {
                value = btree_get(tree, update_key);
                if (value != NULL && strcmp((char *)value, json_ptrs[json_index]) == 0)
                {
                    updates_successful++;
                }
                else
                {
                    string_updates_failed++;
                    printf("String update verify failed for key %u (expected: %s, got: %s)\n", 
                           update_key, json_ptrs[json_index], (char *)value);
                    if (failed_ops_count < MAX_FAILED_OPS)
                    {
                        failed_ops[failed_ops_count].run_num = run_index + 1;
                        failed_ops[failed_ops_count].key = update_key;
                        strcpy(failed_ops[failed_ops_count].op_type, "string_update");
                        sprintf(failed_ops[failed_ops_count].reason, "verify failed: got %s",
                                (value ? (char *)value : "NULL"));
                        failed_ops_count++;
                    }
                }
            }
            else
            {
                string_updates_failed++;
                printf("String update failed: key %u not found\n", update_key);
                if (failed_ops_count < MAX_FAILED_OPS)
                {
                    failed_ops[failed_ops_count].run_num = run_index + 1;
                    failed_ops[failed_ops_count].key = update_key;
                    strcpy(failed_ops[failed_ops_count].op_type, "string_update");
                    strcpy(failed_ops[failed_ops_count].reason, "key not found");
                    failed_ops_count++;
                }
            }
        }
    }
    
    printf("Completed %u random updates (%u successful, %u failed).\n", 
           update_count + string_key_count, updates_successful, 
           numeric_updates_failed + string_updates_failed);
    if (numeric_updates_failed > 0 || string_updates_failed > 0)
    {
        printf("  Numeric: %u/%u successful\n", 
               numeric_updates_attempted - numeric_updates_failed, numeric_updates_attempted);
        printf("  String: %u/%u successful\n", 
               string_updates_attempted - string_updates_failed, string_updates_attempted);
    }
    putchar('\n');

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

    /* RANDOM DELETE operations (only numeric keys) */
    puts("Performing random deletes...");
    delete_count = item_count / 2;
    deletes_successful = 0;
    deletes_attempted = 0;

    if (delete_count > numeric_key_count)
        delete_count = numeric_key_count;
    
    for (i = 0; i < delete_count; i++)
    {
        unsigned int key_index;

        if (numeric_key_count == 0)
            break;

        key_index = rand() % numeric_key_count;
        delete_key = numeric_keys[key_index];

        if (btree_delete(tree, delete_key))
        {
            deletes_attempted++;
            /* Remove from list immediately (regardless of verification) */
            if (numeric_key_count > 0)
            {
                numeric_keys[key_index] = numeric_keys[numeric_key_count - 1];
                numeric_key_count--;
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
                if (failed_ops_count < MAX_FAILED_OPS)
                {
                    failed_ops[failed_ops_count].run_num = run_index + 1;
                    failed_ops[failed_ops_count].key = delete_key;
                    strcpy(failed_ops[failed_ops_count].op_type, "delete");
                    strcpy(failed_ops[failed_ops_count].reason, "key still present after delete");
                    failed_ops_count++;
                }
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
    if (gets_failed > 0)
        printf("  %u gets failed (keys not found)\n", gets_failed);
    printf("Updates: %u/%u successful\n", updates_successful, expected_updates);
    if (numeric_updates_failed > 0 || string_updates_failed > 0)
    {
        printf("  Numeric updates failed: %u\n", numeric_updates_failed);
        printf("  String updates failed: %u\n", string_updates_failed);
    }
    printf("Deletes: %u/%u verified\n", deletes_successful, deletes_attempted);
    
    run_ok = 0;
    if (gets_successful == get_count && updates_successful == expected_updates && deletes_successful == deletes_attempted)
    {
        puts("\nResult: OK - All operations verified successfully");
        run_ok = 1;
    }
    else
    {
        puts("\nResult: FAIL - Some operations did not verify");
        printf("  Validation Details:\n");
        printf("  Gets: %u == %u? %s\n", gets_successful, get_count, 
               (gets_successful == get_count) ? "YES" : "NO");
        printf("  Updates: %u == %u? %s\n", updates_successful, expected_updates, 
               (updates_successful == expected_updates) ? "YES" : "NO");
        printf("  Deletes: %u == %u? %s\n", deletes_successful, deletes_attempted, 
               (deletes_successful == deletes_attempted) ? "YES" : "NO");
        
        /* Record validation failure details */
        if (gets_successful != get_count && failed_ops_count < MAX_FAILED_OPS)
        {
            failed_ops[failed_ops_count].run_num = run_index + 1;
            failed_ops[failed_ops_count].key = 0;  /* Special marker */
            strcpy(failed_ops[failed_ops_count].op_type, "get_count_mismatch");
            sprintf(failed_ops[failed_ops_count].reason, "expected %u, got %u", get_count, gets_successful);
            failed_ops_count++;
        }
        
        if (updates_successful != expected_updates && failed_ops_count < MAX_FAILED_OPS)
        {
            failed_ops[failed_ops_count].run_num = run_index + 1;
            failed_ops[failed_ops_count].key = 0;  /* Special marker */
            strcpy(failed_ops[failed_ops_count].op_type, "update_count_mismatch");
            sprintf(failed_ops[failed_ops_count].reason, "expected %u, got %u (numeric_failed:%u, string_failed:%u)", 
                    expected_updates, updates_successful, numeric_updates_failed, string_updates_failed);
            failed_ops_count++;
        }
        
        if (deletes_successful != deletes_attempted && failed_ops_count < MAX_FAILED_OPS)
        {
            failed_ops[failed_ops_count].run_num = run_index + 1;
            failed_ops[failed_ops_count].key = 0;  /* Special marker */
            strcpy(failed_ops[failed_ops_count].op_type, "delete_count_mismatch");
            sprintf(failed_ops[failed_ops_count].reason, "expected %u, got %u", deletes_attempted, deletes_successful);
            failed_ops_count++;
        }
    }

    if (run_ok)
        runs_ok++;
    else
    {
        if (failed_run_count < MAX_FAILED_RUNS)
        {
            failed_runs[failed_run_count] = run_index + 1;
            failed_run_count++;
        }
    }
    
    printf("Run %u Status: %s (runs_ok now = %u/%u)\n\n", 
           run_index + 1, run_ok ? "PASSED" : "FAILED", runs_ok, run_index + 1);

    /* Cleanup */
    btree_free(tree);
    }

    /* Stress summary */
    printf("\nStress summary: %u/%u runs passed\n", runs_ok, stress_runs);
    
    if (failed_run_count > 0)
    {
        unsigned int i, j;
        printf("\nFailed runs: ");
        for (i = 0; i < failed_run_count; i++)
        {
            printf("%u", failed_runs[i]);
            if (i < failed_run_count - 1)
                printf(", ");
        }
        printf("\n");
        
        printf("\n=== DETAILED FAILURE REPORT ===\n");
        for (i = 0; i < failed_ops_count; i++)
        {
            if (failed_ops[i].run_num <= stress_runs)  /* Only show failures from actual runs */
            {
                printf("Run %u - %s on key %u: %s\n", 
                       failed_ops[i].run_num,
                       failed_ops[i].op_type,
                       failed_ops[i].key,
                       failed_ops[i].reason);
            }
        }
    }
    else
    {
        printf("All operations verified successfully!\n");
    }
}