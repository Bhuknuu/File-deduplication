/*
 * COMMON.H - Header file with all shared definitions
 * 
 * DSA PROJECT - File Deduplication System
 * Demonstrates: Hash Tables, Dynamic Arrays, Trees, String Algorithms
 */

#ifndef COMMON_H
#define COMMON_H

// Standard library includes
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>

// Windows API includes
#include <windows.h>// For Windows API functions like CreateDirectoryA, DeleteFileA, etc.
#include <commctrl.h>// For common controls like progress bar 
#include <shlobj.h>// For SHCreateDirectoryExA 

// ============================================================================
// CONSTANTS like maximum lengths and sizes for paths, hashes, arrays
// ============================================================================
#define MAX_PATH_LENGTH 4096
#define HASH_LENGTH 65               /* SHA-256 hex string: 64 chars + null terminator */
#define MAX_FILES 100000
#define MAX_DIRECTORIES 50
#define MAX_EXCLUSIONS 20

// Performance tuning
#define READ_BUFFER_SIZE 65536
#define HASH_TABLE_SIZE 50021        // Prime number for better distribution
#define QUICK_HASH_SIZE (1024 * 1024)
#define THOROUGH_HASH_SIZE 0

// Custom Windows messages
#define WM_SCAN_COMPLETE (WM_USER + 1)
#define WM_FIND_COMPLETE (WM_USER + 2)
#define WM_UPDATE_PROGRESS (WM_USER + 3)

// ============================================================================
// SCAN MODE ENUMERATION
// Selects hashing algorithm
// ============================================================================
typedef enum {
    SCAN_QUICK,      // Fast: Hash first 1MB
    SCAN_THOROUGH,   // Accurate: Hash entire file
    SCAN_MODE_COUNT
} ScanMode;

// ============================================================================
// FILE INFORMATION STRUCTURE
// Stores metadata for each file
// ============================================================================
typedef struct {
    char path[MAX_PATH_LENGTH];
    long long size;
    time_t modified;
    char hash[HASH_LENGTH];
} FileInfo;

// ============================================================================
// DUPLICATE GROUP STRUCTURE
// Groups files with identical content
// ============================================================================
typedef struct {
    FileInfo* files;
    int count;
    int capacity;  // Added for dynamic array management
} DuplicateGroup;

// ============================================================================
// DUPLICATE RESULTS STRUCTURE
// Contains all duplicate groups found
// ============================================================================
typedef struct {
    DuplicateGroup* groups;
    int count;
    int capacity;  // Added for dynamic array management
} DuplicateResults;

// ============================================================================
// DIRECTORY LIST STRUCTURE
// Fixed-size array with counter
// ============================================================================
typedef struct {
    char paths[MAX_DIRECTORIES][MAX_PATH_LENGTH];
    int count;
    bool include_subdirs;
} DirectoryList;

// ============================================================================
// EXCLUSION LIST STRUCTURE
// Folders to skip during scanning
// ============================================================================
typedef struct {
    char paths[MAX_EXCLUSIONS][MAX_PATH_LENGTH];
    int count;
} ExclusionList;

// ============================================================================
// FILE SIZE FILTER STRUCTURE
// For filtering files by size
// ============================================================================
typedef struct {
    long long min_size;  // 0 = no minimum
    long long max_size;  // -1 = no maximum
} SizeFilter;

// ============================================================================
// EXTENSION FILTER STRUCTURE
// For including/excluding file types
// ============================================================================
typedef struct {
    char extensions[MAX_EXCLUSIONS][32];
    int count;
    bool is_whitelist;  // true = include only these, false = exclude these
} ExtensionFilter;

// ============================================================================
// OPERATION OPTIONS STRUCTURE
// Controls how operations are performed
// ============================================================================
typedef struct {
    bool dry_run;           // Preview only, don't modify files
    bool safe_delete;       // Move to backup folder instead of permanent delete
    bool log_operations;    // Write operations to log file
    bool skip_hidden;       // Skip hidden files
    bool skip_system;       // Skip system files
} OperationOptions;

// ============================================================================
// ADVANCED CONFIGURATION STRUCTURE
// Extended settings for filtering and operations
// ============================================================================
typedef struct {
    ScanMode scan_mode;
    DirectoryList directories;
    ExclusionList exclusions;
    SizeFilter size_filter;
    ExtensionFilter ext_filter;
    OperationOptions options;
    char log_file[MAX_PATH_LENGTH];      // Path to log file
    char backup_folder[MAX_PATH_LENGTH]; // Path for safe deleted files
} AdvancedConfig;

// ============================================================================
// SCAN CONFIGURATION STRUCTURE
// Combines all settings for scan operation (DEPRECATED - use AdvancedConfig)
// ============================================================================
typedef struct {
    ScanMode scan_mode;
    DirectoryList directories;
    ExclusionList exclusions;
} ScanConfig;

// ============================================================================
// PROGRESS INFORMATION STRUCTURE
// For reporting progress to UI thread
// ============================================================================
typedef struct {
    int files_scanned;
    int current_percent;
    bool is_complete;
    int files_per_second;           // Scan speed
    int estimated_seconds_remaining; // ETA
} ProgressInfo;

// ============================================================================
// GLOBAL THREAD SYNCHRONIZATION
// ============================================================================
extern CRITICAL_SECTION g_dataLock;
extern ProgressInfo g_progress;

// ============================================================================
// FUNCTION PROTOTYPES - Directory Management
// ============================================================================
void init_directory_list(DirectoryList* list);
bool add_directory(DirectoryList* list, const char* path);
void init_exclusion_list(ExclusionList* list);
bool add_exclusion(ExclusionList* list, const char* path);
bool is_excluded(const ExclusionList* list, const char* path);

// ============================================================================
// FUNCTION PROTOTYPES - File Scanning
// ============================================================================
int scan_directories(const ScanConfig* config, FileInfo* files, int max_files);
void compute_hash(const char* filename, char* output, ScanMode mode);

// ============================================================================
// FUNCTION PROTOTYPES - Duplicate Detection
// ============================================================================
DuplicateResults find_duplicates(FileInfo* files, int count);
void free_duplicate_results(DuplicateResults* results);

// ============================================================================
// FUNCTION PROTOTYPES - File Operations
// ============================================================================
int remove_duplicates_keep_first(DuplicateResults* results);
int move_duplicates(DuplicateResults* results, const char* dest_folder);
int move_all_duplicates_organized(DuplicateResults* results, const char* dest_folder);
int create_hard_links(DuplicateResults* results);

// ============================================================================
// FUNCTION PROTOTYPES - QOL Features: Logging
// ============================================================================
void init_logger(const char* log_file);
void log_operation(const char* format, ...);
void close_logger(void);

// ============================================================================
// FUNCTION PROTOTYPES - QOL Features: Filtering
// ============================================================================
bool should_process_file(const char* filename, const AdvancedConfig* config);
void init_size_filter(SizeFilter* filter, long long min_bytes, long long max_bytes);
void init_extension_filter(ExtensionFilter* filter, bool is_whitelist);
bool add_extension(ExtensionFilter* filter, const char* ext);
bool matches_extension_filter(const char* filename, const ExtensionFilter* filter);

// ============================================================================
// FUNCTION PROTOTYPES - QOL Features: Export
// ============================================================================
bool export_duplicates_csv(const DuplicateResults* results, const char* filename);
bool export_duplicates_txt(const DuplicateResults* results, const char* filename);
long long calculate_savings(const DuplicateResults* results);

// ============================================================================
// FUNCTION PROTOTYPES - QOL Features: Safe Operations
// ============================================================================
int remove_duplicates_safe(DuplicateResults* results, const char* backup_folder, bool dry_run);
int move_duplicates_safe(DuplicateResults* results, const char* dest_folder, const char* backup_folder, bool dry_run);

// ============================================================================
// FUNCTION PROTOTYPES - QOL Features: Configuration
// ============================================================================
bool load_config_file(const char* config_file, AdvancedConfig* config);
bool save_config_file(const char* config_file, const AdvancedConfig* config);
void init_advanced_config(AdvancedConfig* config);

// ============================================================================
// FUNCTION PROTOTYPES - Utility Functions
// ============================================================================
const char* get_scan_mode_name(ScanMode mode);
const char* get_scan_mode_description(ScanMode mode);
bool ensure_directory_exists(const char* path);
void format_file_size(long long bytes, char* output, int output_size);

#endif // COMMON_H