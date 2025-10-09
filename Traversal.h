#include "traversal.h"
#include <dirent.h>
#include <unistd.h>

bool scan_directories(FileGroup* files) {
    printf("Enter directory path to scan: ");
    char path[MAX_PATH_LENGTH];
    scanf("%s", path);
    
    
    
    DIR* dir = opendir(path);
    if (!dir) {
        show_error("Directory not found");
        return false;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { 
           
             This would be expanded with actual implementation
        }
    }
    
    closedir(dir);
    return true;
}

void cleanup_files(FileGroup* files) {
    free(files->files);
    files->files = NULL;
    files->count = 0;
}
