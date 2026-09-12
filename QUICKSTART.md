# Quick Start Guide - QOL Features

## 🚀 Getting Started in 5 Steps

### Step 1: Build the Project
```batch
cd d:\Programs\C\File-deduplication
build.bat
```
This compiles all source files including the new features.c module.

---

### Step 2: Copy and Edit Config File
```batch
copy dedup.cfg.sample dedup.cfg
notepad dedup.cfg
```

Start with these safe defaults:
```ini
dry_run=true
safe_delete=true
log_operations=true
backup_folder=C:\Backups
```

---

### Step 3: Run Your First Scan (Preview Mode)

In your code:
```c
#include "common.h"

int main() {
    // Load config
    AdvancedConfig config;
    load_config_file("dedup.cfg", &config);
    
    // Setup logging
    init_logger(config.log_file);
    
    // Scan directories
    ScanConfig scan_config;
    scan_config.scan_mode = config.scan_mode;
    scan_config.directories = config.directories;
    scan_config.exclusions = config.exclusions;
    
    FileInfo* files = malloc(MAX_FILES * sizeof(FileInfo));
    int file_count = scan_directories(&scan_config, files, MAX_FILES);
    
    // Find duplicates
    DuplicateResults results = find_duplicates(files, file_count);
    
    printf("Found %d duplicate groups\n", results.count);
    printf("Files affected: %d\n", 
           results.count > 0 ? results.groups[0].count : 0);
    
    // Export for review
    export_duplicates_csv(&results, "preview.csv");
    
    // Cleanup
    free(files);
    free_duplicate_results(&results);
    close_logger();
    
    return 0;
}
```

**Check the output:**
- `dedup_operations.log` - What happened
- `preview.csv` - List of duplicates found
- Console output - Quick summary

---

### Step 4: Review Before Execution

Open `preview.csv` in Excel to see:
- Group number
- File paths
- File sizes
- Modification dates

Check `dedup_operations.log` to verify scanning worked correctly.

---

### Step 5: Execute (If Preview Looks Good)

Change config to:
```ini
dry_run=false
```

Re-run your program. Files will be moved to backup folder:
```
C:\Backups\duplicate_file_1.ext
C:\Backups\duplicate_file_2.ext
...
```

Check `dedup_operations.log` to see exactly what was done.

---

## ✅ Common Workflows

### Workflow 1: Find and Preview Duplicates (No Changes)
```c
config.options.dry_run = true;
config.options.log_operations = true;

// Scan and find duplicates
DuplicateResults results = find_duplicates(files, file_count);
export_duplicates_csv(&results, "duplicates.csv");

// Check preview
// STOP HERE - review results manually
```

### Workflow 2: Safe Backup Before Cleanup
```c
config.options.dry_run = false;
config.options.safe_delete = true;
config.backup_folder = "C:\\Backup\\";

// Scan and find
DuplicateResults results = find_duplicates(files, file_count);

// Execute - files moved to backup
remove_duplicates_safe(&results, config.backup_folder, false);

// If something goes wrong, restore from C:\Backup\
```

### Workflow 3: Focus on Specific File Types
```c
ExtensionFilter ext_filter;
init_extension_filter(&ext_filter, true);  // Whitelist mode
add_extension(&ext_filter, ".jpg");
add_extension(&ext_filter, ".png");
add_extension(&ext_filter, ".bmp");

config.ext_filter = ext_filter;

// Now only checks image files
DuplicateResults results = find_duplicates(files, file_count);
```

### Workflow 4: Large Files Only
```c
SizeFilter size_filter;
init_size_filter(&size_filter,
    100 * 1024 * 1024,    // Min: 100 MB
    -1);                  // Max: unlimited

config.size_filter = size_filter;

// Now only processes files >= 100MB
DuplicateResults results = find_duplicates(files, file_count);
```

---

## 📊 Understanding the Logs

### dedup_operations.log Sample Output:
```
[2026-05-21 14:30:45] Program started
[2026-05-21 14:30:46] Scan started for directory: C:\Documents
[2026-05-21 14:30:52] Scanned 2047 files
[2026-05-21 14:30:55] Found 5 duplicate groups
[2026-05-21 14:30:55] DRY RUN: Would move: C:\Docs\file1.doc -> C:\Backup
[2026-05-21 14:30:55] DRY RUN: Would move: C:\Docs\file2.doc -> C:\Backup
```

### CSV Export Sample (preview.csv):
```
Group,File Index,Path,Size (Bytes),Modified Date
1,1,"C:\Documents\photo.jpg",2097152,2026-05-10 15:30:00
1,2,"C:\Pictures\photo.jpg",2097152,2026-05-08 10:15:00
2,1,"C:\Downloads\doc.pdf",512000,2026-05-20 09:00:00
2,2,"C:\Archive\doc.pdf",512000,2026-05-01 14:20:00
```

---

## 🎯 Tips & Tricks

### Tip 1: Test Everything First
```c
// ALWAYS do this first:
config.options.dry_run = true;
// ... scan and check results ...

// THEN do this:
config.options.dry_run = false;
```

### Tip 2: Keep Multiple Backups
```c
// Different backup folder for each run
time_t now = time(NULL);
char backup_path[256];
sprintf(backup_path, "C:\\Backups\\Run_%ld", now);
config.backup_folder = backup_path;
```

### Tip 3: Save Your Settings
```c
// User configured perfect settings?
save_config_file("my_settings.cfg", &config);

// Later, just load them:
load_config_file("my_settings.cfg", &config);
```

### Tip 4: Check Space Savings
```c
long long saved = calculate_savings(&results);
double saved_mb = saved / (1024.0 * 1024.0);
double saved_gb = saved / (1024.0 * 1024.0 * 1024.0);

printf("Space saved: %.2f MB (%.2f GB)\n", saved_mb, saved_gb);
```

---

## 🔒 Safety Checklist

Before deleting anything:

- [ ] Backed up important data externally
- [ ] Run with `dry_run=true` first
- [ ] Review CSV export of duplicates
- [ ] Verify backup folder is writable
- [ ] Check logs for any errors
- [ ] Test on small folder first
- [ ] Only then set `dry_run=false`

---

## 🐛 Troubleshooting

### Problem: "Could not create backup folder"
**Solution:** Check folder permissions
```batch
icacls "C:\Backup" /grant:r "%USERNAME%:F"
```

### Problem: "Log file not being created"
**Solution:** Check path permissions and disk space
```c
init_logger("./dedup.log");  // Use relative path
```

### Problem: "Files not being deleted"
**Solution:** Check if in dry-run mode
```c
if (config.options.dry_run) {
    printf("DRY-RUN MODE: No files were actually changed\n");
}
```

### Problem: "Config file not loading"
**Solution:** Verify file format and encoding
- Use ASCII/UTF-8 encoding
- Use Windows line endings (CRLF)
- Check syntax: `key=value` format

---

## 📈 Performance Recommendations

| Setting | Performance | Safety |
|---------|-------------|--------|
| scan_mode = QUICK | ⚡⚡⚡ Fast | ✅ Good |
| skip_hidden = true | ⚡ Faster | ✅ Better |
| min_size = 1MB | ⚡ Faster | ✅ Better |
| dry_run = true | ⚡⚡⚡ Fast | ✅✅✅ Safest |
| safe_delete = true | ⚡⚡ Slower | ✅✅ Very safe |

---

## 📚 Example Config Files

### Config 1: Conservative (Maximum Safety)
```ini
dry_run=true
safe_delete=true
log_operations=true
scan_mode=0
skip_hidden=true
skip_system=true
min_size=1048576
backup_folder=C:\SafeBackup
```

### Config 2: Aggressive (Performance)
```ini
dry_run=false
safe_delete=false
log_operations=false
scan_mode=0
skip_hidden=false
skip_system=false
min_size=0
```

### Config 3: Balanced (Recommended)
```ini
dry_run=false
safe_delete=true
log_operations=true
scan_mode=0
skip_hidden=true
skip_system=true
min_size=1048576
backup_folder=C:\Backup
```

---

## 🎓 Learning Path

1. **Day 1:** Read this guide + FEATURES.md
2. **Day 2:** Run with dry_run=true on test folder
3. **Day 3:** Review CSV export and logs
4. **Day 4:** Run with safe_delete=true on test folder
5. **Day 5:** Verify backup folder, check restoration works
6. **Day 6:** Use in production on small folder
7. **Day 7:** Scale to larger operations

---

**Happy deduplicating! 🎉**

Questions? Check FEATURES.md for detailed documentation.
