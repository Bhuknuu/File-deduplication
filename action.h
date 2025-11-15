#ifndef ACTIONS_H
#define ACTIONS_H

#include "common.h"

// Function to remove a specific file
bool remove_file(const char* filepath);

// Function to remove all duplicate files except the first one in each group
int remove_duplicates_except_first(DuplicateResults* results);

// Function to remove all duplicate files except the one specified by index in each group
int remove_duplicates_except_index(DuplicateResults* results, int index);

// Function to move duplicate files to a specified folder
int move_duplicates_to_folder(DuplicateResults* results, int keep_index, const char* dest_folder);

// Helper function to create a directory if it doesn't exist
bool ensure_directory_exists(const char* path);

#endif