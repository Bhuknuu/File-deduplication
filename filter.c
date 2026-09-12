/*
 * FILTER.C - Duplicate Detection using Standard qsort and Linear Grouping
 *
 * Algorithm:
 * 1. Sort files by size, then by hash via stdlib qsort (O(n log n)).
 * 2. Scan contiguously adjacent duplicate runs in O(n).
 * 3. Extract duplicate groups into DuplicateResults.
 */

#include "common.h"

static int compare_file_info(const void* a, const void* b) {
    const FileInfo* fa = (const FileInfo*)a;
    const FileInfo* fb = (const FileInfo*)b;

    // Errors sort to the end
    bool err_a = (strncmp(fa->hash, "ERROR", 5) == 0);
    bool err_b = (strncmp(fb->hash, "ERROR", 5) == 0);
    if (err_a && err_b) return 0;
    if (err_a) return 1;
    if (err_b) return -1;

    // Compare size first (fast integer comparison)
    if (fa->size < fb->size) return -1;
    if (fa->size > fb->size) return 1;

    // Compare hash
    return strcmp(fa->hash, fb->hash);
}

DuplicateResults find_duplicates(FileInfo* files, int count) {
    DuplicateResults results = {0};

    if (!files || count <= 1) {
        return results;
    }

    // Step 1: Sort by size and SHA-256 hash
    qsort(files, count, sizeof(FileInfo), compare_file_info);

    // Step 2: Count duplicate groups
    int group_count = 0;
    for (int i = 0; i < count; ) {
        if (strncmp(files[i].hash, "ERROR", 5) == 0) break;

        int j = i + 1;
        while (j < count && files[j].size == files[i].size && strcmp(files[j].hash, files[i].hash) == 0) {
            j++;
        }

        if (j - i > 1) {
            group_count++;
        }
        i = j;
    }

    if (group_count == 0) {
        return results;
    }

    // Step 3: Extract duplicate groups
    results.groups = (DuplicateGroup*)malloc(group_count * sizeof(DuplicateGroup));
    if (!results.groups) {
        return results;
    }

    results.count = 0;
    results.capacity = group_count;

    for (int i = 0; i < count; ) {
        if (strncmp(files[i].hash, "ERROR", 5) == 0) break;

        int j = i + 1;
        while (j < count && files[j].size == files[i].size && strcmp(files[j].hash, files[i].hash) == 0) {
            j++;
        }

        int grp_len = j - i;
        if (grp_len > 1) {
            DuplicateGroup* grp = &results.groups[results.count++];
            grp->count = grp_len;
            grp->capacity = grp_len;
            grp->files = (FileInfo*)malloc(grp_len * sizeof(FileInfo));
            if (grp->files) {
                memcpy(grp->files, &files[i], grp_len * sizeof(FileInfo));
            }
        }
        i = j;
    }

    return results;
}

void free_duplicate_results(DuplicateResults* results) {
    if (!results || !results->groups) return;

    for (int i = 0; i < results->count; i++) {
        free(results->groups[i].files);
    }

    free(results->groups);
    results->groups = NULL;
    results->count = 0;
    results->capacity = 0;
}
