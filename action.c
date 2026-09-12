#include "common.h"

bool ensure_directory_exists(const char* path) {
    if (!path) return false;
    
    DWORD attr = GetFileAttributesA(path);
    
    // Directory already exists
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    
    // Try to create directory recursively
    int res = SHCreateDirectoryExA(NULL, path, NULL);
    return (res == ERROR_SUCCESS || res == ERROR_ALREADY_EXISTS);
}


int remove_duplicates_keep_first(DuplicateResults* results) {
    if (!results || results->count == 0) return 0;
    
    int removed = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Delete all except first (index 0)
        for (int j = 1; j < group->count; j++) {
            if (DeleteFileA(group->files[j].path)) {
                removed++;
            }
            // If delete fails, continue with next file
        }
    }
    
    return removed;
}

int move_duplicates(DuplicateResults* results, const char* dest_folder) {
    return move_duplicates_safe(results, dest_folder, NULL, false);
}

// ============================================================================
// MOVE ALL DUPLICATES ORGANIZED
// ============================================================================
int move_all_duplicates_organized(DuplicateResults* results, const char* dest_folder) {
    if (!results || !dest_folder || results->count == 0) return 0;

    if (!ensure_directory_exists(dest_folder)) return 0;

    int moved = 0;

    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        if (group->count < 2) continue;

        // Determine where duplicates land
        char group_dest[MAX_PATH_LENGTH];
        int duplicate_count = group->count - 1;  // files[0] is kept

        if (duplicate_count > 1) {
            // Many duplicates: build a named subfolder from the original's filename
            const char* orig_name = strrchr(group->files[0].path, '\\');
            orig_name = orig_name ? orig_name + 1 : group->files[0].path;

            // Strip extension for subfolder name
            char subfolder_name[MAX_PATH_LENGTH];
            const char* ext = strrchr(orig_name, '.');
            if (ext) {
                size_t base_len = (size_t)(ext - orig_name);
                if (base_len >= MAX_PATH_LENGTH) base_len = MAX_PATH_LENGTH - 1;
                strncpy(subfolder_name, orig_name, base_len);
                subfolder_name[base_len] = '\0';
            } else {
                strncpy(subfolder_name, orig_name, MAX_PATH_LENGTH - 1);
                subfolder_name[MAX_PATH_LENGTH - 1] = '\0';
            }

            // Build subfolder path, handle name collision with existing folders
            snprintf(group_dest, MAX_PATH_LENGTH, "%s\\%s", dest_folder, subfolder_name);
            if (GetFileAttributesA(group_dest) != INVALID_FILE_ATTRIBUTES) {
                for (int n = 1; n < 10000; n++) {
                    snprintf(group_dest, MAX_PATH_LENGTH, "%s\\%s_%d", dest_folder, subfolder_name, n);
                    if (GetFileAttributesA(group_dest) == INVALID_FILE_ATTRIBUTES) break;
                }
            }

            if (!ensure_directory_exists(group_dest)) continue;
        } else {
            // Single duplicate: goes flat into dest_folder
            strncpy(group_dest, dest_folder, MAX_PATH_LENGTH - 1);
            group_dest[MAX_PATH_LENGTH - 1] = '\0';
        }

        // Move each duplicate (skip files[0] = the original to keep)
        for (int j = 1; j < group->count; j++) {
            const char* src_path = group->files[j].path;
            const char* filename = strrchr(src_path, '\\');
            filename = filename ? filename + 1 : src_path;

            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s", group_dest, filename);

            // Collision resolution
            if (GetFileAttributesA(dest_path) != INVALID_FILE_ATTRIBUTES) {
                char base_name[MAX_PATH_LENGTH];
                const char* fext = strrchr(filename, '.');
                if (fext) {
                    size_t base_len = (size_t)(fext - filename);
                    if (base_len >= MAX_PATH_LENGTH) base_len = MAX_PATH_LENGTH - 1;
                    strncpy(base_name, filename, base_len);
                    base_name[base_len] = '\0';
                    for (int n = 1; n < 10000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s_%d%s", group_dest, base_name, n, fext);
                        if (GetFileAttributesA(dest_path) == INVALID_FILE_ATTRIBUTES) break;
                    }
                } else {
                    for (int n = 1; n < 10000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s_%d", group_dest, filename, n);
                        if (GetFileAttributesA(dest_path) == INVALID_FILE_ATTRIBUTES) break;
                    }
                }
            }

            if (MoveFileA(src_path, dest_path)) {
                moved++;
            }
        }
    }

    return moved;
}

// ============================================================================
// CREATE HARD LINKS
// 
// CONCEPT: Hard link = multiple directory entries → same file data
// 
// HOW IT WORKS:
// - File system stores file content (inode) separately from names
// - Hard link creates another name pointing to same inode
// - Deleting one name doesn't delete data until all names deleted
// 
// EXAMPLE:
// Original: C:\folder1\file.txt (100 MB)
// Hard link: C:\folder2\file.txt (0 additional bytes)
// Both paths access the SAME 100 MB on disk
// 
// BENEFITS:
// - Saves disk space (1 GB duplicates → 1 GB total, not 2 GB)
// - Instant "duplication" (no copy time)
// - Changes to one affect all (same file)
// - Transparent to applications
// 
// LIMITATIONS:
// - Must be on same volume (C:\ → C:\ works, C:\ → D:\ doesn't)
// - NTFS only (not FAT32)
// - Files only (not directories in Windows)
// 
// ALGORITHM:
// For each duplicate group:
//   - Keep files[0] as source
//   - Delete files[1..n]
//   - Create hard links from deleted locations to files[0]
// 
// RETURNS: Number of hard links created
// TIME COMPLEXITY: O(n) where n = duplicate files
// ============================================================================
int create_hard_links(DuplicateResults* results) {
    if (!results || results->count == 0) return 0;
    
    int link_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Source is first file
        const char* source = group->files[0].path;
        
        // Create links for all others
        for (int j = 1; j < group->count; j++) {
            const char* target = group->files[j].path;
            
            // Build temp path
            char temp_path[MAX_PATH_LENGTH];
            int len = snprintf(temp_path, MAX_PATH_LENGTH, "%s.dedup_tmp_%d_%d", target, i, j);
            if (len >= MAX_PATH_LENGTH - 1) continue;
            
            // Rename to temp
            if (!MoveFileA(target, temp_path)) continue;
            
            // Create hard link
            // CreateHardLinkA(link_name, existing_file, security_attributes)
            if (CreateHardLinkA(target, source, NULL)) {
                DeleteFileA(temp_path);
                link_count++;
            } else {
                // Restore original
                MoveFileA(temp_path, target);
            }
        }
    }
    
    return link_count;
}

/*
 * ============================================================================
 * HARD LINK DEEP DIVE FOR DSA PROJECT
 * ============================================================================
 * 
 * FILE SYSTEM STRUCTURE:
 * ----------------------
 * Traditional view: Filename → File Data
 * Actual structure: Filename → Inode → File Data
 * 
 * INODE: Index node containing:
 * - File size
 * - Permissions
 * - Timestamps
 * - Pointers to data blocks
 * - Reference count (number of hard links)
 * 
 * HARD LINK VS SYMBOLIC LINK:
 * ---------------------------
 * Hard Link:
 *   - Direct pointer to inode
 *   - Same inode as original
 *   - No extra storage (just directory entry)
 *   - Works even if original deleted
 *   - Same volume only
 * 
 * Symbolic Link (Shortcut):
 *   - Separate file containing path
 *   - Different inode
 *   - Uses extra storage
 *   - Breaks if original deleted
 *   - Works across volumes
 * 
 * REFERENCE COUNTING:
 * -------------------
 * When hard link created: ref_count++
 * When hard link deleted: ref_count--
 * When ref_count == 0: Delete file data
 * 
 * EXAMPLE SCENARIO:
 * -----------------
 * Initial state:
 *   File A: 100 MB (ref_count = 1)
 *   File B: 100 MB (ref_count = 1, duplicate of A)
 *   Total disk usage: 200 MB
 * 
 * After hard linking B to A:
 *   File A: 100 MB (ref_count = 2)
 *   File B: Hard link to A (ref_count = 2)
 *   Total disk usage: 100 MB (saved 100 MB!)
 * 
 * After deleting B:
 *   File A: 100 MB (ref_count = 1)
 *   Total disk usage: 100 MB (data still exists)
 * 
 * SPACE SAVINGS CALCULATION:
 * --------------------------
 * Original: n duplicate files × file_size = n × file_size
 * With hard links: 1 × file_size (only one copy)
 * Savings: (n - 1) × file_size
 * 
 * Example: 5 copies of 200 MB file
 * Original: 5 × 200 MB = 1000 MB
 * With links: 1 × 200 MB = 200 MB
 * Savings: 800 MB (80%)
 * 
 * WHY THIS MATTERS FOR DSA:
 * -------------------------
 * - Demonstrates file system internals
 * - Shows reference counting (used in garbage collection)
 * - Illustrates indirection (pointer to pointer concept)
 * - Real-world application of data structures
 * 
 * ============================================================================
 */
