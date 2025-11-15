#ifndef TRAVERSAL_H
#define TRAVERSAL_H

#include "common.h"

void init_directory_list(DirectoryList* dir_list);
bool add_directory_to_list(DirectoryList* dir_list, const char* path);
int scan_directories_recursive(const DirectoryList* dir_list, FileInfo* files, int max_files, HashAlgorithm hash_algo);

#endif