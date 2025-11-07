#include "action.h"
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>  // Added for link and symlink functions

#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
    #define mkdir(path, mode) _mkdir(path)
    // Windows doesn't have symlink/link functions in the same way
    #define link(source, dest) 0  // Placeholder for Windows
    #define symlink(source, dest) 0  // Placeholder for Windows
#else
    #include <unistd.h>  // Already included, but ensuring it's here for POSIX systems
#endif

bool remove_file(const char* filepath) {
    if (remove(filepath) == 0) {
        printf("Successfully removed: %s\n", filepath);
        return true;
    } else {
        perror("Error removing file");
        return false;
    }
}

int remove_duplicates_except_first(DuplicateResults* results) {
    if (results == NULL || results->count == 0) {
        return 0;
    }
    
    int removed_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Skip the first file (index 0) and remove the rest
        for (int j = 1; j < group->count; j++) {
            if (remove_file(group->files[j].path)) {
                removed_count++;
            }
        }
    }
    
    return removed_count;
}

int remove_duplicates_except_index(DuplicateResults* results, int index) {
    if (results == NULL || results->count == 0) {
        return 0;
    }
    
    int removed_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        // Skip the file at the specified index and remove the rest
        for (int j = 0; j < group->count; j++) {
            if (j != index) {
                if (remove_file(group->files[j].path)) {
                    removed_count++;
                }
            }
        }
    }
    
    return removed_count;
}

int interactive_removal(DuplicateResults* results) {
    if (results == NULL || results->count == 0) {
        return 0;
    }
    
    int removed_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        printf("\nDuplicate Group %d:\n", i + 1);
        for (int j = 0; j < group->count; j++) {
            printf("[%d] %s\n", j + 1, group->files[j].path);
        }
        
        printf("Select files to keep (comma-separated indices, e.g., 1,3): ");
        char input[256];
        fgets(input, sizeof(input), stdin);
        
        // Parse the input to get indices to keep
        int keep_indices[group->count];
        int keep_count = 0;
        
        char* token = strtok(input, ",");
        while (token != NULL && keep_count < group->count) {
            int index = atoi(token) - 1; // Convert to 0-based index
            if (index >= 0 && index < group->count) {
                keep_indices[keep_count++] = index;
            }
            token = strtok(NULL, ",");
        }
        
        // Remove files not in the keep list
        for (int j = 0; j < group->count; j++) {
            bool should_keep = false;
            for (int k = 0; k < keep_count; k++) {
                if (j == keep_indices[k]) {
                    should_keep = true;
                    break;
                }
            }
            
            if (!should_keep) {
                if (remove_file(group->files[j].path)) {
                    removed_count++;
                }
            }
        }
    }
    
    return removed_count;
}

// NEW: Functions for re-referencing files
int create_hard_links(DuplicateResults* results, int keep_index) {
    if (results == NULL || results->count == 0) {
        return 0;
    }
    
    int link_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        if (keep_index < 0 || keep_index >= group->count) {
            printf("[!] Invalid keep index for group %d\n", i + 1);
            continue;
        }
        
        const char* source_path = group->files[keep_index].path;
        
        // Create hard links for all other files in the group
        for (int j = 0; j < group->count; j++) {
            if (j == keep_index) continue; // Skip the file we're keeping
            
            const char* target_path = group->files[j].path;
            
            // Remove the original file first
            if (remove(target_path) != 0) {
                perror("Error removing file before creating hard link");
                continue;
            }
            
            // Create the hard link
            if (link(source_path, target_path) == 0) {
                printf("Created hard link: %s -> %s\n", target_path, source_path);
                link_count++;
            } else {
                perror("Error creating hard link");
                // Try to restore the original file if possible
                printf("[!] Warning: Could not create hard link for %s\n", target_path);
            }
        }
    }
    
    return link_count;
}

int create_symbolic_links(DuplicateResults* results, int keep_index) {
    if (results == NULL || results->count == 0) {
        return 0;
    }
    
    int link_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        if (keep_index < 0 || keep_index >= group->count) {
            printf("[!] Invalid keep index for group %d\n", i + 1);
            continue;
        }
        
        const char* source_path = group->files[keep_index].path;
        
        // Create symbolic links for all other files in the group
        for (int j = 0; j < group->count; j++) {
            if (j == keep_index) continue; // Skip the file we're keeping
            
            const char* target_path = group->files[j].path;
            
            // Remove the original file first
            if (remove(target_path) != 0) {
                perror("Error removing file before creating symbolic link");
                continue;
            }
            
            // Create the symbolic link
            if (symlink(source_path, target_path) == 0) {
                printf("Created symbolic link: %s -> %s\n", target_path, source_path);
                link_count++;
            } else {
                perror("Error creating symbolic link");
                // Try to restore the original file if possible
                printf("[!] Warning: Could not create symbolic link for %s\n", target_path);
            }
        }
    }
    
    return link_count;
}

// NEW: Functions for moving files
int move_duplicates_to_folder(DuplicateResults* results, int keep_index, const char* dest_folder) {
    if (results == NULL || results->count == 0 || dest_folder == NULL) {
        return 0;
    }
    
    if (!ensure_directory_exists(dest_folder)) {
        return 0;
    }
    
    int moved_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        if (keep_index < 0 || keep_index >= group->count) {
            printf("[!] Invalid keep index for group %d\n", i + 1);
            continue;
        }
        
        // Move all files except the one at keep_index
        for (int j = 0; j < group->count; j++) {
            if (j == keep_index) continue; // Skip the file we're keeping
            
            const char* source_path = group->files[j].path;
            
            // Extract filename from path
            const char* filename = strrchr(source_path, '/');
            if (filename == NULL) {
                filename = strrchr(source_path, '\\');
            }
            if (filename == NULL) {
                filename = source_path;
            } else {
                filename++; // Skip the slash
            }
            
            // Create destination path
            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, MAX_PATH_LENGTH, "%s/%s", dest_folder, filename);
            
            // Check if destination file already exists
            if (access(dest_path, F_OK) == 0) {
                // Append a number to make the filename unique
                char base_name[MAX_PATH_LENGTH];
                char ext[MAX_PATH_LENGTH] = "";
                strncpy(base_name, dest_path, MAX_PATH_LENGTH);
                
                char* dot = strrchr(base_name, '.');
                if (dot != NULL) {
                    strcpy(ext, dot);
                    *dot = '\0';
                }
                
                int counter = 1;
                do {
                    snprintf(dest_path, MAX_PATH_LENGTH, "%s_%d%s", base_name, counter, ext);
                    counter++;
                } while (access(dest_path, F_OK) == 0);
            }
            
            // Move the file
            if (rename(source_path, dest_path) == 0) {
                printf("Moved: %s -> %s\n", source_path, dest_path);
                moved_count++;
            } else {
                perror("Error moving file");
            }
        }
    }
    
    return moved_count;
}

int organize_duplicates_by_type(DuplicateResults* results, int keep_index, const char* base_folder) {
    if (results == NULL || results->count == 0 || base_folder == NULL) {
        return 0;
    }
    
    if (!ensure_directory_exists(base_folder)) {
        return 0;
    }
    
    int moved_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        if (keep_index < 0 || keep_index >= group->count) {
            printf("[!] Invalid keep index for group %d\n", i + 1);
            continue;
        }
        
        // Move all files except the one at keep_index
        for (int j = 0; j < group->count; j++) {
            if (j == keep_index) continue; // Skip the file we're keeping
            
            const char* source_path = group->files[j].path;
            
            // Extract file extension
            const char* ext = strrchr(source_path, '.');
            if (ext == NULL) {
                ext = "no_extension";
            } else {
                ext++; // Skip the dot
            }
            
            // Create folder for this file type
            char type_folder[MAX_PATH_LENGTH];
            snprintf(type_folder, MAX_PATH_LENGTH, "%s/%s", base_folder, ext);
            
            if (!ensure_directory_exists(type_folder)) {
                continue;
            }
            
            // Extract filename from path
            const char* filename = strrchr(source_path, '/');
            if (filename == NULL) {
                filename = strrchr(source_path, '\\');
            }
            if (filename == NULL) {
                filename = source_path;
            } else {
                filename++; // Skip the slash
            }
            
            // Create destination path
            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, MAX_PATH_LENGTH, "%s/%s", type_folder, filename);
            
            // Check if destination file already exists
            if (access(dest_path, F_OK) == 0) {
                // Append a number to make the filename unique
                char base_name[MAX_PATH_LENGTH];
                char extension[MAX_PATH_LENGTH] = "";
                strncpy(base_name, dest_path, MAX_PATH_LENGTH);
                
                char* dot = strrchr(base_name, '.');
                if (dot != NULL) {
                    strcpy(extension, dot);
                    *dot = '\0';
                }
                
                int counter = 1;
                do {
                    snprintf(dest_path, MAX_PATH_LENGTH, "%s_%d%s", base_name, counter, extension);
                    counter++;
                } while (access(dest_path, F_OK) == 0);
            }
            
            // Move the file
            if (rename(source_path, dest_path) == 0) {
                printf("Moved: %s -> %s\n", source_path, dest_path);
                moved_count++;
            } else {
                perror("Error moving file");
            }
        }
    }
    
    return moved_count;
}

int organize_duplicates_by_hash(DuplicateResults* results, int keep_index, const char* base_folder) {
    if (results == NULL || results->count == 0 || base_folder == NULL) {
        return 0;
    }
    
    if (!ensure_directory_exists(base_folder)) {
        return 0;
    }
    
    int moved_count = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        if (keep_index < 0 || keep_index >= group->count) {
            printf("[!] Invalid keep index for group %d\n", i + 1);
            continue;
        }
        
        // Create folder for this hash
        char hash_folder[MAX_PATH_LENGTH];
        snprintf(hash_folder, MAX_PATH_LENGTH, "%s/%s", base_folder, group->files[0].hash);
        
        if (!ensure_directory_exists(hash_folder)) {
            continue;
        }
        
        // Move all files except the one at keep_index
        for (int j = 0; j < group->count; j++) {
            if (j == keep_index) continue; // Skip the file we're keeping
            
            const char* source_path = group->files[j].path;
            
            // Extract filename from path
            const char* filename = strrchr(source_path, '/');
            if (filename == NULL) {
                filename = strrchr(source_path, '\\');
            }
            if (filename == NULL) {
                filename = source_path;
            } else {
                filename++; // Skip the slash
            }
            
            // Create destination path
            char dest_path[MAX_PATH_LENGTH];
            snprintf(dest_path, MAX_PATH_LENGTH, "%s/%s", hash_folder, filename);
            
            // Check if destination file already exists
            if (access(dest_path, F_OK) == 0) {
                // Append a number to make the filename unique
                char base_name[MAX_PATH_LENGTH];
                char extension[MAX_PATH_LENGTH] = "";
                strncpy(base_name, dest_path, MAX_PATH_LENGTH);
                
                char* dot = strrchr(base_name, '.');
                if (dot != NULL) {
                    strcpy(extension, dot);
                    *dot = '\0';
                }
                
                int counter = 1;
                do {
                    snprintf(dest_path, MAX_PATH_LENGTH, "%s_%d%s", base_name, counter, extension);
                    counter++;
                } while (access(dest_path, F_OK) == 0);
            }
            
            // Move the file
            if (rename(source_path, dest_path) == 0) {
                printf("Moved: %s -> %s\n", source_path, dest_path);
                moved_count++;
            } else {
                perror("Error moving file");
            }
        }
    }
    
    return moved_count;
}

bool ensure_directory_exists(const char* path) {
    struct stat st = {0};
    
    if (stat(path, &st) == -1) {
        // Directory doesn't exist, try to create it
        if (mkdir(path, 0755) == 0) {
            printf("Created directory: %s\n", path);
            return true;
        } else {
            perror("Error creating directory");
            return false;
        }
    } else if (S_ISDIR(st.st_mode)) {
        // Path exists and is a directory
        return true;
    } else {
        // Path exists but is not a directory
        fprintf(stderr, "Error: %s exists but is not a directory\n", path);
        return false;
    }
}