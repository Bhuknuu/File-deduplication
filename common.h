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
#include <stdint.h>  // Add this for uint64_t type

#define MAX_PATH_LENGTH 4096
#define HASH_LENGTH 17  // Reduced from 65 to 17 for 64-bit hash (16 hex chars + null terminator)
#define MAX_FILES 10000  // Maximum number of files to process

// FNV-1a hash constants
#define FNV_PRIME 1099511628211UL
#define FNV_OFFSET_BASIS 14695981039346656037UL

// Structure to store file metadata
typedef struct {
    char path[MAX_PATH_LENGTH];  // Full path to the file
    off_t size;                  // File size in bytes
    time_t modified;             // Last modification time
    char hash[HASH_LENGTH];      // Hash of file content (now using built-in hash)
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
void compute_fnv1a_hash(const char* filename, char* output);  // Replaced SHA-256 with FNV-1a
void trim_newline(char* str);
void free_duplicate_results(DuplicateResults* results);

#endif // COMMON_H