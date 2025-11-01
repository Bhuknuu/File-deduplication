#ifndef FILTER_H
#define FILTER_H

#include "common.h"

DuplicateResults find_duplicates(FileInfo* files, int file_count);
void free_duplicate_results(DuplicateResults* results);

#endif 
