#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#define MAX_PATH_LENGTH 4096
#define HASH_LENGTH 65  // SHA-256 max (64 hex + null)
#define MAX_FILES 50000
#define MAX_DIRECTORIES 20
#define MAX_FILTERS 10
#define SCAN_DATA_VERSION 2

// Constants for clarity
#define INITIAL_CAPACITY 4
#define GROWTH_FACTOR 2
#define READ_BUFFER_SIZE 32768
#define HASH_TABLE_SIZE 10007  // Prime number

// FNV-1a constants
#define FNV_PRIME 1099511628211ULL
#define FNV_OFFSET_BASIS 14695981039346656037ULL

// Scan modes - User-friendly instead of confusing hash names
typedef enum {
    SCAN_QUICK,      // FNV-1a - Fast, good for large files (99.99% accurate)
    SCAN_BALANCED,   // MD5 - Medium speed, very reliable
    SCAN_THOROUGH,   // SHA-256 - Slowest, cryptographically secure
    SCAN_MODE_COUNT
} ScanMode;

// Internal hash algorithms (not exposed to user)
typedef enum {
    HASH_FNV1A,
    HASH_MD5,
    HASH_SHA1,
    HASH_SHA256,
    HASH_COUNT
} HashAlgorithm;

// Filter types
typedef enum {
    FILTER_NONE,
    FILTER_SIZE,
    FILTER_NAME,
    FILTER_EXTENSION,
    FILTER_MODIFIED_DATE,
    FILTER_COUNT
} FilterType;

// File metadata
typedef struct {
    char path[MAX_PATH_LENGTH];
    long long size;
    time_t modified;
    char hash[HASH_LENGTH];
} FileInfo;

// Duplicate group
typedef struct {
    FileInfo* files;
    int count;
} DuplicateGroup;

// Results
typedef struct {
    DuplicateGroup* groups;
    int count;
} DuplicateResults;

// Directory list
typedef struct {
    char paths[MAX_DIRECTORIES][MAX_PATH_LENGTH];
    int count;
    bool include_subdirs;
} DirectoryList;

// Filter definition
typedef struct {
    FilterType type;
    bool enabled;
    union {
        struct { long long min_size; long long max_size; } size;
        struct { char pattern[256]; bool case_sensitive; } name;
        struct { char extension[32]; } ext;
        struct { time_t min_date; time_t max_date; } date;
    } criteria;
} Filter;

// Filter configuration
typedef struct {
    Filter filters[MAX_FILTERS];
    int count;
    HashAlgorithm hash_algo;
} FilterConfig;

// Scan data for persistence
typedef struct {
    int version;
    time_t scan_time;
    int file_count;
    DirectoryList dir_list;
    FilterConfig filter_config;
    FileInfo* files;
} ScanData;

// Hash computation function
void compute_hash(const char* filename, char* output, HashAlgorithm algo);

#endif // COMMON_H