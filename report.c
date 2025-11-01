#include "report.h"
#include <time.h>

// Helper function to print large numbers in human-readable format
void print_size(long long size) {
    if (size < 1024) {
        printf("%ld bytes", (long)size);
    } else if (size < 1024 * 1024) {
        printf("%.2f KB", (double)size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        printf("%.2f MB", (double)size / (1024.0 * 1024.0));
    } else {
        printf("%.2f GB", (double)size / (1024.0 * 1024.0 * 1024.0));
    }
}

void display_results(DuplicateResults* results) {
    if (results == NULL || results->count == 0) {
        printf("No duplicate files to display.\n");
        return;
    }
    
    printf("\nDUPLICATE FILES REPORT\n");
    printf("======================\n");
    printf("Found %d groups of duplicate files.\n\n", results->count);
    fflush(stdout);
  
    long long total_wasted = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        printf("Group %d: %d duplicates\n", i + 1, group->count);
    
        printf("  Size: ");
        print_size((long long)group->files[0].size);
        printf("\n");
        
        printf("  Hash: %s\n", group->files[0].hash);
        
        // Calculate wasted space (no of group members - 1) * size of one file
        long long group_wasted = (long long)(group->count - 1) * group->files[0].size;
        total_wasted += group_wasted;
        
        printf("  Wasted space: ");
        print_size(group_wasted);
        printf("\n");
        
        printf("  Files:\n");
        
        for (int j = 0; j < group->count; j++) {
            char* time_str = ctime(&group->files[j].modified);
            
            if (time_str != NULL) {
                char* newline = strchr(time_str, '\n');
                if (newline) *newline = '\0';
            }
            
            printf("    %s (Modified: %s)\n", 
                   group->files[j].path, 
                   time_str ? time_str : "Invalid time");
        }
        
        printf("\n");
        fflush(stdout);
    }
    
    printf("SUMMARY\n");
    printf("=======\n");
    printf("Total duplicate groups: %d\n", results->count);
 
    printf("Total wasted space: ");
    print_size(total_wasted);
    printf(" (%.2f MB)\n", (double)total_wasted / (1024.0 * 1024.0));
    
    fflush(stdout);
}
