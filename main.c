#include "common.h"
#include "traversal.h"
#include "filter.h"
#include "report.h"

void print_banner() {
    printf("========================================\n");
    printf("    FILE DEDUPLICATION SYSTEM (DEMO)    \n");
    printf("========================================\n");
    printf("TCS-302 Data Structures with C\n\n");
    fflush(stdout);
}

void print_menu() {
    printf("\nMAIN MENU\n");
    printf("1. Scan Directory\n");
    printf("2. Find Duplicates\n");
    printf("3. Display Results\n");
    printf("4. Exit\n");
    fflush(stdout);
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
    if (files == NULL) {
        fprintf(stderr, "FATAL ERROR: Cannot allocate memory for file array\n");
        fprintf(stderr, "Attempted to allocate %lu bytes\n", 
                (unsigned long)(MAX_FILES * sizeof(FileInfo)));
        fprintf(stderr, "Press Enter to exit...\n");
        fflush(stderr);
        getchar();
        return 1;
    }
    
    print_banner();
    
    while (1) {
        print_menu();
        printf("Enter your choice: ");
        fflush(stdout);
        
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            fflush(stdout);
            clear_input_buffer();
            continue;
        }
        clear_input_buffer();
        
        switch (choice) {
            case 1: // Scan directory
                printf("Enter directory path to scan: ");
                fflush(stdout);
                
                if (fgets(directory, MAX_PATH_LENGTH, stdin) == NULL) {
                    printf("Error reading input.\n");
                    fflush(stdout);
                    break;
                }
                trim_newline(directory);
                
                printf("Scanning directory: %s\n", directory);
                fflush(stdout);
                
                file_count = scan_directory(directory, files, MAX_FILES);
                if (file_count > 0) {
                    printf("Scan completed. Found %d files.\n", file_count);
                } else {
                    printf("No files found or error scanning directory.\n");
                }
                fflush(stdout);
                break;
                
            case 2: // Find duplicates
                if (file_count == 0) {
                    printf("Please scan a directory first.\n");
                    fflush(stdout);
                    break;
                }
                
                free_duplicate_results(&duplicates);
                duplicates = find_duplicates(files, file_count);
                
                if (duplicates.count > 0) {
                    printf("Found %d groups of duplicate files.\n", duplicates.count);
                } else {
                    printf("No duplicate files found.\n");
                }
                fflush(stdout);
                break;
                
            case 3: // Display results
                if (duplicates.count == 0) {
                    printf("No results to display. Please find duplicates first.\n");
                    fflush(stdout);
                    break;
                }
                
                display_results(&duplicates);
                break;
                
            case 4: // Exit
                free_duplicate_results(&duplicates);
                free(files);
                printf("Exiting program. Goodbye!\n");
                fflush(stdout);
                return 0;
                
            default:
                printf("Invalid choice. Please try again.\n");
                fflush(stdout);
        }
        
        printf("\nPress Enter to continue...");
        fflush(stdout);
        getchar();  
    }
    
    free(files);
    return 0;
}
