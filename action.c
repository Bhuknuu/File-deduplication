/*
 * ACTION.C - File Operations on Duplicates
 * 
 * OPERATIONS:
 * 1. Delete duplicates (keep first)
 * 2. Move duplicates to folder
 * 3. Create hard links (space-saving technique)
 * 
 * DSA CONCEPTS:
 * - Array iteration
 * - String manipulation
 * - File system operations
 */

#include "common.h"

// ============================================================================
// ENSURE DIRECTORY EXISTS
// 
// Creates directory if it doesn't exist
// Returns: true if directory exists/created, false otherwise
// ============================================================================
bool ensure_directory_exists(const char* path) {
    if (!path) return false;
    
    DWORD attr = GetFileAttributesA(path);
    
    // Directory already exists
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    
    // Try to create directory
    return CreateDirectoryA(path, NULL) != 0;
}

// ============================================================================
// REMOVE DUPLICATES - KEEP FIRST FILE
// 
// ALGORITHM:
// For each duplicate group:
//   - Keep files[0] (first occurrence)
//   - Delete files[1], files[2], ..., files[n-1]
// 
// WHY KEEP FIRST?
// - Arbitrary but consistent choice
// - First found is often in primary location
// - User can sort before deletion if needed
// 
// RETURNS: Number of files deleted
// TIME COMPLEXITY: O(n) where n = duplicate files
// ============================================================================
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

// ============================================================================
// MOVE DUPLICATES TO FOLDER
// 
// ALGORITHM:
// For each duplicate group:
//   - Keep files[0] in place
//   - Move files[1..n] to destination folder
//   - Handle filename conflicts by appending numbers
// 
// FILENAME CONFLICT RESOLUTION:
// If file.txt exists in destination:
//   - Try file_1.txt, file_2.txt, ..., file_9999.txt
//   - Preserves original extension
// 
// RETURNS: Number of files moved
// TIME COMPLEXITY: O(n * k) where k = conflict resolution attempts
// ============================================================================
int move_duplicates(DuplicateResults* results, const char* dest_folder) {
    if (!results || !dest_folder) return 0;
    if (results->count == 0) return 0;
    
    // Ensure destination exists
    if (!ensure_directory_exists(dest_folder)) {
        return 0;
    }
    
    int moved = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Move all except first
        for (int j = 1; j < group->count; j++) {
            // Extract filename from path
            const char* filename = strrchr(group->files[j].path, '\\');
            if (!filename) {
                filename = group->files[j].path;
            } else {
                filename++;  // Skip backslash
            }
            
            // Build destination path
            char dest_path[MAX_PATH_LENGTH];
            int len = snprintf(dest_path, MAX_PATH_LENGTH, 
                              "%s\\%s", dest_folder, filename);
            
            if (len >= MAX_PATH_LENGTH - 1) {
                continue;  // Path too long
            }
            
            // Handle conflicts
            if (GetFileAttributesA(dest_path) != INVALID_FILE_ATTRIBUTES) {
                // File exists - append number
                char base_name[MAX_PATH_LENGTH];
                const char* ext = strrchr(filename, '.');
                
                if (ext) {
                    // Split into base and extension
                    size_t base_len = ext - filename;
                    if (base_len >= MAX_PATH_LENGTH) {
                        base_len = MAX_PATH_LENGTH - 1;
                    }
                    
                    strncpy(base_name, filename, base_len);
                    base_name[base_len] = '\0';
                    
                    // Try base_1.ext, base_2.ext, ...
                    for (int n = 1; n < 10000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, 
                                "%s\\%s_%d%s", 
                                dest_folder, base_name, n, ext);
                        
                        if (GetFileAttributesA(dest_path) == 
                            INVALID_FILE_ATTRIBUTES) {
                            break;  // Found available name
                        }
                    }
                } else {
                    // No extension
                    for (int n = 1; n < 10000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, 
                                "%s\\%s_%d", 
                                dest_folder, filename, n);
                        
                        if (GetFileAttributesA(dest_path) == 
                            INVALID_FILE_ATTRIBUTES) {
                            break;
                        }
                    }
                }
            }
            
            // Move file
            if (MoveFileA(group->files[j].path, dest_path)) {
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
            
            // Delete existing file
            DeleteFileA(target);
            
            // Create hard link
            // CreateHardLinkA(link_name, existing_file, security_attributes)
            if (CreateHardLinkA(target, source, NULL)) {
                link_count++;
            }
            // Fails if different drives, not NTFS, etc.
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