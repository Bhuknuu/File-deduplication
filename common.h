#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <stdint.h>  // For uint64_t type
#include <windows.h> // Native Windows API header
#if defined(_WIN32) || defined(_WIN64)
// Use native Win32 threads on Windows; pthreads header is not available.
#else
#include <pthread.h> // For threading support in the GUI
#endif

#define MAX_PATH_LENGTH 4096
#define HASH_LENGTH 17  // 16 hex chars + null terminator for 64-bit hash
#define MAX_FILES 10000  // Maximum number of files to process

// FNV-1a hash constants (64-bit)
#define FNV_PRIME 1099511628211ULL
#define FNV_OFFSET_BASIS 14695981039346656037ULL

// Structure to store file metadata
typedef struct {
    char path[MAX_PATH_LENGTH];  
    long long size;              // Use long long for Windows file size
    time_t modified;             
    char hash[HASH_LENGTH];      
} FileInfo;

// Structure to store a group of duplicate files
typedef struct {
    FileInfo* files;             
    int count;                  
} DuplicateGroup;

// Structure to store all duplicate groups found
typedef struct {
    DuplicateGroup* groups;     
    int count;                   
} DuplicateResults;

void free_duplicate_results(DuplicateResults* results);

#endif