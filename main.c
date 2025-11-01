#include "common.h"
#include "traversal.h"
#include "filter.h"
#include "report.h"

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
    printf("4. Exit\n");
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
<<<<<<< HEAD
=======
    
>>>>>>> e2b2e8a70c7a6611b0b2d6b02c53d5fb04fbd074
    print_banner();
    while (1) {
        print_menu();
        printf("[->] Enter your choice: ");
        
<<<<<<< HEAD
        // Check if scanf succeeds
        if (scanf("%d", &choice) != 1){
            printf("[!] Invalid input. Please enter a number.\n");
=======
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            fflush(stdout);
            clear_input_buffer();
>>>>>>> e2b2e8a70c7a6611b0b2d6b02c53d5fb04fbd074
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
                
            case 4: // exit selected 
                free_duplicate_results(&duplicates);
                free(files);
                printf("[*] Exiting program. Seeya!\n");
                return 0;
                
            default:
                printf("[!] Invalid choice. Please try again.\n");
        }
        
<<<<<<< HEAD
        printf("\nPress any key to continue");
        getchar();  // for pause 
    }
=======
        printf("\nPress Enter to continue...");
        fflush(stdout);
        getchar();  
    }
    
>>>>>>> e2b2e8a70c7a6611b0b2d6b02c53d5fb04fbd074
    free(files);
    return 0;
}
