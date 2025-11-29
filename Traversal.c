/*
 * TRAVERSAL.C - File System Scanning and Hashing
 * 
 * DSA CONCEPTS DEMONSTRATED:
 * 1. Tree Traversal (Directory = Tree)
 * 2. Depth-First Search (DFS)
 * 3. Recursion
 * 4. String Manipulation
 * 5. Hash Functions (FNV-1a)
 */

#include "common.h"

// ============================================================================
// DIRECTORY LIST INITIALIZATION
// ============================================================================
void init_directory_list(DirectoryList* list) {
    if (!list) return;
    memset(list, 0, sizeof(DirectoryList));
    list->include_subdirs = true;
}

// ============================================================================
// ADD DIRECTORY TO LIST
// 
// Returns: true if added, false if list full or invalid
// ============================================================================
bool add_directory(DirectoryList* list, const char* path) {
    if (!list || !path) return false;
    if (list->count >= MAX_DIRECTORIES) return false;
    if (strlen(path) >= MAX_PATH_LENGTH) return false;
    
    strncpy(list->paths[list->count], path, MAX_PATH_LENGTH - 1);
    list->paths[list->count][MAX_PATH_LENGTH - 1] = '\0';
    list->count++;
    
    return true;
}

// ============================================================================
// EXCLUSION LIST INITIALIZATION
// ============================================================================
void init_exclusion_list(ExclusionList* list) {
    if (!list) return;
    memset(list, 0, sizeof(ExclusionList));
}

// ============================================================================
// ADD EXCLUSION PATH
// ============================================================================
bool add_exclusion(ExclusionList* list, const char* path) {
    if (!list || !path) return false;
    if (list->count >= MAX_EXCLUSIONS) return false;
    if (strlen(path) >= MAX_PATH_LENGTH) return false;
    
    strncpy(list->paths[list->count], path, MAX_PATH_LENGTH - 1);
    list->paths[list->count][MAX_PATH_LENGTH - 1] = '\0';
    list->count++;
    
    return true;
}

// ============================================================================
// CHECK IF PATH IS EXCLUDED
// 
// Uses prefix matching - if path starts with exclusion, skip it
// ============================================================================
bool is_excluded(const ExclusionList* list, const char* path) {
    if (!list || !path) return false;
    
    for (int i = 0; i < list->count; i++) {
        size_t excl_len = strlen(list->paths[i]);
        if (strnicmp(path, list->paths[i], excl_len) == 0) {
            // Check if it's an exact match or has a path separator
            if (strlen(path) == excl_len || path[excl_len] == '\\' || path[excl_len] == '/') {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// CONVERT WINDOWS FILETIME TO UNIX TIMESTAMP
// 
// Windows: 100-nanosecond intervals since Jan 1, 1601
// Unix: Seconds since Jan 1, 1970
// ============================================================================
static time_t FileTimeToTimeT(const FILETIME* ft) {
    if (!ft) return 0;
    
    const long long EPOCH_DIFF = 116444736000000000LL;
    
    LARGE_INTEGER li;
    li.LowPart = ft->dwLowDateTime;
    li.HighPart = ft->dwHighDateTime;
    
    return (time_t)((li.QuadPart - EPOCH_DIFF) / 10000000LL);
}

// ============================================================================
// FNV-1a HASH FUNCTION
// 
// ALGORITHM EXPLANATION:
// FNV-1a (Fowler-Noll-Vo) is a non-cryptographic hash function
// 
// STEPS:
// 1. hash = FNV_OFFSET_BASIS (large prime: 14695981039346656037)
// 2. For each byte:
//    a. hash = hash XOR byte
//    b. hash = hash * FNV_PRIME (1099511628211)
// 3. Return 64-bit hash
// 
// PROPERTIES:
// - Fast: Only XOR and multiplication
// - Good avalanche effect: Small input change → large hash change
// - Non-cryptographic: Don't use for security!
// - Collision resistant for practical use
// 
// TIME COMPLEXITY: O(n) where n = bytes to hash
// SPACE COMPLEXITY: O(1)
// 
// WHY FNV-1a?
// - Simple to implement (good for learning)
// - Fast performance
// - Good distribution properties
// - Better than simple additive or XOR hashing
// ============================================================================
void compute_hash(const char* filename, char* output, ScanMode mode) {
    if (!filename || !output) {
        if (output) strcpy(output, "ERROR_NULL");
        return;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        strcpy(output, "ERROR_OPEN");
        return;
    }
    
    // Determine bytes to hash
    size_t bytes_to_hash = (mode == SCAN_QUICK) ? QUICK_HASH_SIZE : 0;
    
    // Initialize FNV-1a
    uint64_t hash = FNV_OFFSET_BASIS;
    unsigned char buffer[READ_BUFFER_SIZE];
    size_t total_read = 0;
    size_t bytes_read;
    
    // Read and hash file in chunks
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        // Stop if we've read enough (quick mode)
        if (bytes_to_hash > 0 && total_read >= bytes_to_hash) {
            break;
        }
        
        // FNV-1a: XOR then multiply
        for (size_t i = 0; i < bytes_read; i++) {
            hash ^= buffer[i];
            hash *= FNV_PRIME;
        }
        
        total_read += bytes_read;
    }
    
    fclose(file);
    
    // Convert to hexadecimal string
    sprintf(output, "%016llx", (unsigned long long)hash);
}

// ============================================================================
// RECURSIVE DIRECTORY SCANNER (INTERNAL)
// 
// DSA CONCEPT: Depth-First Search (DFS) on Tree
// 
// DIRECTORY STRUCTURE AS TREE:
//   C:\
//   ├── folder1/
//   │   ├── file1.txt
//   │   └── subfolder/
//   │       └── file2.txt
//   └── folder2/
//       └── file3.txt
// 
// TRAVERSAL: Pre-order DFS
// 1. Visit node (process files in directory)
// 2. Recurse left child (first subdirectory)
// 3. Recurse right sibling (next subdirectory)
// 
// TIME COMPLEXITY: O(n) where n = total files + directories
// SPACE COMPLEXITY: O(d) where d = maximum directory depth (recursion stack)
// 
// ALGORITHM:
// 1. Open directory
// 2. For each item:
//    - Skip "." and ".."
//    - If directory: recurse if enabled
//    - If file: collect metadata and hash
// 3. Close directory
// ============================================================================
static int scan_directory_internal(
    const char* path,
    FileInfo* files,
    int current_count,
    int max_files,
    bool recurse,
    ScanMode mode,
    const ExclusionList* exclusions
) {
    // Build search pattern
    char search_path[MAX_PATH_LENGTH];
    int len = snprintf(search_path, MAX_PATH_LENGTH, "%s\\*", path);
    
    if (len >= MAX_PATH_LENGTH - 1) {
        return current_count;
    }
    
    // Check exclusions
    if (is_excluded(exclusions, path)) {
        return current_count;
    }
    
    // Start enumeration
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path, &ffd);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return current_count;
    }
    
    int count = current_count;
    
    // Iterate through directory
    do {
        // Skip . and ..
        if (strcmp(ffd.cFileName, ".") == 0 || 
            strcmp(ffd.cFileName, "..") == 0) {
            continue;
        }
        
        // Build full path
        char full_path[MAX_PATH_LENGTH];
        len = snprintf(full_path, MAX_PATH_LENGTH, "%s\\%s", path, ffd.cFileName);
        
        if (len >= MAX_PATH_LENGTH - 1) {
            continue;
        }
        
        // Check if directory
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            if (recurse && count < max_files) {
                count = scan_directory_internal(
                    full_path, files, count, max_files,
                    true, mode, exclusions
                );
            }
        } else {
            // Process file
            if (count >= max_files) {
                break;
            }
            
            // Update progress
            EnterCriticalSection(&g_dataLock);
            g_progress.files_scanned++;
            LeaveCriticalSection(&g_dataLock);
            
            // Store path
            strncpy(files[count].path, full_path, MAX_PATH_LENGTH - 1);
            files[count].path[MAX_PATH_LENGTH - 1] = '\0';
            
            // Get size (combine high and low parts)
            files[count].size = ((long long)ffd.nFileSizeHigh << 32) | 
                                ffd.nFileSizeLow;
            
            // Get modification time
            files[count].modified = FileTimeToTimeT(&ffd.ftLastWriteTime);
            
            // Compute hash
            compute_hash(full_path, files[count].hash, mode);
            
            count++;
        }
        
    } while (FindNextFileA(hFind, &ffd) && count < max_files);
    
    FindClose(hFind);
    return count;
}

// ============================================================================
// PUBLIC INTERFACE: SCAN MULTIPLE DIRECTORIES
// 
// Scans all directories in configuration
// Returns: Number of files found
// ============================================================================
int scan_directories(const ScanConfig* config, FileInfo* files, int max_files) {
    if (!config || !files || max_files <= 0) return 0;
    
    // Initialize progress
    EnterCriticalSection(&g_dataLock);
    g_progress.files_scanned = 0;
    g_progress.current_percent = 0;
    g_progress.is_complete = false;
    LeaveCriticalSection(&g_dataLock);
    
    int total = 0;
    
    // Scan each directory
    for (int i = 0; i < config->directories.count && total < max_files; i++) {
        total = scan_directory_internal(
            config->directories.paths[i],
            files,
            total,
            max_files,
            config->directories.include_subdirs,
            config->scan_mode,
            &config->exclusions
        );
    }
    
    // Mark complete
    EnterCriticalSection(&g_dataLock);
    g_progress.is_complete = true;
    g_progress.current_percent = 100;
    LeaveCriticalSection(&g_dataLock);
    
    return total;
}

// ============================================================================
// UTILITY: GET SCAN MODE NAME
// ============================================================================
const char* get_scan_mode_name(ScanMode mode) {
    switch (mode) {
        case SCAN_QUICK:     return "Quick Scan";
        case SCAN_THOROUGH:  return "Thorough Scan";
        default:             return "Unknown";
    }
}

// ============================================================================
// UTILITY: GET SCAN MODE DESCRIPTION
// ============================================================================
const char* get_scan_mode_description(ScanMode mode) {
    switch (mode) {
        case SCAN_QUICK:
            return "Fast: Hashes first 1MB of each file";
        case SCAN_THOROUGH:
            return "Accurate: Hashes entire file";
        default:
            return "";
    }
}

// ============================================================================
// UTILITY: FORMAT FILE SIZE FOR DISPLAY
// 
// Converts bytes to human-readable format
// ============================================================================
void format_file_size(long long bytes, char* output, int output_size) {
    if (!output || output_size <= 0) return;
    
    if (bytes < 1024) {
        snprintf(output, output_size, "%lld B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(output, output_size, "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024LL * 1024 * 1024) {
        snprintf(output, output_size, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(output, output_size, "%.2f GB", 
                bytes / (1024.0 * 1024.0 * 1024.0));
    }
}