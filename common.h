#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <openssl/sha.h>

#define MAX_PATH_LENGTH 4096
#define HASH_LENGTH 65  // SHA-256 produces 64-character hash + null terminator
#define MAX_FILES 10000  // Maximum number of files to process

// Structure to store file metadata
typedef struct {
    char path[MAX_PATH_LENGTH];  // Full path to the file
    off_t size;                  // File size in bytes
    time_t modified;             // Last modification time
    char hash[HASH_LENGTH];      // SHA-256 hash of file content
} FileInfo;

// Structure to store a group of duplicate files
typedef struct {
    FileInfo* files;             // Array of duplicate files
    int count;                   // Number of files in this group
} DuplicateGroup;

// Structure to store all duplicate groups found
typedef struct {
    DuplicateGroup* groups;      // Array of duplicate groups
    int count;                   // Number of duplicate groups
} DuplicateResults;

// Function blueprints
void print_banner();
void print_menu();
void clear_input_buffer();
void compute_sha256(const char* filename, char* output);
void trim_newline(char* str);
void free_duplicate_results(DuplicateResults* results);

#endif // COMMON_H
