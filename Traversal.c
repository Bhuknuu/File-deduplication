#include "traversal.h"
#include <errno.h>

void compute_fnv1a_hash(const char* filename, char* output) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        strcpy(output, "ERROR");
        return;
    }
    
    uint64_t hash = FNV_OFFSET_BASIS;
    
    const int bufSize = 32768;
    unsigned char* buffer = (unsigned char*)malloc(bufSize);
    if (buffer == NULL) {
        perror("Memory allocation failed");
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
    
    // Convert 64-bit hash to 16-character hex string
    // Use I64 prefix for MinGW/Windows compatibility
    #ifdef _WIN32
        snprintf(output, HASH_LENGTH, "%016I64x", (unsigned long long)hash);
    #else
        snprintf(output, HASH_LENGTH, "%016llx", (unsigned long long)hash);
    #endif
    
    fclose(file);
    free(buffer);
}

int scan_directory(const char* path, FileInfo* files, int max_files) {
    DIR* dir;
    struct dirent* entry;
    struct stat file_stat;
    char full_path[MAX_PATH_LENGTH];
    int count = 0;
    
    printf("Scanning directory: %s\n", path);
    fflush(stdout);
   
    dir = opendir(path);
    if (dir == NULL) {
        perror("Error opening directory");
        return 0;
    }
  
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
    
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", path, entry->d_name);
        
        if (stat(full_path, &file_stat) == -1) {
            perror("Error getting file status");
            continue;
        }
        
        if (S_ISREG(file_stat.st_mode)) {
            // Store file metadata
            strncpy(files[count].path, full_path, MAX_PATH_LENGTH - 1);
            files[count].path[MAX_PATH_LENGTH - 1] = '\0';  // Ensure null termination
            files[count].size = file_stat.st_size;
            files[count].modified = file_stat.st_mtime;
            
            compute_fnv1a_hash(full_path, files[count].hash);
            
            count++;
            
            printf("\rScanned: %d files", count);
            fflush(stdout);
        }
     }
    
    closedir(dir);
    printf("\nScan complete.\n");
    fflush(stdout);
    
    return count;
}
