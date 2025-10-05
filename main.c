#include "common.h"
#include "traversal.h"

// THIS IS MAIN FILE OF FILE DEDUPLICATION PROJECT
// IMPORTING ALL THE NECESSARY LIBRARIES AND HEADER FILES

//User Interface functions
//Filter functions
//Progress bar function
//Hashing functions
//result display functions
//Traversing functions

int main() {
    FileGroup all_files = {0};
    FileGroup duplicates = {0};
    ActionType action = ACTION_REPORT;
    
    while (true) {
        display_main_menu();
        
        switch (get_user_choice()) {
            case 1: // Scan directories
                if (scan_directories(&all_files)) {
                    printf("Scan completed! Total files found: %d.\n", all_files.count);
                }
                break;
                
            case 2: // Find duplicates
                if (all_files.count > 0) {
                    find_duplicates(&all_files, &duplicates);
                    printf("Duplicate groups found: %d\n", duplicates.count);
                } else {
                    printf("Scan directories first!\n");
                }
                break;
                
            case 3: // Configure action
                action = configure_action();
                break;

    
            case 4: // Execute action
                if (duplicates.count > 0) {
                    execute_action(&duplicates, action);
                } else {
                    printf("Find duplicates first!\n");
                }
                break;
                
            case 5: // Exit
                cleanup(&all_files, &duplicates);
                printf("Exiting...\n");
                return 0;
                
            default:
                printf("Invalid choice. Try again.\n");
        }
    }
}
