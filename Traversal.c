/*
 * TRAVERSAL.C - File System Scanning and Hashing
 * 
 * DSA CONCEPTS DEMONSTRATED:
 * 1. Tree Traversal (Directory = Tree)
 * 2. Depth-First Search (DFS) 
 * 3. Recursion
 * 4. String Manipulation
 * 5. Hash Functions (SHA-256 via CNG BCrypt API)
 */

#include "common.h"
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")

// ============================================================================
// CACHE IMPLEMENTATION (Sorted Array + Binary Search)
// ============================================================================
typedef struct {
    char path[MAX_PATH_LENGTH];
    long long size;
    time_t modified;
    char hash[HASH_LENGTH];
} CacheEntry;

static CacheEntry* g_cache = NULL;
static int g_cache_count = 0;
static int g_cache_capacity = 0;

static int compare_cache_entry(const void* a, const void* b) {
    return strcmp(((const CacheEntry*)a)->path, ((const CacheEntry*)b)->path);
}

static void load_cache(void) {
    FILE* f = fopen("dedup_cache.txt", "r");
    if (!f) return;

    g_cache_capacity = 128;
    g_cache = (CacheEntry*)malloc(g_cache_capacity * sizeof(CacheEntry));
    if (!g_cache) { fclose(f); return; }
    g_cache_count = 0;

    char line[MAX_PATH_LENGTH + 256];
    while (fgets(line, sizeof(line), f)) {
        char* p = strrchr(line, '\n'); if (p) *p = 0;
        char* context = NULL;

        char* token = strtok_s(line, "|", &context);
        if (!token) continue;
        char path[MAX_PATH_LENGTH];
        strcpy_s(path, sizeof(path), token);

        token = strtok_s(NULL, "|", &context);
        if (!token) continue;
        long long sz = atoll(token);

        token = strtok_s(NULL, "|", &context);
        if (!token) continue;
        time_t mod = atoll(token);

        token = strtok_s(NULL, "|", &context);
        if (!token) continue;

        if (g_cache_count >= g_cache_capacity) {
            int new_cap = g_cache_capacity * 2;
            CacheEntry* new_arr = (CacheEntry*)realloc(g_cache, new_cap * sizeof(CacheEntry));
            if (!new_arr) break;
            g_cache = new_arr;
            g_cache_capacity = new_cap;
        }

        strcpy_s(g_cache[g_cache_count].path, MAX_PATH_LENGTH, path);
        g_cache[g_cache_count].size = sz;
        g_cache[g_cache_count].modified = mod;
        strcpy_s(g_cache[g_cache_count].hash, HASH_LENGTH, token);
        g_cache_count++;
    }
    fclose(f);

    if (g_cache_count > 1) {
        qsort(g_cache, g_cache_count, sizeof(CacheEntry), compare_cache_entry);
    }
}

static bool get_cached_hash(const char* path, long long size, time_t modified, char* out_hash) {
    if (!g_cache || g_cache_count == 0) return false;

    CacheEntry key;
    strcpy_s(key.path, sizeof(key.path), path);
    CacheEntry* found = (CacheEntry*)bsearch(&key, g_cache, g_cache_count, sizeof(CacheEntry), compare_cache_entry);

    if (found && found->size == size && found->modified == modified) {
        strcpy_s(out_hash, HASH_LENGTH, found->hash);
        return true;
    }
    return false;
}

static void save_cache(FileInfo* files, int count) {
    FILE* f = fopen("dedup_cache.txt", "w");
    if (!f) return;
    for (int i = 0; i < count; i++) {
        fprintf(f, "%s|%lld|%lld|%s\n", files[i].path, files[i].size, (long long)files[i].modified, files[i].hash);
    }
    fclose(f);
}

static void free_cache(void) {
    free(g_cache);
    g_cache = NULL;
    g_cache_count = 0;
    g_cache_capacity = 0;
}
// ============================================================================
// DIRECTORY LIST INITIALIZATION
// ============================================================================
void init_directory_list(DirectoryList* list) {
    if (!list) return;
    memset(list, 0, sizeof(DirectoryList));
    list->include_subdirs = true;
}

bool add_directory(DirectoryList* list, const char* path) {
    if (!list || !path) return false;
    if (list->count >= MAX_DIRECTORIES) return false;
    if (strlen(path) >= MAX_PATH_LENGTH) return false;
    
    strncpy(list->paths[list->count], path, MAX_PATH_LENGTH - 1);
    list->paths[list->count][MAX_PATH_LENGTH - 1] = '\0';
    list->count++;
    
    return true;
}

void init_exclusion_list(ExclusionList* list) {
    if (!list) return;
    memset(list, 0, sizeof(ExclusionList)); 
}


bool add_exclusion(ExclusionList* list, const char* path) {
    if (!list || !path) return false;
    if (list->count >= MAX_EXCLUSIONS) return false;
    if (strlen(path) >= MAX_PATH_LENGTH) return false;
    
    strncpy(list->paths[list->count], path, MAX_PATH_LENGTH - 1);
    list->paths[list->count][MAX_PATH_LENGTH - 1] = '\0';
    list->count++;
    
    return true;
}

bool is_excluded(const ExclusionList* list, const char* path) {
    if (!list || !path) return false;
    
    for (int i = 0; i < list->count; i++) {
        size_t excl_len = strlen(list->paths[i]);
        // Use _strnicmp for Windows compatibility
        if (_strnicmp(path, list->paths[i], excl_len) == 0) {
            // Check if it's an exact match or has a path separator
            if (strlen(path) == excl_len || path[excl_len] == '\\' || path[excl_len] == '/') {
                return true;
            }
        }
    }
    return false;
}

static time_t FileTimeToTimeT(const FILETIME* ft) {
    if (!ft) return 0;
    
    const long long EPOCH_DIFF = 116444736000000000LL;
    
    LARGE_INTEGER li;
    li.LowPart = ft->dwLowDateTime;
    li.HighPart = ft->dwHighDateTime;
    
    return (time_t)((li.QuadPart - EPOCH_DIFF) / 10000000LL);
}

void compute_hash(const char* filename, char* output, ScanMode mode) {
    if (!filename || !output) {
        if (output) strcpy_s(output, HASH_LENGTH, "ERROR_NULL");
        return;
    }

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) {
        strcpy_s(output, HASH_LENGTH, "ERROR_ALGO");
        return;
    }

    DWORD cbHashObject = 0, cbData = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                               (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        strcpy_s(output, HASH_LENGTH, "ERROR_PROP");
        return;
    }

    PBYTE pbHashObject = (PBYTE)malloc(cbHashObject);
    if (!pbHashObject) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        strcpy_s(output, HASH_LENGTH, "ERROR_MEM");
        return;
    }

    status = BCryptCreateHash(hAlg, &hHash, pbHashObject, cbHashObject, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        strcpy_s(output, HASH_LENGTH, "ERROR_HASH");
        return;
    }

    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        BCryptDestroyHash(hHash);
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        strcpy_s(output, HASH_LENGTH, "ERROR_OPEN");
        return;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        BCryptDestroyHash(hHash);
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        strcpy_s(output, HASH_LENGTH, "ERROR_SIZE");
        return;
    }

    bool hash_error = false;
    size_t bytes_to_hash = (mode == SCAN_QUICK) ? QUICK_HASH_SIZE : (size_t)fileSize.QuadPart;
    if (bytes_to_hash > (size_t)fileSize.QuadPart) {
        bytes_to_hash = (size_t)fileSize.QuadPart;
    }

    if (bytes_to_hash > 0) {
        HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (hMap) {
            // Map only the bytes we need to hash
            void* pMap = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, bytes_to_hash);
            if (pMap) {
                status = BCryptHashData(hHash, (PUCHAR)pMap, (ULONG)bytes_to_hash, 0);
                if (!BCRYPT_SUCCESS(status)) {
                    hash_error = true;
                }
                UnmapViewOfFile(pMap);
            } else {
                hash_error = true;
            }
            CloseHandle(hMap);
        } else {
            hash_error = true;
        }
    }

    CloseHandle(hFile);

    if (hash_error) {
        BCryptDestroyHash(hHash);
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        strcpy_s(output, HASH_LENGTH, "ERROR_READ");
        return;
    }

    BYTE hash[32]; /* SHA-256 produces 32 bytes */
    status = BCryptFinishHash(hHash, hash, sizeof(hash), 0);
    BCryptDestroyHash(hHash);
    free(pbHashObject);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        strcpy_s(output, HASH_LENGTH, "ERROR_FIN");
        return;
    }

    /* Convert 32 bytes to 64-character hex string + null terminator */
    for (int i = 0; i < 32; i++) {
        sprintf(output + i * 2, "%02x", hash[i]);
    }
    output[64] = '\0';
}

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
    size_t path_len = strlen(path);
    int len;
    if (path_len > 0 && (path[path_len - 1] == '\\' || path[path_len - 1] == '/')) {
        len = snprintf(search_path, MAX_PATH_LENGTH, "%s*", path);
    } else {
        len = snprintf(search_path, MAX_PATH_LENGTH, "%s\\*", path);
    }
    
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
        
        // Skip reparse points (symlinks, junctions) to avoid infinite loops
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
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
            
            // Store path
            strncpy(files[count].path, full_path, MAX_PATH_LENGTH - 1);
            files[count].path[MAX_PATH_LENGTH - 1] = '\0';
            
            // Get size (combine high and low parts)
            files[count].size = ((long long)ffd.nFileSizeHigh << 32) | 
                                ffd.nFileSizeLow;
            
            // Get modification time
            files[count].modified = FileTimeToTimeT(&ffd.ftLastWriteTime);
            
            // Hashing is now deferred to the parallel processing phase.
            
            count++;
        }
        
    } while (FindNextFileA(hFind, &ffd) && count < max_files);
    
    FindClose(hFind);
    return count;
}


typedef struct {
    FileInfo* file;
    ScanMode mode;
} HashWorkContext;

static volatile LONG g_pending_hashes = 0;

static void CALLBACK HashWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context) {
    (void)Instance;
    HashWorkContext* ctx = (HashWorkContext*)Context;
    
    if (!get_cached_hash(ctx->file->path, ctx->file->size, ctx->file->modified, ctx->file->hash)) {
        compute_hash(ctx->file->path, ctx->file->hash, ctx->mode);
    }
    
    InterlockedDecrement(&g_pending_hashes);
}

int scan_directories(const AdvancedConfig* config, FileInfo* files, int max_files) {
    if (!config || !files || max_files <= 0) return 0;
    
    // Initialize progress
    EnterCriticalSection(&g_dataLock);
    g_progress.files_scanned = 0;
    g_progress.current_percent = 0;
    g_progress.is_complete = false;
    LeaveCriticalSection(&g_dataLock);
    
    int total = 0;
    
    // Scan each directory (DFS phase)
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
    
    if (total > 0) {
        // Parallel Hashing Phase
        load_cache();
        
        HashWorkContext* contexts = malloc(total * sizeof(HashWorkContext));
        if (contexts) {
            g_pending_hashes = total;
            
            for (int i = 0; i < total; i++) {
                contexts[i].file = &files[i];
                contexts[i].mode = config->scan_mode;
                if (!TrySubmitThreadpoolCallback(HashWorkCallback, &contexts[i], NULL)) {
                    // P4: Fallback -- hash synchronously on threadpool failure
                    if (!get_cached_hash(contexts[i].file->path,
                                         contexts[i].file->size,
                                         contexts[i].file->modified,
                                         contexts[i].file->hash)) {
                        compute_hash(contexts[i].file->path,
                                     contexts[i].file->hash,
                                     config->scan_mode);
                    }
                    InterlockedDecrement(&g_pending_hashes);
                }
            }
            
            // Wait for all to complete
            while (InterlockedAdd(&g_pending_hashes, 0) > 0) {
                Sleep(50);
                LONG pending = InterlockedAdd(&g_pending_hashes, 0);
                EnterCriticalSection(&g_dataLock);
                g_progress.files_scanned = total - pending;
                g_progress.current_percent = (total > 0) ? ((total - pending) * 100 / total) : 0;
                LeaveCriticalSection(&g_dataLock);
            }
            
            save_cache(files, total);
            free(contexts);
        } else {
            // Fallback to sequential if malloc fails
            for (int i = 0; i < total; i++) {
                if (!get_cached_hash(files[i].path, files[i].size, files[i].modified, files[i].hash)) {
                    compute_hash(files[i].path, files[i].hash, config->scan_mode);
                }
                EnterCriticalSection(&g_dataLock);
                g_progress.files_scanned = i + 1;
                LeaveCriticalSection(&g_dataLock);
            }
            save_cache(files, total);
        }
        
        free_cache();
    }
    
    // Mark complete
    EnterCriticalSection(&g_dataLock);
    g_progress.is_complete = true;
    g_progress.current_percent = 100;
    g_progress.files_scanned = total;
    LeaveCriticalSection(&g_dataLock);
    
    return total;
}

// ============================================================================
// UTILITY: GET SCAN MODE NAME
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
