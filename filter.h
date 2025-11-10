#ifndef FILTER_H
#define FILTER_H
#include "common.h"

void init_filter_config(FilterConfig* config);
bool add_filter(FilterConfig* config, FilterType type);
const char* get_hash_algorithm_name(HashAlgorithm algo);
const char* get_filter_type_name(FilterType type);
DuplicateResults find_duplicates(FileInfo* files, int file_count, const FilterConfig* config);
void free_duplicate_results(DuplicateResults* results);

#endif