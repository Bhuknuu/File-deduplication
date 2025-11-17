#include "common.h"

// Define CreateSymbolicLinkA if not available
#ifndef SYMBOLIC_LINK_FLAG_FILE
#define SYMBOLIC_LINK_FLAG_FILE 0x0
#endif

#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif

typedef BOOLEAN (WINAPI *CreateSymbolicLinkAFunc)(LPCSTR, LPCSTR, DWORD);

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
            if (remove_file(g->files[j].path)) {
                removed++;
            }
        }
    }
    return removed;
}

int remove_duplicates_except_index(DuplicateResults* results, int index) {
    if (!results || results->count == 0) return 0;
    
    int removed = 0;
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* g = &results->groups[i];
        
        // Validate index for this group
        int keep_idx = index;
        if (keep_idx >= g->count) keep_idx = 0;
        
        for (int j = 0; j < g->count; j++) {
            if (j != keep_idx && remove_file(g->files[j].path)) {
                removed++;
            }
        }
    }
    return removed;
}

bool ensure_directory_exists(const char* path) {
    if (!path) return false;
    
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
        
        // Validate index for this group
        int keep_idx = keep_index;
        if (keep_idx >= g->count) keep_idx = 0;
        
        for (int j = 0; j < g->count; j++) {
            if (j == keep_idx) continue;
            
            const char* filename = strrchr(g->files[j].path, '\\');
            filename = filename ? filename + 1 : g->files[j].path;
            
            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s", dest, filename);
            
            // Handle duplicate filenames in destination
            if (GetFileAttributesA(dest_path) != INVALID_FILE_ATTRIBUTES) {
                char base_name[MAX_PATH_LENGTH];
                const char* ext = strrchr(filename, '.');
                if (ext) {
                    size_t base_len = ext - filename;
                    strncpy(base_name, filename, base_len);
                    base_name[base_len] = '\0';
                    
                    for (int n = 1; n < 1000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s_%d%s", 
                                dest, base_name, n, ext);
                        if (GetFileAttributesA(dest_path) == INVALID_FILE_ATTRIBUTES) {
                            break;
                        }
                    }
                } else {
                    for (int n = 1; n < 1000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s_%d", 
                                dest, filename, n);
                        if (GetFileAttributesA(dest_path) == INVALID_FILE_ATTRIBUTES) {
                            break;
                        }
                    }
                }
            }
            
            if (MoveFileA(g->files[j].path, dest_path)) {
                moved++;
            }
        }
    }
    return moved;
}

int create_hard_links(DuplicateResults* results, int keep_index) {
    if (!results || results->count == 0) return 0;
    
    int link_count = 0;
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* g = &results->groups[i];
        
        // Validate index for this group
        int keep_idx = keep_index;
        if (keep_idx >= g->count) keep_idx = 0;
        
        const char* source = g->files[keep_idx].path;
        for (int j = 0; j < g->count; j++) {
            if (j == keep_idx) continue;
            
            const char* target = g->files[j].path;
            DeleteFileA(target);
            
            if (CreateHardLinkA(target, source, NULL)) {
                link_count++;
            }
        }
    }
    return link_count;
}

int create_symbolic_links(DuplicateResults* results, int keep_index) {
    if (!results || results->count == 0) return 0;
    
    int link_count = 0;
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* g = &results->groups[i];
        
        // Validate index for this group
        int keep_idx = keep_index;
        if (keep_idx >= g->count) keep_idx = 0;
        
        const char* source = g->files[keep_idx].path;
        for (int j = 0; j < g->count; j++) {
            if (j == keep_idx) continue;
            
            const char* target = g->files[j].path;
            DeleteFileA(target);
            
            if (CreateSymbolicLinkA(target, source, 0)) {
                link_count++;
            }
        }
    }
    return link_count;
}