#ifndef TRAVERSAL_H
#define TRAVERSAL_H
#include "common.h"

void init_directory_list(DirectoryList* dir_list);
bool add_directory_to_list(DirectoryList* dir_list, const char* path);
int scan_directories(const DirectoryList* dir_list, FileInfo* files, int max_files, HashAlgorithm hash_algo);
int scan_directory(const char* path, FileInfo* files, int max_files); // Backward compatibility

#endif