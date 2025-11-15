// ============================================================================
// FILTER MODULE - DUPLICATE DETECTION AND FILTERING
// ============================================================================

#include "common.h"
#include <ctype.h>

void init_filter_config(FilterConfig* config) {
    if (!config) return;
    memset(config, 0, sizeof(FilterConfig));
    config->hash_algo = HASH_FNV1A;
}

bool add_filter(FilterConfig* config, FilterType type) {
    if (!config || config->count >= MAX_FILTERS) return false;
    config->filters[config->count].type = type;
    config->filters[config->count].enabled = true;
    config->count++;
    return true;
}

const char* get_hash_algorithm_name(HashAlgorithm algo) {
    switch (algo) {
        case HASH_FNV1A: return "FNV-1a (Fast)";
        case HASH_MD5: return "MD5";
        case HASH_SHA1: return "SHA-1";
        case HASH_SHA256: return "SHA-256";
        default: return "Unknown";
    }
}

const char* get_filter_type_name(FilterType type) {
    switch (type) {
        case FILTER_NONE: return "None";
        case FILTER_SIZE: return "File Size";
        case FILTER_NAME: return "File Name Pattern";
        case FILTER_EXTENSION: return "File Extension";
        case FILTER_MODIFIED_DATE: return "Modified Date";
        default: return "Unknown";
    }
}

typedef struct HashNode {
    char hash[HASH_LENGTH];
    int* indices;
    int count;
    int capacity;
    struct HashNode* next;
} HashNode;

static unsigned int hash_string(const char* str) {
    unsigned int h = 5381;
    int c;
    while ((c = *str++)) {
        h = ((h << 5) + h) + c;
    }
    return h % HASH_TABLE_SIZE;
}

static bool passes_filters(const FileInfo* file, const FilterConfig* config) {
    if (!config || config->count == 0) return true;
    
    for (int i = 0; i < config->count; i++) {
        const Filter* f = &config->filters[i];
        if (!f->enabled) continue;
        
        switch (f->type) {
            case FILTER_SIZE:
                if ((f->criteria.size.min_size > 0 && file->size < f->criteria.size.min_size) ||
                    (f->criteria.size.max_size > 0 && file->size > f->criteria.size.max_size))
                    return false;
                break;
                
            case FILTER_NAME: {
                const char* filename = strrchr(file->path, '\\');
                filename = filename ? filename + 1 : file->path;
                if (f->criteria.name.case_sensitive) {
                    if (!strstr(filename, f->criteria.name.pattern)) return false;
                } else {
                    char lower_file[MAX_PATH_LENGTH], lower_pattern[256];
                    strncpy(lower_file, filename, MAX_PATH_LENGTH - 1);
                    strncpy(lower_pattern, f->criteria.name.pattern, 255);
                    for (char* p = lower_file; *p; p++) *p = tolower(*p);
                    for (char* p = lower_pattern; *p; p++) *p = tolower(*p);
                    if (!strstr(lower_file, lower_pattern)) return false;
                }
                break;
            }
            
            case FILTER_EXTENSION: {
                const char* ext = strrchr(file->path, '.');
                if (!ext || _stricmp(ext + 1, f->criteria.ext.extension) != 0)
                    return false;
                break;
            }
            
            case FILTER_MODIFIED_DATE:
                if ((f->criteria.date.min_date > 0 && file->modified < f->criteria.date.min_date) ||
                    (f->criteria.date.max_date > 0 && file->modified > f->criteria.date.max_date))
                    return false;
                break;
                
            default: break;
        }
    }
    return true;
}

DuplicateResults find_duplicates(FileInfo* files, int count, const FilterConfig* config) {
    DuplicateResults results = {0};
    if (!files || count <= 0) return results;
    
    // Apply filters first
    FileInfo* filtered = malloc(count * sizeof(FileInfo));
    if (!filtered) return results;
    
    int filtered_count = 0;
    for (int i = 0; i < count; i++) {
        if (passes_filters(&files[i], config)) {
            filtered[filtered_count++] = files[i];
        }
    }
    
    if (filtered_count < 2) {
        free(filtered);
        return results;
    }
    
    // Build hash table
    HashNode** table = calloc(HASH_TABLE_SIZE, sizeof(HashNode*));
    if (!table) {
        free(filtered);
        return results;
    }
    
    for (int i = 0; i < filtered_count; i++) {
        unsigned int bucket = hash_string(filtered[i].hash);
        
        HashNode* node = table[bucket];
        HashNode* prev = NULL;
        bool found = false;
        
        while (node) {
            if (strcmp(node->hash, filtered[i].hash) == 0) {
                if (node->count >= node->capacity) {
                    node->capacity *= GROWTH_FACTOR;
                    int* new_indices = realloc(node->indices, node->capacity * sizeof(int));
                    if (!new_indices) break;
                    node->indices = new_indices;
                }
                node->indices[node->count++] = i;
                found = true;
                break;
            }
            prev = node;
            node = node->next;
        }
        
        if (!found) {
            node = malloc(sizeof(HashNode));
            if (!node) continue;
            
            strncpy(node->hash, filtered[i].hash, HASH_LENGTH - 1);
            node->hash[HASH_LENGTH - 1] = '\0';
            node->capacity = INITIAL_CAPACITY;
            node->indices = malloc(node->capacity * sizeof(int));
            node->count = 0;
            node->next = NULL;
            
            if (!node->indices) {
                free(node);
                continue;
            }
            
            node->indices[node->count++] = i;
            
            if (prev) prev->next = node;
            else table[bucket] = node;
        }
    }
    
    // Count duplicate groups
    int group_count = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        for (HashNode* n = table[i]; n; n = n->next) {
            if (n->count > 1) group_count++;
        }
    }
    
    // Collect results
    results.groups = malloc(group_count * sizeof(DuplicateGroup));
    if (!results.groups) {
        // Free hash table
        for (int i = 0; i < HASH_TABLE_SIZE; i++) {
            HashNode* n = table[i];
            while (n) {
                HashNode* next = n->next;
                free(n->indices);
                free(n);
                n = next;
            }
        }
        free(table);
        free(filtered);
        return results;
    }
    
    results.count = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode* n = table[i];
        while (n) {
            if (n->count > 1) {
                DuplicateGroup* g = &results.groups[results.count++];
                g->count = n->count;
                g->files = malloc(n->count * sizeof(FileInfo));
                if (g->files) {
                    for (int j = 0; j < n->count; j++) {
                        g->files[j] = filtered[n->indices[j]];
                    }
                }
            }
            HashNode* next = n->next;
            free(n->indices);
            free(n);
            n = next;
        }
    }
    
    free(table);
    free(filtered);
    return results;
}

void free_duplicate_results(DuplicateResults* results) {
    if (!results) return;
    for (int i = 0; i < results->count; i++) {
        free(results->groups[i].files);
    }
    free(results->groups);
    results->groups = NULL;
    results->count = 0;
}