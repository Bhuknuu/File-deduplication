/*
 * FEATURES.C - Quality-of-Life Features
 * 
 * IMPLEMENTED FEATURES:
 * 1. Operation Logging - Track what was done
 * 2. File Filtering - Size, extension, attributes
 * 3. Export Functions - CSV and text export
 * 4. Safe Operations - Dry-run and backup options
 * 5. Configuration Management - Save/load settings
 */

#include "common.h"

// ============================================================================
// GLOBAL LOGGER STATE
// ============================================================================
static FILE* g_log_file = NULL;
static CRITICAL_SECTION g_log_lock;
static bool g_logger_initialized = false;

// ============================================================================
// LOGGER IMPLEMENTATION
// ============================================================================

/**
 * Initialize the logging system
 * 
 * @param log_file Path to log file (NULL to skip logging)
 */
void init_logger(const char* log_file) {
    if (!g_logger_initialized) {
        InitializeCriticalSection(&g_log_lock);
        g_logger_initialized = true;
    }
    
    if (log_file) {
        EnterCriticalSection(&g_log_lock);
        if (g_log_file) fclose(g_log_file);
        g_log_file = fopen(log_file, "a");  // Append mode
        LeaveCriticalSection(&g_log_lock);
    }
}

/**
 * Log an operation with timestamp
 * Supports printf-style formatting
 */
void log_operation(const char* format, ...) {
    if (!g_log_file || !format) return;
    
    EnterCriticalSection(&g_log_lock);
    
    time_t now = time(NULL);
    struct tm timeinfo_val;
    struct tm* timeinfo = &timeinfo_val;
    localtime_s(timeinfo, &now);
    
    fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec);
    
    va_list args;
    va_start(args, format);
    vfprintf(g_log_file, format, args);
    va_end(args);
    
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    LeaveCriticalSection(&g_log_lock);
}

/**
 * Close the logging system
 */
void close_logger(void) {
    if (g_log_file) {
        EnterCriticalSection(&g_log_lock);
        fclose(g_log_file);
        g_log_file = NULL;
        LeaveCriticalSection(&g_log_lock);
    }
}

// ============================================================================
// FILTERING IMPLEMENTATION
// ============================================================================

/**
 * Initialize size filter with min and max values
 */
void init_size_filter(SizeFilter* filter, long long min_bytes, long long max_bytes) {
    if (filter) {
        filter->min_size = min_bytes;
        filter->max_size = max_bytes;
    }
}

/**
 * Initialize extension filter
 */
void init_extension_filter(ExtensionFilter* filter, bool is_whitelist) {
    if (filter) {
        filter->count = 0;
        filter->is_whitelist = is_whitelist;
        memset(filter->extensions, 0, sizeof(filter->extensions));
    }
}

/**
 * Add an extension to the filter
 */
bool add_extension(ExtensionFilter* filter, const char* ext) {
    if (!filter || !ext || filter->count >= MAX_EXCLUSIONS) return false;
    
    // Normalize extension (add leading dot if missing)
    if (ext[0] == '.') {
        strncpy(filter->extensions[filter->count], ext, 31);
    } else {
        filter->extensions[filter->count][0] = '.';
        strncpy(filter->extensions[filter->count] + 1, ext, 30);
    }
    filter->extensions[filter->count][31] = '\0';
    filter->count++;
    return true;
}

/**
 * Get file extension from path
 */
static const char* get_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (dot && dot != filename) {
        return dot;
    }
    return "";
}

/**
 * Check if file matches extension filter
 */
bool matches_extension_filter(const char* filename, const ExtensionFilter* filter) {
    if (!filter || filter->count == 0) return true;  // No filter = accept all
    
    const char* ext = get_extension(filename);
    
    for (int i = 0; i < filter->count; i++) {
        if (stricmp(ext, filter->extensions[i]) == 0) {
            // Found matching extension
            return filter->is_whitelist;  // Accept if whitelist, reject if blacklist
        }
    }
    
    // No match found
    return !filter->is_whitelist;  // Reject if whitelist, accept if blacklist
}

/**
 * Check if file is hidden (Windows attribute)
 */
static bool is_file_hidden(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_HIDDEN);
}

/**
 * Check if file is system file (Windows attribute)
 */
static bool is_file_system(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_SYSTEM);
}

/**
 * Determine if file should be processed based on filters
 */
bool should_process_file(const char* filename, const AdvancedConfig* config) {
    if (!filename || !config) return true;
    
    // Check extension filter
    if (!matches_extension_filter(filename, &config->ext_filter)) {
        return false;
    }
    
    // Check hidden file option
    if (config->options.skip_hidden && is_file_hidden(filename)) {
        return false;
    }
    
    // Check system file option
    if (config->options.skip_system && is_file_system(filename)) {
        return false;
    }
    
    return true;
}

// ============================================================================
// EXPORT IMPLEMENTATION
// ============================================================================

static void write_csv_field(FILE* fp, const char* value) {
    fputc('"', fp);
    for (const char* p = value; *p; p++) {
        if (*p == '"') fputc('"', fp); /* RFC 4180 escaping */
        fputc(*p, fp);
    }
    fputc('"', fp);
}

/**
 * Export duplicates to CSV format
 */
bool export_duplicates_csv(const DuplicateResults* results, const char* filename) {
    if (!results || !filename) return false;
    
    FILE* fp = fopen(filename, "w");
    if (!fp) return false;
    
    // Write CSV header
    fprintf(fp, "Group,File Index,Path,Size (Bytes),Modified Date\n");
    
    // Write data
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        for (int j = 0; j < group->count; j++) {
            FileInfo* file = &group->files[j];
            char time_buf[32];
            
            struct tm tm_csv;
            localtime_s(&tm_csv, &file->modified);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                    &tm_csv);
            
            fprintf(fp, "%d,%d,", i + 1, j + 1);
            write_csv_field(fp, file->path);
            fprintf(fp, ",%lld,%s\n", file->size, time_buf);
        }
    }
    
    fclose(fp);
    return true;
}

/**
 * Export duplicates to human-readable text format
 */
bool export_duplicates_txt(const DuplicateResults* results, const char* filename) {
    if (!results || !filename) return false;
    
    FILE* fp = fopen(filename, "w");
    if (!fp) return false;
    
    time_t gen_time = time(NULL);
    struct tm gen_tm;
    localtime_s(&gen_tm, &gen_time);
    char gen_buf[32];
    strftime(gen_buf, sizeof(gen_buf), "%Y-%m-%d %H:%M:%S", &gen_tm);
    
    fprintf(fp, "FILE DEDUPLICATION REPORT\n");
    fprintf(fp, "=======================\n");
    fprintf(fp, "Generated: %s\n", gen_buf);
    fprintf(fp, "Total Duplicate Groups: %d\n\n", results->count);
    
    long long total_wasted = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        fprintf(fp, "GROUP %d - %d duplicates found\n", i + 1, group->count);
        fprintf(fp, "-----------------------------------\n");
        
        for (int j = 0; j < group->count; j++) {
            FileInfo* file = &group->files[j];
            char size_str[32];
            char time_buf[32];
            
            format_file_size(file->size, size_str, sizeof(size_str));
            struct tm tm_txt;
            localtime_s(&tm_txt, &file->modified);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                    &tm_txt);
            
            fprintf(fp, "  [%d] %s (%s)\n", j + 1, file->path, size_str);
            fprintf(fp, "      Modified: %s\n", time_buf);
            fprintf(fp, "      Hash: %s\n", file->hash);
        }
        
        // Calculate wasted space (all but first file)
        if (group->count > 1) {
            total_wasted += group->files[0].size * (group->count - 1);
        }
        fprintf(fp, "\n");
    }
    
    fprintf(fp, "\nSUMMARY\n");
    fprintf(fp, "=======\n");
    char wasted_str[32];
    format_file_size(total_wasted, wasted_str, sizeof(wasted_str));
    fprintf(fp, "Total wasted space: %s\n", wasted_str);
    
    fclose(fp);
    return true;
}

/**
 * Calculate total space wasted by duplicates
 */
long long calculate_savings(const DuplicateResults* results) {
    if (!results) return 0;
    
    long long savings = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Wasted space = file size * (number of duplicates - 1)
        if (group->count > 1 && group->files) {
            savings += group->files[0].size * (group->count - 1);
        }
    }
    
    return savings;
}

// ============================================================================
// SAFE OPERATIONS IMPLEMENTATION
// ============================================================================

/**
 * Create backup folder with timestamp
 */
static __attribute__((unused)) bool create_backup_folder(char* backup_path, size_t size) {
    time_t now = time(NULL);
    struct tm timeinfo_val;
    struct tm* timeinfo = &timeinfo_val;
    localtime_s(timeinfo, &now);
    const char* temp_dir = getenv("TEMP");
    if (!temp_dir || temp_dir[0] == '\0') {
        temp_dir = getenv("TMP");
    }
    if (!temp_dir || temp_dir[0] == '\0') {
        temp_dir = "C:\\Temp";
    }
    snprintf(backup_path, size,
            "%s\\Dedup_Backup_%04d%02d%02d_%02d%02d%02d",
            temp_dir,
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec);
    
    return ensure_directory_exists(backup_path);
}

/**
 * Safe delete with backup option
 * Instead of deleting, moves files to backup folder
 */
int remove_duplicates_safe(DuplicateResults* results, const char* backup_folder, bool dry_run) {
    if (!results || results->count == 0) return 0;
    
    int removed = 0;
    char backup_path[MAX_PATH_LENGTH];
    
    if (!dry_run) {
        if (!ensure_directory_exists(backup_folder)) {
            log_operation("ERROR: Could not create backup folder: %s", backup_folder);
            return 0;
        }
    }
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Keep first file, move rest to backup
        for (int j = 1; j < group->count; j++) {
            const char* src_path = group->files[j].path;
            
            if (dry_run) {
                log_operation("DRY RUN: Would move: %s -> %s", src_path, backup_folder);
                removed++;
            } else {
                // Generate unique filename in backup folder
                const char* filename = strrchr(src_path, '\\');
                filename = filename ? filename + 1 : src_path;
                
                snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s", backup_folder, filename);
                
                if (GetFileAttributesA(backup_path) != INVALID_FILE_ATTRIBUTES) {
                    char base_name[MAX_PATH_LENGTH];
                    const char* ext = strrchr(filename, '.');
                    if (ext) {
                        size_t base_len = ext - filename;
                        if (base_len >= MAX_PATH_LENGTH) base_len = MAX_PATH_LENGTH - 1;
                        strncpy(base_name, filename, base_len);
                        base_name[base_len] = '\0';
                        for (int n = 1; n < 10000; n++) {
                            snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s_%d%s", backup_folder, base_name, n, ext);
                            if (GetFileAttributesA(backup_path) == INVALID_FILE_ATTRIBUTES) break;
                        }
                    } else {
                        for (int n = 1; n < 10000; n++) {
                            snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s_%d", backup_folder, filename, n);
                            if (GetFileAttributesA(backup_path) == INVALID_FILE_ATTRIBUTES) break;
                        }
                    }
                }
                
                if (MoveFileA(src_path, backup_path)) {
                    log_operation("MOVED: %s -> %s", src_path, backup_path);
                    removed++;
                } else {
                    log_operation("ERROR: Failed to move: %s", src_path);
                }
            }
        }
    }
    
    return removed;
}

/**
 * Safe move with backup option
 */
int move_duplicates_safe(DuplicateResults* results, const char* dest_folder, 
                         const char* backup_folder, bool dry_run) {
    if (!results || results->count == 0) return 0;
    
    int moved = 0;
    char dest_path[MAX_PATH_LENGTH];
    
    if (!dry_run) {
        if (!ensure_directory_exists(dest_folder)) {
            log_operation("ERROR: Could not create destination folder: %s", dest_folder);
            return 0;
        }
        if (backup_folder && !ensure_directory_exists(backup_folder)) {
            log_operation("ERROR: Could not create backup folder: %s", backup_folder);
        }
    }
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Move all but first file
        for (int j = 1; j < group->count; j++) {
            const char* src_path = group->files[j].path;
            const char* filename = strrchr(src_path, '\\');
            filename = filename ? filename + 1 : src_path;
            
            snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s", dest_folder, filename);
            
            // P5: Collision resolution for same-name files
            if (GetFileAttributesA(dest_path) != INVALID_FILE_ATTRIBUTES) {
                char base_name[MAX_PATH_LENGTH];
                const char* ext = strrchr(filename, '.');
                if (ext) {
                    size_t base_len = ext - filename;
                    if (base_len >= MAX_PATH_LENGTH) base_len = MAX_PATH_LENGTH - 1;
                    strncpy(base_name, filename, base_len);
                    base_name[base_len] = '\0';
                    for (int n = 1; n < 10000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s_%d%s", dest_folder, base_name, n, ext);
                        if (GetFileAttributesA(dest_path) == INVALID_FILE_ATTRIBUTES) break;
                    }
                } else {
                    for (int n = 1; n < 10000; n++) {
                        snprintf(dest_path, MAX_PATH_LENGTH, "%s\\%s_%d", dest_folder, filename, n);
                        if (GetFileAttributesA(dest_path) == INVALID_FILE_ATTRIBUTES) break;
                    }
                }
            }
            
            if (dry_run) {
                log_operation("DRY RUN: Would move: %s -> %s", src_path, dest_path);
                moved++;
            } else {
                if (MoveFileA(src_path, dest_path)) {
                    log_operation("MOVED: %s -> %s", src_path, dest_path);
                    moved++;
                } else {
                    log_operation("ERROR: Failed to move: %s", src_path);
                }
            }
        }
    }
    
    return moved;
}

// ============================================================================
// CONFIGURATION FILE IMPLEMENTATION
// ============================================================================

static void rtrim(char* s) {
    if (!s || !*s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

/**
 * Initialize configuration with defaults
 */
void init_advanced_config(AdvancedConfig* config) {
    if (!config) return;
    
    memset(config, 0, sizeof(AdvancedConfig));
    
    config->scan_mode = SCAN_QUICK;
    config->options.dry_run = false;
    config->options.safe_delete = true;
    config->options.log_operations = true;
    config->options.skip_hidden = true;
    config->options.skip_system = true;
    
    config->size_filter.min_size = 0;
    config->size_filter.max_size = -1;
    
    config->ext_filter.count = 0;
    config->ext_filter.is_whitelist = false;
    
    strcpy_s(config->log_file, sizeof(config->log_file), "dedup.log");
    strcpy_s(config->backup_folder, sizeof(config->backup_folder), "");
}

/**
 * Load configuration from INI-like text file
 */
bool load_config_file(const char* config_file, AdvancedConfig* config) {
    if (!config_file || !config) return false;
    
    FILE* fp = fopen(config_file, "r");
    if (!fp) return false;
    
    init_advanced_config(config);
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // Remove comments
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        // Trim whitespace
        char* p = line;
        while (*p && isspace(*p)) p++;
        if (!*p) continue;
        
        // Parse key=value
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        
        // Trim
        while (*key && isspace(*key)) key++;
        rtrim(key);
        while (*value && isspace(*value)) value++;
        rtrim(value);
        
        // Parse settings
        if (strcmp(key, "scan_mode") == 0) {
            config->scan_mode = atoi(value);
        } else if (strcmp(key, "dry_run") == 0) {
            config->options.dry_run = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "safe_delete") == 0) {
            config->options.safe_delete = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "log_operations") == 0) {
            config->options.log_operations = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "skip_hidden") == 0) {
            config->options.skip_hidden = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "skip_system") == 0) {
            config->options.skip_system = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "min_size") == 0) {
            config->size_filter.min_size = _atoi64(value);
        } else if (strcmp(key, "max_size") == 0) {
            config->size_filter.max_size = _atoi64(value);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy_s(config->log_file, sizeof(config->log_file), value, 
                     sizeof(config->log_file) - 1);
        } else if (strcmp(key, "backup_folder") == 0) {
            strncpy_s(config->backup_folder, sizeof(config->backup_folder), value, 
                     sizeof(config->backup_folder) - 1);
        }
    }
    
    fclose(fp);
    return true;
}

/**
 * Save configuration to INI-like text file
 */
bool save_config_file(const char* config_file, const AdvancedConfig* config) {
    if (!config_file || !config) return false;
    
    FILE* fp = fopen(config_file, "w");
    if (!fp) return false;
    
    fprintf(fp, "# File Deduplication System Configuration\n");
    fprintf(fp, "# Generated automatically\n\n");
    
    fprintf(fp, "[Scan Settings]\n");
    fprintf(fp, "scan_mode=%d\n", config->scan_mode);
    fprintf(fp, "skip_hidden=%s\n", config->options.skip_hidden ? "true" : "false");
    fprintf(fp, "skip_system=%s\n", config->options.skip_system ? "true" : "false");
    
    fprintf(fp, "\n[Filtering]\n");
    fprintf(fp, "min_size=%lld\n", config->size_filter.min_size);
    fprintf(fp, "max_size=%lld\n", config->size_filter.max_size);
    
    fprintf(fp, "\n[Operations]\n");
    fprintf(fp, "dry_run=%s\n", config->options.dry_run ? "true" : "false");
    fprintf(fp, "safe_delete=%s\n", config->options.safe_delete ? "true" : "false");
    fprintf(fp, "log_operations=%s\n", config->options.log_operations ? "true" : "false");
    
    fprintf(fp, "\n[Paths]\n");
    fprintf(fp, "log_file=%s\n", config->log_file);
    fprintf(fp, "backup_folder=%s\n", config->backup_folder);
    
    fclose(fp);
    return true;
}
