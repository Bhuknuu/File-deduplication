// ============================================================================
// ACTION MODULE - FILE OPERATIONS (DELETE, MOVE)
// ============================================================================

#include "common.h"

bool remove_file(const char* filepath) {
    if (!filepath) return false;
    return DeleteFileA(filepath) != 0;
}

int remove_duplicates_except_first(DuplicateResults* results) {
    if (!results || results->count == 0) return 0;
    
    int removed = 0;
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* g = &results->groups[i];
        for (int j = 1; j < g->count; j++) {
            if (remove_file(g->files[j].path)) removed++;
        }
    }
    return removed;
}

int remove_duplicates_except_index(DuplicateResults* results, int index) {
    if (!results || results->count == 0) return 0;
    
    int removed = 0;
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* g = &results->groups[i];
        for (int j = 0; j < g->count; j++) {
            if (j != index && remove_file(g->files[j].path)) removed++;
        }
    }
    return removed;
}

bool ensure_directory_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return CreateDirectoryA(path, NULL) != 0;
}

int move_duplicates_to_folder(DuplicateResults* results, int keep_index, const char* dest) {
    if (!results || !dest || !ensure_directory_exists(dest)) return 0;
    
    int moved = 0;
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* g = &results->groups[i];
        for (int j = 0; j < g->count; j++) {
            if (j == keep_index) continue;
            
            const char* filename = strrchr(g->files[j].path, '\\');
            filename = filename ? filename + 1 : g->files[j].path;
            
            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s", dest, filename);
            
            if (MoveFileA(g->files[j].path, dest_path)) moved++;
        }
    }
    return moved;
}