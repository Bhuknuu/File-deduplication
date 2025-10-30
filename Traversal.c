#include "traversal.h"
#include <errno.h>

int scan_directory(const char* path, FileInfo* files, int max_files) {
    DIR* dir;
    struct dirent* entry;
    struct stat file_stat;
    char full_path[MAX_PATH_LENGTH];
    int count = 0;
    
    printf("Scanning directory: %s\n", path);
    
    // Open the directory
    dir = opendir(path);
    if (dir == NULL) {
        perror("Error opening directory");
        return 0;
    }
    
    // Read each entry in the directory
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Construct full path
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", path, entry->d_name);
        
        // Get file information
        if (stat(full_path, &file_stat) == -1) {
            perror("Error getting file status");
            continue;
        }
        
        // Process only regular files
        if (S_ISREG(file_stat.st_mode)) {
            // Store file metadata
            strncpy(files[count].path, full_path, MAX_PATH_LENGTH);
            files[count].size = file_stat.st_size;
            files[count].modified = file_stat.st_mtime;
            
            // Compute file hash
            compute_sha256(full_path, files[count].hash);
            
            count++;
            
            // Print progress
            printf("\rScanned: %d files", count);
            fflush(stdout);
        }
        // Note: Recursive directory scanning could be added here for subdirectories
    }
    
    closedir(dir);
    printf("\nScan complete.\n");
    
    return count;
}

void compute_sha256(const char* filename, char* output) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        strcpy(output, "ERROR");
        return;
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    // Read file in chunks to handle large files
    const int bufSize = 32768;
    unsigned char* buffer = malloc(bufSize);
    int bytesRead;
    
    while ((bytesRead = fread(buffer, 1, bufSize, file))) {
        SHA256_Update(&sha256, buffer, bytesRead);
    }
    
    SHA256_Final(hash, &sha256);
    
    // Convert hash to hexadecimal string
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[HASH_LENGTH - 1] = '\0';
    
    fclose(file);
    free(buffer);
}
