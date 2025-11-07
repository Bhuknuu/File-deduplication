#include "common.h"
#include "traversal.h"
#include "filter.h"
#include "report.h"
#include "action.h"

void print_banner() {
    printf("========================================\n");
    printf("    FILE DEDUPLICATION SYSTEM (DEMO)    \n");
    printf("========================================\n");
    printf("TCS-302 Data Structures with C\n");
}

void print_menu() {
    printf("\nMAIN MENU\n");
    printf("1. Scan Directory\n");
    printf("2. Find Duplicates\n");
    printf("3. Display Results\n");
    printf("4. Remove Duplicates\n");
    printf("5. Re-reference Duplicates\n");
    printf("6. Move Duplicates\n");
    printf("7. Exit\n");
}

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void trim_newline(char* str) {
    int len = strlen(str);
    if (len > 0 && str[len-1] == '\n') {
        str[len-1] = '\0';
    }
}

int main() {
    char directory[MAX_PATH_LENGTH];
    int choice;
    FileInfo* files = NULL;
    int file_count = 0;
    DuplicateResults duplicates = {0};
    
    // Allocate files array on heap to avoid stack overflow
    files = (FileInfo*)malloc(MAX_FILES * sizeof(FileInfo));
    
    if (files==NULL) {
        fprintf(stderr, "[!] FATAL ERROR: Cannot allocate memory for file array\n");
        fprintf(stderr, "[!] Attempted to allocate %lu bytes\n", 
                (unsigned long)(MAX_FILES * sizeof(FileInfo)));
        fprintf(stderr, "Press any key to exit.\n");
        fflush(stderr);
        getchar();
        return 1;
    }
    print_banner();
    while (1) {
        print_menu();
        printf("[->] Enter your choice: ");
        
        // Check if scanf succeeds
        if (scanf("%d", &choice) != 1){
            printf("[!] Invalid input. Please enter a number.\n");
            continue;
        }
        clear_input_buffer();
        
        switch (choice) {
            case 1: // Scan directory
                printf("[->] Enter directory path to scan: ");
                
                if (fgets(directory, MAX_PATH_LENGTH, stdin) == NULL) {
                    printf("[!] Error reading input.\n");
                    break;
                }
                trim_newline(directory);
                
                printf("[*] Scanning directory: %s\n", directory);
                
                file_count = scan_directory(directory, files, MAX_FILES);
                if (file_count > 0) {
                    printf("[ ^___^ ]Scan completed. Found %d files.\n", file_count);
                } else {
                    printf("[ ^___^ ]No files found or error scanning directory.\n");
                }
                break;
                
            case 2: // Find duplicates
                if (file_count == 0) {
                    printf("[!] Please scan a directory first!.\n");
                    break;
                }
                
                free_duplicate_results(&duplicates);
                duplicates = find_duplicates(files, file_count);
                
                if (duplicates.count > 0) {
                    printf("[ ^___^ ] Found %d groups of duplicate files.\n", duplicates.count);
                } else {
                    printf("[ T___T ] No duplicate files found.\n");
                }
                break;
                
            case 3: // Display results
                if (duplicates.count == 0) {
                    printf("[!] No results to display. Please find duplicates first.\n");
                    break;
                }
                
                display_results(&duplicates);
                break;
                
            case 4: // Remove duplicates
                if (duplicates.count == 0) {
                    printf("[!] No duplicates to remove. Please find duplicates first.\n");
                    break;
                }
                
                printf("\nREMOVAL OPTIONS\n");
                printf("1. Remove all duplicates except the first file in each group\n");
                printf("2. Remove all duplicates except the oldest file in each group\n");
                printf("3. Remove all duplicates except the newest file in each group\n");
                printf("4. Interactively select files to keep\n");
                printf("5. Go back to main menu\n");
                printf("[->] Enter your choice: ");
                
                int removal_choice;
                if (scanf("%d", &removal_choice) != 1) {
                    printf("[!] Invalid input. Please enter a number.\n");
                    clear_input_buffer();
                    break;
                }
                clear_input_buffer();
                
                int removed_count = 0;
                switch (removal_choice) {
                    case 1:
                        removed_count = remove_duplicates_except_first(&duplicates);
                        break;
                    case 2: {
                        // Find the oldest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int oldest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified < group->files[oldest_index].modified) {
                                    oldest_index = j;
                                }
                            }
                            removed_count += remove_duplicates_except_index(&duplicates, oldest_index);
                        }
                        break;
                    }
                    case 3: {
                        // Find the newest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int newest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified > group->files[newest_index].modified) {
                                    newest_index = j;
                                }
                            }
                            removed_count += remove_duplicates_except_index(&duplicates, newest_index);
                        }
                        break;
                    }
                    case 4:
                        removed_count = interactive_removal(&duplicates);
                        break;
                    case 5:
                        // Go back to main menu
                        break;
                    default:
                        printf("[!] Invalid choice. Please try again.\n");
                }
                
                if (removed_count > 0) {
                    printf("[ ^___^ ] Successfully removed %d duplicate files.\n", removed_count);
                    // Clear the results since we've modified the file system
                    free_duplicate_results(&duplicates);
                    file_count = 0;
                } else if (removal_choice >= 1 && removal_choice <= 4) {
                    printf("[ T___T ] No files were removed.\n");
                }
                break;
                
            case 5: // Re-reference duplicates
                if (duplicates.count == 0) {
                    printf("[!] No duplicates to re-reference. Please find duplicates first.\n");
                    break;
                }
                
                printf("\nRE-REFERENCE OPTIONS\n");
                printf("1. Create hard links (keeps first file in each group)\n");
                printf("2. Create hard links (keeps oldest file in each group)\n");
                printf("3. Create hard links (keeps newest file in each group)\n");
                printf("4. Create symbolic links (keeps first file in each group)\n");
                printf("5. Create symbolic links (keeps oldest file in each group)\n");
                printf("6. Create symbolic links (keeps newest file in each group)\n");
                printf("7. Go back to main menu\n");
                printf("[->] Enter your choice: ");
                
                int re_ref_choice;
                if (scanf("%d", &re_ref_choice) != 1) {
                    printf("[!] Invalid input. Please enter a number.\n");
                    clear_input_buffer();
                    break;
                }
                clear_input_buffer();
                
                int link_count = 0;
                switch (re_ref_choice) {
                    case 1:
                        link_count = create_hard_links(&duplicates, 0);
                        break;
                    case 2: {
                        // Find the oldest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int oldest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified < group->files[oldest_index].modified) {
                                    oldest_index = j;
                                }
                            }
                            link_count += create_hard_links(&duplicates, oldest_index);
                        }
                        break;
                    }
                    case 3: {
                        // Find the newest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int newest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified > group->files[newest_index].modified) {
                                    newest_index = j;
                                }
                            }
                            link_count += create_hard_links(&duplicates, newest_index);
                        }
                        break;
                    }
                    case 4:
                        link_count = create_symbolic_links(&duplicates, 0);
                        break;
                    case 5: {
                        // Find the oldest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int oldest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified < group->files[oldest_index].modified) {
                                    oldest_index = j;
                                }
                            }
                            link_count += create_symbolic_links(&duplicates, oldest_index);
                        }
                        break;
                    }
                    case 6: {
                        // Find the newest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int newest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified > group->files[newest_index].modified) {
                                    newest_index = j;
                                }
                            }
                            link_count += create_symbolic_links(&duplicates, newest_index);
                        }
                        break;
                    }
                    case 7:
                        // Go back to main menu
                        break;
                    default:
                        printf("[!] Invalid choice. Please try again.\n");
                }
                
                if (link_count > 0) {
                    printf("[ ^___^ ] Successfully created %d links.\n", link_count);
                    // Clear the results since we've modified the file system
                    free_duplicate_results(&duplicates);
                    file_count = 0;
                } else if (re_ref_choice >= 1 && re_ref_choice <= 6) {
                    printf("[ T___T ] No links were created.\n");
                }
                break;
                
            case 6: // Move duplicates
                if (duplicates.count == 0) {
                    printf("[!] No duplicates to move. Please find duplicates first.\n");
                    break;
                }
                
                printf("\nMOVE OPTIONS\n");
                printf("1. Move duplicates to a single folder (keeps first file in each group)\n");
                printf("2. Move duplicates to a single folder (keeps oldest file in each group)\n");
                printf("3. Move duplicates to a single folder (keeps newest file in each group)\n");
                printf("4. Organize duplicates by file type (keeps first file in each group)\n");
                printf("5. Organize duplicates by file type (keeps oldest file in each group)\n");
                printf("6. Organize duplicates by file type (keeps newest file in each group)\n");
                printf("7. Organize duplicates by hash (keeps first file in each group)\n");
                printf("8. Organize duplicates by hash (keeps oldest file in each group)\n");
                printf("9. Organize duplicates by hash (keeps newest file in each group)\n");
                printf("10. Go back to main menu\n");
                printf("[->] Enter your choice: ");
                
                int move_choice;
                if (scanf("%d", &move_choice) != 1) {
                    printf("[!] Invalid input. Please enter a number.\n");
                    clear_input_buffer();
                    break;
                }
                clear_input_buffer();
                
                // Declare dest_folder here to fix the undeclared variable error
                char dest_folder[MAX_PATH_LENGTH];
                
                if (move_choice >= 1 && move_choice <= 9) {
                    printf("[->] Enter destination folder path: ");
                    if (fgets(dest_folder, MAX_PATH_LENGTH, stdin) == NULL) {
                        printf("[!] Error reading input.\n");
                        break;
                    }
                    trim_newline(dest_folder);
                }
                
                int moved_count = 0;
                switch (move_choice) {
                    case 1:
                        moved_count = move_duplicates_to_folder(&duplicates, 0, dest_folder);
                        break;
                    case 2: {
                        // Find the oldest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int oldest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified < group->files[oldest_index].modified) {
                                    oldest_index = j;
                                }
                            }
                            moved_count += move_duplicates_to_folder(&duplicates, oldest_index, dest_folder);
                        }
                        break;
                    }
                    case 3: {
                        // Find the newest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int newest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified > group->files[newest_index].modified) {
                                    newest_index = j;
                                }
                            }
                            moved_count += move_duplicates_to_folder(&duplicates, newest_index, dest_folder);
                        }
                        break;
                    }
                    case 4:
                        moved_count = organize_duplicates_by_type(&duplicates, 0, dest_folder);
                        break;
                    case 5: {
                        // Find the oldest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int oldest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified < group->files[oldest_index].modified) {
                                    oldest_index = j;
                                }
                            }
                            moved_count += organize_duplicates_by_type(&duplicates, oldest_index, dest_folder);
                        }
                        break;
                    }
                    case 6: {
                        // Find the newest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int newest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified > group->files[newest_index].modified) {
                                    newest_index = j;
                                }
                            }
                            moved_count += organize_duplicates_by_type(&duplicates, newest_index, dest_folder);
                        }
                        break;
                    }
                    case 7:
                        moved_count = organize_duplicates_by_hash(&duplicates, 0, dest_folder);
                        break;
                    case 8: {
                        // Find the oldest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int oldest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified < group->files[oldest_index].modified) {
                                    oldest_index = j;
                                }
                            }
                            moved_count += organize_duplicates_by_hash(&duplicates, oldest_index, dest_folder);
                        }
                        break;
                    }
                    case 9: {
                        // Find the newest file in each group
                        for (int i = 0; i < duplicates.count; i++) {
                            DuplicateGroup* group = &duplicates.groups[i];
                            int newest_index = 0;
                            for (int j = 1; j < group->count; j++) {
                                if (group->files[j].modified > group->files[newest_index].modified) {
                                    newest_index = j;
                                }
                            }
                            moved_count += organize_duplicates_by_hash(&duplicates, newest_index, dest_folder);
                        }
                        break;
                    }
                    case 10:
                        // Go back to main menu
                        break;
                    default:
                        printf("[!] Invalid choice. Please try again.\n");
                }
                
                if (moved_count > 0) {
                    printf("[ ^___^ ] Successfully moved %d duplicate files.\n", moved_count);
                    // Clear the results since we've modified the file system
                    free_duplicate_results(&duplicates);
                    file_count = 0;
                } else if (move_choice >= 1 && move_choice <= 9) {
                    printf("[ T___T ] No files were moved.\n");
                }
                break;
                
            case 7: // exit selected 
                free_duplicate_results(&duplicates);
                free(files);
                printf("[*] Exiting program. Seeya!\n");
                return 0;
                
            default:
                printf("[!] Invalid choice. Please try again.\n");
        }
        
        printf("\nPress any key to continue");
        getchar();  // for pause 
    }
    free(files);
    return 0;
}