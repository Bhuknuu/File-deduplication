/*
 * FILTER.C - Duplicate Detection using Hash Table
 * 
 * DSA CONCEPTS DEMONSTRATED:
 * 1. Hash Table with Separate Chaining
 * 2. Dynamic Arrays with Growth Strategy
 * 3. Collision Resolution
 * 4. String Hashing (DJB2 Algorithm)
 * 5. Load Factor Analysis
 * 
 * ALGORITHM: Find duplicates in O(n) average time
 */

#include "common.h"

// ============================================================================
// HASH TABLE NODE STRUCTURE
// 
// Each node in the linked list represents files with the same hash value
// ============================================================================
typedef struct HashNode {
    char hash[HASH_LENGTH];
    int* file_indices;
    int count;
    int capacity;
    struct HashNode* next;
} HashNode;

// ============================================================================
// DJB2 STRING HASH FUNCTION
// 
// PURPOSE: Convert hash string to table index
// 
// ALGORITHM:
// 1. Initialize hash = 5381 (magic number)
// 2. For each character: hash = hash * 33 + character
// 3. Take modulo HASH_TABLE_SIZE
// 
// WHY THIS WORKS:
// - Multiplication by 33 (prime-like) gives good distribution
// - Left shift by 5 (hash << 5) = hash * 32, add hash = hash * 33
// - Fast bit operations instead of multiplication
// 
// TIME COMPLEXITY: O(k) where k = string length
// SPACE COMPLEXITY: O(1)
// ============================================================================
static unsigned int hash_string_to_index(const char* str) {
    if (!str) return 0;
    
    unsigned int hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    
    return hash % HASH_TABLE_SIZE;
}

// ============================================================================
// GROW DYNAMIC ARRAY (DOUBLING STRATEGY)
// 
// DSA CONCEPT: Dynamic array growth
// 
// STRATEGY: Double capacity when full
// - Amortized time complexity: O(1) per insertion
// - Total copies over n insertions: n/2 + n/4 + n/8 + ... < n
// - Trade-off: Space vs Time
// ============================================================================
static bool grow_array_if_needed(HashNode* node) {
    if (!node) return false;
    
    if (node->count >= node->capacity) {
        int new_capacity = node->capacity * 2;
        int* new_array = (int*)realloc(
            node->file_indices,
            new_capacity * sizeof(int)
        );
        
        if (!new_array) return false;
        
        node->file_indices = new_array;
        node->capacity = new_capacity;
    }
    
    return true;
}

// ============================================================================
// CREATE NEW HASH NODE
// 
// Initializes a new node with given file index
// ============================================================================
static HashNode* create_hash_node(const char* hash, int file_idx) {
    HashNode* node = (HashNode*)malloc(sizeof(HashNode));
    if (!node) return NULL;
    
    strncpy(node->hash, hash, HASH_LENGTH - 1);
    node->hash[HASH_LENGTH - 1] = '\0';
    
    node->capacity = 4;  // Initial capacity
    node->file_indices = (int*)malloc(node->capacity * sizeof(int));
    
    if (!node->file_indices) {
        free(node);
        return NULL;
    }
    
    node->file_indices[0] = file_idx;
    node->count = 1;
    node->next = NULL;
    
    return node;
}

// ============================================================================
// FREE HASH TABLE
// 
// Deallocates all nodes in the hash table
// ============================================================================
static void free_hash_table(HashNode** table) {
    if (!table) return;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* node = table[i];
        while (node) {
            HashNode* next = node->next;
            free(node->file_indices);
            free(node);
            node = next;
        }
    }
    free(table);
}

// ============================================================================
// FIND DUPLICATES USING HASH TABLE
// 
// ALGORITHM OVERVIEW:
// Phase 1: Build hash table
//    - For each file, compute bucket index
//    - Search chain for matching hash
//    - Add to existing node or create new node
// 
// Phase 2: Count duplicate groups
//    - Traverse table, count nodes with 2+ files
// 
// Phase 3: Extract results
//    - Copy file information to results structure
// 
// TIME COMPLEXITY:
// - Average: O(n) where n = number of files
// - Worst: O(n²) if all hashes collide (extremely unlikely)
// 
// SPACE COMPLEXITY: O(n)
// 
// COLLISION HANDLING:
// - Primary: Separate chaining (linked lists)
// - Secondary: Size verification (prevents false positives)
// ============================================================================
DuplicateResults find_duplicates(FileInfo* files, int count) {
    DuplicateResults results = {0};
    
    // Validation
    if (!files || count <= 1) {
        return results;
    }
    
    // Allocate hash table
    HashNode** table = (HashNode**)calloc(HASH_TABLE_SIZE, sizeof(HashNode*));
    if (!table) {
        return results;
    }
    
    // ========================================================================
    // PHASE 1: BUILD HASH TABLE
    // ========================================================================
    for (int i = 0; i < count; i++) {
        // Skip error hashes
        if (strncmp(files[i].hash, "ERROR", 5) == 0) {
            continue;
        }
        
        // Get bucket index
        unsigned int bucket = hash_string_to_index(files[i].hash);
        
        // Search chain for matching hash
        HashNode* current = table[bucket];
        HashNode* prev = NULL;
        bool found = false;
        
        while (current) {
            if (strcmp(current->hash, files[i].hash) == 0) {
                // Verify file size to prevent hash collisions
                if (files[i].size == files[current->file_indices[0]].size) {
                    // Add to existing node
                    if (grow_array_if_needed(current)) {
                        current->file_indices[current->count++] = i;
                        found = true;
                    }
                }
                break;
            }
            prev = current;
            current = current->next;
        }
        
        // Create new node if not found
        if (!found) {
            HashNode* new_node = create_hash_node(files[i].hash, i);
            if (new_node) {
                // Insert at end of chain
                if (prev) {
                    prev->next = new_node;
                } else {
                    table[bucket] = new_node;
                }
            }
        }
    }
    
    // ========================================================================
    // PHASE 2: COUNT DUPLICATE GROUPS
    // ========================================================================
    int group_count = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        for (HashNode* node = table[i]; node; node = node->next) {
            if (node->count > 1) {
                group_count++;
            }
        }
    }
    
    // No duplicates found
    if (group_count == 0) {
        free_hash_table(table);
        return results;
    }
    
    // ========================================================================
    // PHASE 3: EXTRACT DUPLICATE GROUPS
    // ========================================================================
    results.groups = (DuplicateGroup*)malloc(group_count * sizeof(DuplicateGroup));
    if (!results.groups) {
        free_hash_table(table);
        return results;
    }
    
    results.count = 0;
    results.capacity = group_count;
    
    // Copy file information
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* node = table[i];
        while (node) {
            if (node->count > 1) {
                DuplicateGroup* group = &results.groups[results.count];
                
                group->files = (FileInfo*)malloc(node->count * sizeof(FileInfo));
                
                if (group->files) {
                    group->count = node->count;
                    group->capacity = node->count;
                    
                    // Copy file data
                    for (int j = 0; j < node->count; j++) {
                        int file_idx = node->file_indices[j];
                        group->files[j] = files[file_idx];
                    }
                    
                    results.count++;
                }
            }
            node = node->next;
        }
    }
    
    free_hash_table(table);
    return results;
}

// ============================================================================
// FREE DUPLICATE RESULTS
// 
// Deallocates all memory used by results structure
// ============================================================================
void free_duplicate_results(DuplicateResults* results) {
    if (!results) return;
    
    for (int i = 0; i < results->count; i++) {
        free(results->groups[i].files);
    }
    
    free(results->groups);
    results->groups = NULL;
    results->count = 0;
    results->capacity = 0;
}

/*
 * ============================================================================
 * PERFORMANCE ANALYSIS FOR DSA PROJECT
 * ============================================================================
 * 
 * HASH TABLE METRICS:
 * -------------------
 * Load Factor (α) = n / m
 *   where n = number of files, m = HASH_TABLE_SIZE (50021)
 * 
 * Example: 10,000 files → α ≈ 0.2 (excellent distribution)
 * 
 * COLLISION PROBABILITY:
 * ----------------------
 * With good hash function and α < 1:
 * - Average chain length ≈ α
 * - Most buckets have 0-1 items
 * - Few long chains
 * 
 * COMPLEXITY ANALYSIS:
 * --------------------
 * Insert into hash table:
 *   - Average: O(1)
 *   - Worst: O(n) - all collide in one bucket
 * 
 * Search in hash table:
 *   - Average: O(1)
 *   - Worst: O(n)
 * 
 * Overall algorithm:
 *   - Average: O(n) for n files
 *   - Worst: O(n²)
 * 
 * DYNAMIC ARRAY AMORTIZATION:
 * ---------------------------
 * Doubling strategy:
 *   - Single insertion: O(1) amortized
 *   - Total cost over n insertions: O(n)
 *   - Proof: 1 + 2 + 4 + 8 + ... + n < 2n
 * 
 * MEMORY USAGE:
 * -------------
 * - Hash table: 50,021 * 8 bytes ≈ 400 KB
 * - Nodes: ~n * sizeof(HashNode) ≈ n * 56 bytes
 * - Index arrays: ~n * 4 bytes
 * - Total: O(n) space
 * 
 * COLLISION PREVENTION:
 * ---------------------
 * 1. Prime table size (50021) reduces patterns
 * 2. Good hash function (DJB2) distributes uniformly
 * 3. Size check eliminates false positives
 * 4. 64-bit hash → collision probability ≈ 1 in 10^19
 * 
 * ============================================================================
 */