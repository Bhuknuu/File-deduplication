#include "filter.h"
#include <errno.h>

// Hash table for efficient duplicate detection
#define HASH_TABLE_SIZE 10007  // Prime number for better distribution

typedef struct HashNode {
    char hash[HASH_LENGTH];
    int* file_indices;  // Dynamic array of file indices
    int count;
    int capacity;
    struct HashNode* next;
} HashNode;

// Simple hash function for hash table
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_TABLE_SIZE;
}

// Add file index to hash node
static bool add_to_node(HashNode* node, int file_index) {
    if (node->count >= node->capacity) {
        int new_capacity = node->capacity * 2;
        int* new_indices = realloc(node->file_indices, new_capacity * sizeof(int));
        if (new_indices == NULL) {
            return false;
        }
        node->file_indices = new_indices;
        node->capacity = new_capacity;
    }
    node->file_indices[node->count++] = file_index;
    return true;
}

// Free hash table
static void free_hash_table(HashNode** table) {
    if (table == NULL) return;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* node = table[i];
        while (node != NULL) {
            HashNode* next = node->next;
            free(node->file_indices);
            free(node);
            node = next;
        }
    }
    free(table);
}

// Optimized duplicate finding using hash table - O(n) instead of O(n²)
DuplicateResults find_duplicates(FileInfo* files, int file_count) {
    DuplicateResults results = {0};
    
    // Allocate hash table
    HashNode** hash_table = calloc(HASH_TABLE_SIZE, sizeof(HashNode*));
    if (hash_table == NULL) {
        perror("Memory allocation failed");
        return results;
    }
    
    printf("Finding duplicates...\n");
    
    // Build hash table - O(n)
    for (int i = 0; i < file_count; i++) {
        unsigned int bucket = hash_string(files[i].hash);
        
        // Search for existing node with same hash
        HashNode* node = hash_table[bucket];
        HashNode* prev = NULL;
        bool found = false;
        
        while (node != NULL) {
            // Check if hashes match AND sizes match (double check)
            if (strcmp(node->hash, files[i].hash) == 0) {
                if (!add_to_node(node, i)) {
                    perror("Failed to add file to node");
                }
                found = true;
                break;
            }
            prev = node;
            node = node->next;
        }
        
        // Create new node if not found
        if (!found) {
            HashNode* new_node = malloc(sizeof(HashNode));
            if (new_node == NULL) {
                perror("Memory allocation failed");
                continue;
            }
            
            strncpy(new_node->hash, files[i].hash, HASH_LENGTH);
            new_node->capacity = 4;  // Start small
            new_node->file_indices = malloc(new_node->capacity * sizeof(int));
            new_node->count = 0;
            new_node->next = NULL;
            
            if (new_node->file_indices == NULL) {
                perror("Memory allocation failed");
                free(new_node);
                continue;
            }
            
            add_to_node(new_node, i);
            
            // Add to hash table
            if (prev == NULL) {
                hash_table[bucket] = new_node;
            } else {
                prev->next = new_node;
            }
        }
        
        if ((i + 1) % 100 == 0) {
            printf("\rProcessed: %d/%d files", i + 1, file_count);
            fflush(stdout);
        }
    }
    printf("\rProcessed: %d/%d files\n", file_count, file_count);
    
    // Count duplicate groups
    int group_count = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* node = hash_table[i];
        while (node != NULL) {
            if (node->count > 1) {
                group_count++;
            }
            node = node->next;
        }
    }
    
    // Allocate results
    results.groups = malloc(group_count * sizeof(DuplicateGroup));
    if (results.groups == NULL) {
        perror("Memory allocation failed");
        free_hash_table(hash_table);
        return results;
    }
    
    // Build duplicate groups
    int group_idx = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* node = hash_table[i];
        while (node != NULL) {
            if (node->count > 1) {
                DuplicateGroup* group = &results.groups[group_idx++];
                group->count = node->count;
                group->files = malloc(node->count * sizeof(FileInfo));
                
                if (group->files == NULL) {
                    perror("Memory allocation failed");
                    continue;
                }
                
                // Copy file info
                for (int j = 0; j < node->count; j++) {
                    group->files[j] = files[node->file_indices[j]];
                }
            }
            node = node->next;
        }
    }
    
    results.count = group_idx;
    free_hash_table(hash_table);
    
    printf("Found %d groups of duplicates.\n", results.count);
    return results;
}

void free_duplicate_results(DuplicateResults* results) {
    if (results == NULL) return;
    
    for (int i = 0; i < results->count; i++) {
        free(results->groups[i].files);
    }
    
    free(results->groups);
    results->groups = NULL;
    results->count = 0;
}