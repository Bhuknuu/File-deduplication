#include "filter.h"
#include <errno.h>

DuplicateResults find_duplicates(FileInfo* files, int file_count) {
    DuplicateResults results = {0};
    int max_groups = file_count / 2;  // Maximum possible groups
    
    // Allocate memory for groups
    results.groups = malloc(max_groups * sizeof(DuplicateGroup));
    if (results.groups == NULL) {
        perror("Memory allocation failed");
        return results;
    }
    
    // Temporary array to track which files have been processed
    bool* processed = calloc(file_count, sizeof(bool));
    if (processed == NULL) {
        perror("Memory allocation failed");
        free(results.groups);
        return results;
    }
    
    printf("Finding duplicates...\n");
    
    // Compare each file with every other file
    for (int i = 0; i < file_count; i++) {
        if (processed[i]) continue;  // Skip already processed files
        
        // Create a new group for this file
        DuplicateGroup* group = &results.groups[results.count];
        group->count = 0;
        
        // Allocate memory for files in this group
        group->files = malloc(file_count * sizeof(FileInfo));
        if (group->files == NULL) {
            perror("Memory allocation failed");
            continue;
        }
        
        // Add the current file to the group
        group->files[group->count++] = files[i];
        processed[i] = true;
        
        // Find all duplicates of this file
        for (int j = i + 1; j < file_count; j++) {
            if (processed[j]) continue;  // Skip already processed files
            
            // First compare file sizes (fast check)
            if (files[i].size != files[j].size) continue;
            
            // Then compare hashes (definitive check)
            if (strcmp(files[i].hash, files[j].hash) == 0) {
                // Found a duplicate
                group->files[group->count++] = files[j];
                processed[j] = true;
            }
        }
        
        // If we found duplicates, add this group to results
        if (group->count > 1) {
            results.count++;
        } else {
            // No duplicates found, free the group
            free(group->files);
        }
    }
    
    free(processed);
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
