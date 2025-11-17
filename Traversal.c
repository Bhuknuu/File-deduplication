#include "common.h"

void init_directory_list(DirectoryList* list) {
    if (!list) return;
    memset(list, 0, sizeof(DirectoryList));
    list->include_subdirs = true;
}

bool add_directory_to_list(DirectoryList* list, const char* path) {
    if (!list || !path || list->count >= MAX_DIRECTORIES) return false;
    strncpy(list->paths[list->count], path, MAX_PATH_LENGTH - 1);
    list->paths[list->count][MAX_PATH_LENGTH - 1] = '\0';
    list->count++;
    return true;
}

static time_t FileTimeToTimeT(const FILETIME* ft) {
    const long long EPOCH_DIFF = 116444736000000000LL;
    LARGE_INTEGER li;
    li.LowPart = ft->dwLowDateTime;
    li.HighPart = ft->dwHighDateTime;
    return (time_t)((li.QuadPart - EPOCH_DIFF) / 10000000LL);
}

void compute_hash(const char* filename, char* output, ScanMode mode) {
    HashAlgorithm algo = HASH_FNV1A;
    switch (mode) {
        case SCAN_QUICK: algo = HASH_FNV1A; break;
        case SCAN_BALANCED: algo = HASH_MD5; break;
        case SCAN_THOROUGH: algo = HASH_SHA256; break;
        default: algo = HASH_FNV1A; break;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        strcpy(output, "ERROR");
        return;
    }
    
    uint64_t hash = FNV_OFFSET_BASIS;
    unsigned char buffer[READ_BUFFER_SIZE];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes; i++) {
            hash ^= buffer[i];
            hash *= FNV_PRIME;
        }
    }
    
    sprintf(output, "%016llx", hash);
    fclose(file);
}

static int scan_directory_internal(const char* path, FileInfo* files, int current_count, 
                                   int max_files, bool recurse, ScanMode mode) {
    WIN32_FIND_DATAA ffd;
    HANDLE hFind;
    char search_path[MAX_PATH_LENGTH];
    int count = current_count;
    
    snprintf(search_path, MAX_PATH_LENGTH, "%s\\*", path);
    hFind = FindFirstFileA(search_path, &ffd);
    
    if (hFind == INVALID_HANDLE_VALUE) return count;
    
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;
        
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, MAX_PATH_LENGTH, "%s\\%s", path, ffd.cFileName);
        
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recurse && count < max_files) {
                count = scan_directory_internal(full_path, files, count, max_files, true, mode);
            }
        } else {
            if (count >= max_files) break;
            
            strncpy(files[count].path, full_path, MAX_PATH_LENGTH - 1);
            files[count].path[MAX_PATH_LENGTH - 1] = '\0';
            files[count].size = ((long long)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow;
            files[count].modified = FileTimeToTimeT(&ffd.ftLastWriteTime);
            compute_hash(full_path, files[count].hash, mode);
            count++;
        }
    } while (FindNextFileA(hFind, &ffd));
    
    FindClose(hFind);
    return count;
}

int scan_directories_recursive(const DirectoryList* list, FileInfo* files, 
                                int max_files, ScanMode mode) {
    if (!list || !files) return 0;
    int total = 0;
    for (int i = 0; i < list->count && total < max_files; i++) {
        total = scan_directory_internal(list->paths[i], files, total, 
                                       max_files, list->include_subdirs, mode);
    }
    return total;
}