#include "traversal.h"
#include <errno.h>

// Helper function to convert Windows FILETIME to standard time_t
static time_t FileTimeToTimeT(const FILETIME* ft) {
    // FILETIME is 100-nanosecond intervals since January 1, 1601 (UTC)
    // time_t is seconds since January 1, 1970 (UTC)
    // Calculate the difference between the two epochs in 100-nanosecond intervals
    const long long EPOCH_DIFF = 116444736000000000LL;
    LARGE_INTEGER li;
    li.LowPart = ft->dwLowDateTime;
    li.HighPart = ft->dwHighDateTime;
    return (time_t)((li.QuadPart - EPOCH_DIFF) / 10000000LL);
}

void compute_fnv1a_hash(const char* filename, char* output) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        strcpy(output, "ERROR");
        return;
    }
    
    uint64_t hash = FNV_OFFSET_BASIS;
    
    const int bufSize = 32768;
    unsigned char* buffer = (unsigned char*)malloc(bufSize);
    if (buffer == NULL) {
        fclose(file);
        strcpy(output, "ERROR");
        return;
    }
    
    int bytesRead;
    
    while ((bytesRead = fread(buffer, 1, bufSize, file)) > 0) {
        for (int i = 0; i < bytesRead; i++) {
            hash ^= buffer[i];        
            hash *= FNV_PRIME;
        }
    }
    
    snprintf(output, HASH_LENGTH, "%016llx", (unsigned long long)hash);
    
    fclose(file);
    free(buffer);
}

int scan_directory(const char* path, FileInfo* files, int max_files) {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    char search_path[MAX_PATH_LENGTH];
    int count = 0;
   
    // Create a search path like "C:\path\*"
    snprintf(search_path, MAX_PATH_LENGTH, "%s\\*", path);
    
    hFind = FindFirstFile(search_path, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }
  
    do {
        // Skip "." and ".." entries
        if (strcmp(findFileData.cFileName, ".") == 0 || strcmp(findFileData.cFileName, "..") == 0) {
            continue;
        }

        // Skip directories
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        if (count >= max_files) {
            break; // Reached max file limit
        }

        // Store file metadata
        snprintf(files[count].path, MAX_PATH_LENGTH, "%s\\%s", path, findFileData.cFileName);
        
        // Get file size (nFileSizeHigh and nFileSizeLow must be combined)
        files[count].size = ((long long)findFileData.nFileSizeHigh << 32) + findFileData.nFileSizeLow;
        
        // Get last modified time
        files[count].modified = FileTimeToTimeT(&findFileData.ftLastWriteTime);
        
        compute_fnv1a_hash(files[count].path, files[count].hash);
        
        count++;

    } while (FindNextFile(hFind, &findFileData) != 0);
    
    FindClose(hFind);
    
    return count;
}
