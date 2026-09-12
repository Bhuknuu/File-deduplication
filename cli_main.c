/*
 * CLI_MAIN.C - Command Line Operational Interface for File Deduplication System
 * 
 * Machine-friendly and agent-ready CLI layer supporting:
 * - Direct directory scanning (recursive or flat)
 * - JSON output for automation / AI agents (--json)
 * - Safe dry-run by default (--dry-run / --apply)
 * - Deletion, safe backup deletion, moving, and NTFS hardlinking
 * - CSV and human-readable text report exports
 * - Standard exit codes: 0 = success, 1 = duplicates found (--check), 2 = error
 */

#include "common.h"

// Required by Traversal.c
CRITICAL_SECTION g_dataLock;
ProgressInfo g_progress = {0};

typedef enum {
    CLI_ACT_SCAN = 0,
    CLI_ACT_DELETE,
    CLI_ACT_SAFE_DELETE,
    CLI_ACT_MOVE,
    CLI_ACT_HARDLINK
} CliAction;

typedef struct {
    AdvancedConfig config;
    CliAction action;
    bool force;            // Execute destructive actions
    bool json_mode;        // Output machine-readable JSON
    bool quiet;            // Minimal output
    bool check_mode;       // Exit 1 if duplicates found
    char move_dir[MAX_PATH_LENGTH];
    char csv_output[MAX_PATH_LENGTH];
    char txt_output[MAX_PATH_LENGTH];
} CliContext;

// ============================================================================
// HELPERS
// ============================================================================

static void print_json_escaped(FILE* fp, const char* str) {
    if (!str) {
        fputs("\"\"", fp);
        return;
    }
    fputc('"', fp);
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '\\': fputs("\\\\", fp); break;
            case '"':  fputs("\\\"", fp); break;
            case '\b': fputs("\\b", fp); break;
            case '\f': fputs("\\f", fp); break;
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(fp, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, fp);
                }
                break;
        }
    }
    fputc('"', fp);
}

static long long parse_size_string(const char* str) {
    if (!str || !*str) return -1;
    char* endptr = NULL;
    double val = strtod(str, &endptr);
    if (endptr == str) return -1;
    while (*endptr == ' ') endptr++;
    if (*endptr == 'k' || *endptr == 'K') val *= 1024.0;
    else if (*endptr == 'm' || *endptr == 'M') val *= 1024.0 * 1024.0;
    else if (*endptr == 'g' || *endptr == 'G') val *= 1024.0 * 1024.0 * 1024.0;
    return (long long)val;
}

static void parse_and_add_extensions(ExtensionFilter* filter, const char* ext_list) {
    char buf[512];
    strncpy_s(buf, sizeof(buf), ext_list, _TRUNCATE);
    char* context = NULL;
    char* token = strtok_s(buf, ",", &context);
    while (token) {
        while (*token == ' ') token++;
        if (*token) {
            add_extension(filter, token);
        }
        token = strtok_s(NULL, ",", &context);
    }
}

static void print_usage(const char* prog) {
    printf("File Deduplication System - CLI Layer\n");
    printf("Usage: %s [OPTIONS] <directory...> [actions]\n\n", prog);
    printf("OPERATIONAL MODES (Default: scan/report only):\n");
    printf("  --scan                  Find and report duplicate files (default)\n");
    printf("  --delete                Delete duplicate files (keeps first/original)\n");
    printf("  --safe-delete           Move duplicates to backup folder\n");
    printf("  --move <folder>         Move duplicates to target folder\n");
    printf("  --hardlink              Replace duplicates with NTFS hard links\n\n");
    printf("SAFETY CONTROLS:\n");
    printf("  --apply, --force        Execute modifications (required for delete/move/hardlink)\n");
    printf("  --dry-run               Preview modifications without touching files (default)\n");
    printf("  --backup-dir <folder>   Directory for backups (used with safe-delete)\n\n");
    printf("SCANNING & FILTERING:\n");
    printf("  -d, --dir <folder>      Add directory to scan (can be repeated)\n");
    printf("  -x, --exclude <folder>  Exclude directory from scan (can be repeated)\n");
    printf("  --no-subdirs            Do not recurse into subdirectories\n");
    printf("  --quick                 Quick scan mode (hashes first 1MB only)\n");
    printf("  --thorough              Thorough scan mode (hashes entire file, default)\n");
    printf("  --min-size <size>       Minimum file size (e.g. 1000, 500K, 10M, 1G)\n");
    printf("  --max-size <size>       Maximum file size (e.g. 100M, 2G)\n");
    printf("  --ext <ext1,ext2>       Extension whitelist (e.g. .jpg,.png or jpg,png)\n");
    printf("  --exclude-ext <exts>    Extension blacklist (e.g. .tmp,.bak)\n");
    printf("  --include-hidden        Scan hidden files (skipped by default)\n");
    printf("  --include-system        Scan system files (skipped by default)\n\n");
    printf("OUTPUT & AUTOMATION:\n");
    printf("  --json                  Output machine-readable JSON to stdout (for AI agents)\n");
    printf("  --csv <file>            Export results to CSV file\n");
    printf("  --txt <file>            Export results to text report file\n");
    printf("  --config <file>         Load settings from ini config file\n");
    printf("  --log <file>            Set log file path\n");
    printf("  --check                 Return exit code 1 if duplicates exist, 0 if clean\n");
    printf("  -q, --quiet             Quiet mode (suppress banners and progress)\n");
    printf("  -h, --help              Show this help message\n");
}

static void output_json_error(const char* message) {
    printf("{\n  \"status\": \"error\",\n  \"message\": ");
    print_json_escaped(stdout, message);
    printf("\n}\n");
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int main(int argc, char* argv[]) {
    CliContext ctx = {0};
    init_advanced_config(&ctx.config);
    InitializeCriticalSection(&g_dataLock);

    // Default safety settings
    ctx.config.options.dry_run = true;
    ctx.config.scan_mode = SCAN_THOROUGH;
    ctx.action = CLI_ACT_SCAN;

    const char* prog_name = argv[0];

    // Argument parsing
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(prog_name);
            DeleteCriticalSection(&g_dataLock);
            return 0;
        } else if (strcmp(arg, "--json") == 0) {
            ctx.json_mode = true;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            ctx.quiet = true;
        } else if (strcmp(arg, "--check") == 0) {
            ctx.check_mode = true;
        } else if (strcmp(arg, "--apply") == 0 || strcmp(arg, "--force") == 0 || strcmp(arg, "-f") == 0) {
            ctx.force = true;
            ctx.config.options.dry_run = false;
        } else if (strcmp(arg, "--dry-run") == 0) {
            ctx.force = false;
            ctx.config.options.dry_run = true;
        } else if (strcmp(arg, "--scan") == 0) {
            ctx.action = CLI_ACT_SCAN;
        } else if (strcmp(arg, "--delete") == 0) {
            ctx.action = CLI_ACT_DELETE;
        } else if (strcmp(arg, "--safe-delete") == 0) {
            ctx.action = CLI_ACT_SAFE_DELETE;
        } else if (strcmp(arg, "--move") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --move");
                else fprintf(stderr, "Error: --move requires destination folder path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            ctx.action = CLI_ACT_MOVE;
            strncpy_s(ctx.move_dir, sizeof(ctx.move_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(arg, "--hardlink") == 0) {
            ctx.action = CLI_ACT_HARDLINK;
        } else if (strcmp(arg, "--backup-dir") == 0 || strcmp(arg, "--backup") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --backup-dir");
                else fprintf(stderr, "Error: --backup-dir requires folder path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            strncpy_s(ctx.config.backup_folder, sizeof(ctx.config.backup_folder), argv[++i], _TRUNCATE);
        } else if (strcmp(arg, "--quick") == 0) {
            ctx.config.scan_mode = SCAN_QUICK;
        } else if (strcmp(arg, "--thorough") == 0) {
            ctx.config.scan_mode = SCAN_THOROUGH;
        } else if (strcmp(arg, "-m") == 0 || strcmp(arg, "--mode") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --mode");
                else fprintf(stderr, "Error: --mode requires 'quick' or 'thorough'\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            i++;
            if (_stricmp(argv[i], "quick") == 0 || strcmp(argv[i], "0") == 0) {
                ctx.config.scan_mode = SCAN_QUICK;
            } else if (_stricmp(argv[i], "thorough") == 0 || strcmp(argv[i], "1") == 0) {
                ctx.config.scan_mode = SCAN_THOROUGH;
            } else {
                if (ctx.json_mode) output_json_error("Invalid scan mode. Use 'quick' or 'thorough'");
                else fprintf(stderr, "Error: Invalid scan mode '%s'\n", argv[i]);
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
        } else if (strcmp(arg, "--no-subdirs") == 0 || strcmp(arg, "--no-recurse") == 0) {
            ctx.config.directories.include_subdirs = false;
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--recursive") == 0) {
            ctx.config.directories.include_subdirs = true;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--dir") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --dir");
                else fprintf(stderr, "Error: --dir requires folder path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            add_directory(&ctx.config.directories, argv[++i]);
        } else if (strcmp(arg, "-x") == 0 || strcmp(arg, "--exclude") == 0 || strcmp(arg, "--exclude-dir") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --exclude");
                else fprintf(stderr, "Error: --exclude requires folder path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            add_exclusion(&ctx.config.exclusions, argv[++i]);
        } else if (strcmp(arg, "--min-size") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --min-size");
                else fprintf(stderr, "Error: --min-size requires size string\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            long long sz = parse_size_string(argv[++i]);
            if (sz >= 0) ctx.config.size_filter.min_size = sz;
        } else if (strcmp(arg, "--max-size") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --max-size");
                else fprintf(stderr, "Error: --max-size requires size string\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            long long sz = parse_size_string(argv[++i]);
            if (sz >= 0) ctx.config.size_filter.max_size = sz;
        } else if (strcmp(arg, "--ext") == 0 || strcmp(arg, "--include-ext") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --ext");
                else fprintf(stderr, "Error: --ext requires comma-separated extensions\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            ctx.config.ext_filter.is_whitelist = true;
            parse_and_add_extensions(&ctx.config.ext_filter, argv[++i]);
        } else if (strcmp(arg, "--exclude-ext") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --exclude-ext");
                else fprintf(stderr, "Error: --exclude-ext requires comma-separated extensions\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            ctx.config.ext_filter.is_whitelist = false;
            parse_and_add_extensions(&ctx.config.ext_filter, argv[++i]);
        } else if (strcmp(arg, "--include-hidden") == 0 || strcmp(arg, "--hidden") == 0) {
            ctx.config.options.skip_hidden = false;
        } else if (strcmp(arg, "--include-system") == 0 || strcmp(arg, "--system") == 0) {
            ctx.config.options.skip_system = false;
        } else if (strcmp(arg, "--config") == 0 || strcmp(arg, "-c") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --config");
                else fprintf(stderr, "Error: --config requires file path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            if (!load_config_file(argv[++i], &ctx.config)) {
                if (ctx.json_mode) output_json_error("Failed to load config file");
                else fprintf(stderr, "Error: Failed to load config file '%s'\n", argv[i]);
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
        } else if (strcmp(arg, "--csv") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --csv");
                else fprintf(stderr, "Error: --csv requires output file path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            strncpy_s(ctx.csv_output, sizeof(ctx.csv_output), argv[++i], _TRUNCATE);
        } else if (strcmp(arg, "--txt") == 0 || strcmp(arg, "--report") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --txt");
                else fprintf(stderr, "Error: --txt requires output file path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            strncpy_s(ctx.txt_output, sizeof(ctx.txt_output), argv[++i], _TRUNCATE);
        } else if (strcmp(arg, "--log") == 0 || strcmp(arg, "-l") == 0) {
            if (i + 1 >= argc) {
                if (ctx.json_mode) output_json_error("Missing argument for --log");
                else fprintf(stderr, "Error: --log requires log file path\n");
                DeleteCriticalSection(&g_dataLock);
                return 2;
            }
            strncpy_s(ctx.config.log_file, sizeof(ctx.config.log_file), argv[++i], _TRUNCATE);
            ctx.config.options.log_operations = true;
        } else if (arg[0] == '-') {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Unknown option: %s", arg);
            if (ctx.json_mode) output_json_error(err_buf);
            else fprintf(stderr, "Error: %s\nRun with --help for options.\n", err_buf);
            DeleteCriticalSection(&g_dataLock);
            return 2;
        } else {
            // Positional argument: directory
            add_directory(&ctx.config.directories, arg);
        }
    }

    // Default directory if none provided
    if (ctx.config.directories.count == 0) {
        add_directory(&ctx.config.directories, ".");
    }

    // Initialize logger if requested
    if (ctx.config.options.log_operations && ctx.config.log_file[0]) {
        init_logger(ctx.config.log_file);
    }

    // Banner in interactive non-json mode
    if (!ctx.json_mode && !ctx.quiet) {
        printf("============================================================================\n");
        printf("File Deduplication System - CLI Layer\n");
        printf("Scan Mode: %s\n", ctx.config.scan_mode == SCAN_QUICK ? "QUICK (first 1MB)" : "THOROUGH (full SHA-256)");
        printf("Directories: %d | Exclusions: %d | Subdirs: %s\n",
               ctx.config.directories.count,
               ctx.config.exclusions.count,
               ctx.config.directories.include_subdirs ? "yes" : "no");
        if (ctx.config.options.dry_run || !ctx.force) {
            printf("Mode: PREVIEW / DRY-RUN (no files will be changed)\n");
        } else {
            printf("Mode: LIVE EXECUTION (modifications will be applied)\n");
        }
        printf("============================================================================\n\n");
    }

    // Allocate scan memory
    FileInfo* files = (FileInfo*)malloc(MAX_FILES * sizeof(FileInfo));
    if (!files) {
        if (ctx.json_mode) output_json_error("Out of memory allocating scan buffer");
        else fprintf(stderr, "Error: Out of memory allocating file scan buffer\n");
        DeleteCriticalSection(&g_dataLock);
        return 2;
    }

    if (!ctx.json_mode && !ctx.quiet) {
        printf("[INFO] Scanning directories...\n");
    }

    int raw_count = scan_directories(&ctx.config, files, MAX_FILES);

    // Apply filtering (size, extensions, hidden, system)
    int filtered_count = 0;
    for (int i = 0; i < raw_count; i++) {
        if (ctx.config.size_filter.min_size > 0 && files[i].size < ctx.config.size_filter.min_size) {
            continue;
        }
        if (ctx.config.size_filter.max_size >= 0 && files[i].size > ctx.config.size_filter.max_size) {
            continue;
        }
        if (!should_process_file(files[i].path, &ctx.config)) {
            continue;
        }
        if (filtered_count != i) {
            files[filtered_count] = files[i];
        }
        filtered_count++;
    }

    if (!ctx.json_mode && !ctx.quiet) {
        printf("[INFO] Found %d files (%d matched filters). Grouping duplicates...\n", raw_count, filtered_count);
    }

    // Duplicate detection
    DuplicateResults results = find_duplicates(files, filtered_count);
    long long wasted_bytes = calculate_savings(&results);

    char wasted_human[64] = "0 B";
    format_file_size(wasted_bytes, wasted_human, sizeof(wasted_human));

    int duplicate_copies_count = 0;
    for (int i = 0; i < results.count; i++) {
        if (results.groups[i].count > 1) {
            duplicate_copies_count += (results.groups[i].count - 1);
        }
    }

    // Determine execution mode
    bool is_dry_run = ctx.config.options.dry_run || !ctx.force;
    int affected_files = 0;
    const char* action_name = "scan";

    switch (ctx.action) {
        case CLI_ACT_DELETE:
            action_name = "delete";
            if (is_dry_run) {
                affected_files = duplicate_copies_count;
            } else {
                if (ctx.config.backup_folder[0] != '\0') {
                    affected_files = remove_duplicates_safe(&results, ctx.config.backup_folder, false);
                } else {
                    affected_files = remove_duplicates_keep_first(&results);
                }
            }
            break;

        case CLI_ACT_SAFE_DELETE:
            action_name = "safe-delete";
            if (is_dry_run) {
                affected_files = duplicate_copies_count;
            } else {
                affected_files = remove_duplicates_safe(&results, ctx.config.backup_folder, false);
            }
            break;

        case CLI_ACT_MOVE:
            action_name = "move";
            if (is_dry_run) {
                affected_files = duplicate_copies_count;
            } else {
                affected_files = move_duplicates_safe(&results, ctx.move_dir, ctx.config.backup_folder, false);
            }
            break;

        case CLI_ACT_HARDLINK:
            action_name = "hardlink";
            if (is_dry_run) {
                affected_files = duplicate_copies_count;
            } else {
                affected_files = create_hard_links(&results);
            }
            break;

        case CLI_ACT_SCAN:
        default:
            action_name = "scan";
            affected_files = 0;
            break;
    }

    // Optional exports
    if (ctx.csv_output[0]) {
        export_duplicates_csv(&results, ctx.csv_output);
        if (!ctx.json_mode && !ctx.quiet) {
            printf("[INFO] Exported CSV to: %s\n", ctx.csv_output);
        }
    }
    if (ctx.txt_output[0]) {
        export_duplicates_txt(&results, ctx.txt_output);
        if (!ctx.json_mode && !ctx.quiet) {
            printf("[INFO] Exported report to: %s\n", ctx.txt_output);
        }
    }

    // Output formatting
    if (ctx.json_mode) {
        printf("{\n");
        printf("  \"status\": \"success\",\n");
        printf("  \"action\": \"%s\",\n", action_name);
        printf("  \"dry_run\": %s,\n", is_dry_run ? "true" : "false");
        printf("  \"scan_mode\": \"%s\",\n", ctx.config.scan_mode == SCAN_QUICK ? "quick" : "thorough");
        printf("  \"total_scanned_files\": %d,\n", raw_count);
        printf("  \"filtered_files\": %d,\n", filtered_count);
        printf("  \"duplicate_groups_count\": %d,\n", results.count);
        printf("  \"duplicate_files_count\": %d,\n", duplicate_copies_count);
        printf("  \"wasted_bytes\": %lld,\n", wasted_bytes);
        printf("  \"wasted_formatted\": \"%s\",\n", wasted_human);
        printf("  \"affected_files_count\": %d,\n", affected_files);

        // Directories list
        printf("  \"directories\": [\n");
        for (int d = 0; d < ctx.config.directories.count; d++) {
            printf("    ");
            print_json_escaped(stdout, ctx.config.directories.paths[d]);
            if (d + 1 < ctx.config.directories.count) printf(",");
            printf("\n");
        }
        printf("  ],\n");

        // Groups list
        printf("  \"groups\": [\n");
        for (int i = 0; i < results.count; i++) {
            DuplicateGroup* g = &results.groups[i];
            char size_human[64] = "0 B";
            if (g->count > 0) {
                format_file_size(g->files[0].size, size_human, sizeof(size_human));
            }
            printf("    {\n");
            printf("      \"group_id\": %d,\n", i + 1);
            printf("      \"hash\": \"%s\",\n", g->count > 0 ? g->files[0].hash : "");
            printf("      \"file_size\": %lld,\n", g->count > 0 ? g->files[0].size : 0LL);
            printf("      \"file_size_formatted\": \"%s\",\n", size_human);
            printf("      \"file_count\": %d,\n", g->count);
            printf("      \"original\": ");
            print_json_escaped(stdout, g->count > 0 ? g->files[0].path : "");
            printf(",\n");
            printf("      \"duplicates\": [\n");
            for (int j = 1; j < g->count; j++) {
                printf("        ");
                print_json_escaped(stdout, g->files[j].path);
                if (j + 1 < g->count) printf(",");
                printf("\n");
            }
            printf("      ]\n");
            printf("    }%s\n", (i + 1 < results.count) ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
    } else {
        if (!ctx.quiet) {
            printf("\n============================================================================\n");
            printf("RESULTS SUMMARY\n");
            printf("============================================================================\n");
            printf("Scanned files    : %d\n", raw_count);
            printf("Matched filters  : %d\n", filtered_count);
            printf("Duplicate groups : %d\n", results.count);
            printf("Duplicate copies : %d\n", duplicate_copies_count);
            printf("Wasted space     : %s (%lld bytes)\n", wasted_human, wasted_bytes);
            printf("Action           : %s\n", action_name);
            printf("Execution mode   : %s\n", is_dry_run ? "DRY-RUN (preview only)" : "LIVE (changes applied)");
            printf("Files affected   : %d\n", affected_files);
            printf("============================================================================\n\n");

            if (results.count > 0) {
                printf("--- Top Duplicate Groups ---\n");
                int show_count = results.count > 20 ? 20 : results.count;
                for (int i = 0; i < show_count; i++) {
                    DuplicateGroup* g = &results.groups[i];
                    char sz_buf[64];
                    format_file_size(g->files[0].size, sz_buf, sizeof(sz_buf));
                    printf("Group #%d (%s each, %d copies, hash: %.12s...):\n",
                           i + 1, sz_buf, g->count, g->files[0].hash);
                    printf("  [Original]  %s\n", g->files[0].path);
                    for (int j = 1; j < g->count; j++) {
                        printf("  [Duplicate] %s\n", g->files[j].path);
                    }
                    printf("\n");
                }
                if (results.count > 20) {
                    printf("... and %d more duplicate groups. (Use --json or --csv for full list)\n\n", results.count - 20);
                }
            }

            if (ctx.action != CLI_ACT_SCAN && is_dry_run) {
                printf("[NOTE] No files were deleted or moved because --apply / --force was not specified.\n");
                printf("       To execute modifications, re-run with '--apply'.\n");
            }
        }
    }

    // Cleanup
    free_duplicate_results(&results);
    free(files);
    DeleteCriticalSection(&g_dataLock);
    close_logger();

    if (ctx.check_mode && results.count > 0) {
        return 1;
    }
    return 0;
}
