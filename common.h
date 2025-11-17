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
#define HASH_LENGTH 65
#define MAX_FILES 50000
#define MAX_DIRECTORIES 20
#define MAX_FILTERS 10
#define SCAN_DATA_VERSION 2

#define INITIAL_CAPACITY 4
#define GROWTH_FACTOR 2
#define READ_BUFFER_SIZE 32768
#define HASH_TABLE_SIZE 10007

#define FNV_PRIME 1099511628211ULL
#define FNV_OFFSET_BASIS 14695981039346656037ULL

// Custom window messages for thread communication
#define WM_SCAN_COMPLETE (WM_USER + 1)
#define WM_FIND_COMPLETE (WM_USER + 2)

// Scan modes
typedef enum {
    SCAN_QUICK,
    SCAN_BALANCED,
    SCAN_THOROUGH,
    SCAN_MODE_COUNT
} ScanMode;

// Internal hash algorithms
typedef enum {
    HASH_FNV1A,
    HASH_MD5,
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
    ScanMode scan_mode;
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

// Thread synchronization
extern CRITICAL_SECTION g_dataLock;

// Function prototypes
void init_directory_list(DirectoryList* list);
bool add_directory_to_list(DirectoryList* list, const char* path);
int scan_directories_recursive(const DirectoryList* list, FileInfo* files, int max_files, ScanMode mode);
void compute_hash(const char* filename, char* output, ScanMode mode);

void init_filter_config(FilterConfig* config);
bool add_filter(FilterConfig* config, FilterType type);
const char* get_scan_mode_name(ScanMode mode);
const char* get_scan_mode_description(ScanMode mode);
const char* get_filter_type_name(FilterType type);

DuplicateResults find_duplicates(FileInfo* files, int count, const FilterConfig* config);
void free_duplicate_results(DuplicateResults* results);

bool save_scan_data(const char* filename, const ScanData* data);
bool load_scan_data(const char* filename, ScanData* data);
void free_scan_data(ScanData* data);

bool remove_file(const char* filepath);
int remove_duplicates_except_first(DuplicateResults* results);
int remove_duplicates_except_index(DuplicateResults* results, int index);
int move_duplicates_to_folder(DuplicateResults* results, int keep_index, const char* dest_folder);
int create_hard_links(DuplicateResults* results, int keep_index);
int create_symbolic_links(DuplicateResults* results, int keep_index);
bool ensure_directory_exists(const char* path);

#endif