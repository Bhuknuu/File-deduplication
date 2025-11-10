#include "filter.h"
#include <errno.h>

#define HASH_TABLE_SIZE 10007

typedef struct HashNode {
    char hash[HASH_LENGTH];
    int* file_indices;  
    int count;
    int capacity;
    struct HashNode* next;
} HashNode;

static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_TABLE_SIZE;
}

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

DuplicateResults find_duplicates(FileInfo* files, int file_count) {
    DuplicateResults results = {0};
    
    HashNode** hash_table = calloc(HASH_TABLE_SIZE, sizeof(HashNode*));
    if (hash_table == NULL) {
        return results;
    }
  
    for (int i = 0; i < file_count; i++) {
        unsigned int bucket = hash_string(files[i].hash);
        
        HashNode* node = hash_table[bucket];
        HashNode* prev = NULL;
        bool found = false;
        
        while (node != NULL) {
            if (strcmp(node->hash, files[i].hash) == 0) {
                add_to_node(node, i);
                found = true;
                break;
            }
            prev = node;
            node = node->next;
        }
        
        if (!found) {
            HashNode* new_node = malloc(sizeof(HashNode));
            if (new_node == NULL) continue;
            
            strncpy(new_node->hash, files[i].hash, HASH_LENGTH);
            new_node->capacity = 4; 
            new_node->file_indices = malloc(new_node->capacity * sizeof(int));
            new_node->count = 0;
            new_node->next = NULL;
            
            if (new_node->file_indices == NULL) {
                free(new_node);
                continue;
            }
            
            add_to_node(new_node, i);

            if (prev == NULL) {
                hash_table[bucket] = new_node;
            } else {
                prev->next = new_node;
            }
        }
    }

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
   
    results.groups = malloc(group_count * sizeof(DuplicateGroup));
    if (results.groups == NULL) {
        free_hash_table(hash_table);
        return results;
    }
   
    int group_idx = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* node = hash_table[i];
        while (node != NULL) {
            if (node->count > 1) {
                DuplicateGroup* group = &results.groups[group_idx++];
                group->count = node->count;
                group->files = malloc(node->count * sizeof(FileInfo));
                
                if (group->files == NULL) {
                    // In a real app, you might want to handle this error more gracefully
                    continue;
                }
              
                for (int j = 0; j < node->count; j++) {
                    group->files[j] = files[node->file_indices[j]];
                }
            }
            node = node->next;
        }
    }
    
    results.count = group_idx;
    free_hash_table(hash_table);
    
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