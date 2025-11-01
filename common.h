#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>  // For uint64_t type

// Platform-specific includes
#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
    // MinGW compatibility
    #ifndef S_ISREG
        #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
    #endif
    #ifndef S_ISDIR
        #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
    #endif
#else
    #include <unistd.h>
#endif

// Cross-platform dirent.h
#ifdef _WIN32
    #include <dirent.h>  // MinGW provides this
#else
    #include <dirent.h>
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
    off_t size;                  
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

void print_banner();
void print_menu();
void clear_input_buffer();
void compute_fnv1a_hash(const char* filename, char* output);
void trim_newline(char* str);
void free_duplicate_results(DuplicateResults* results);

#endif 
