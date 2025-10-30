#include "report.h"
#include <time.h>

void display_results(DuplicateResults* results) {
    if (results == NULL || results->count == 0) {
        printf("No duplicate files to display.\n");
        return;
    }
    
    printf("\nDUPLICATE FILES REPORT\n");
    printf("======================\n");
    printf("Found %d groups of duplicate files.\n\n", results->count);
    
    // Calculate total wasted space
    long long total_wasted = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        
        printf("Group %d: %d duplicates\n", i + 1, group->count);
        printf("  Size: %lld bytes\n", (long long)group->files[0].size);
        printf("  Hash: %s\n", group->files[0].hash);
        
        // Calculate wasted space for this group
        long long group_wasted = (long long)(group->count - 1) * group->files[0].size;
        total_wasted += group_wasted;
        
        printf("  Wasted space: %lld bytes\n", group_wasted);
        printf("  Files:\n");
        
        // List all files in this group
        for (int j = 0; j < group->count; j++) {
            char time_str[26];
            ctime_r(&group->files[j].modified, time_str);
            time_str[24] = '\0';  // Remove newline
            
            printf("    %s (Modified: %s)\n", group->files[j].path, time_str);
        }
        
        printf("\n");
    }
    
    printf("SUMMARY\n");
    printf("=======\n");
    printf("Total duplicate groups: %d\n", results->count);
    printf("Total wasted space: %lld bytes (%.2f MB)\n", 
           total_wasted, (double)total_wasted / (1024 * 1024));
}
