#ifndef ACTIONS_H
#define ACTIONS_H

#include "common.h"

// Function to remove a specific file from a duplicate group
bool remove_file(const char* filepath);

// Function to remove all duplicate files except the first one in each group
int remove_duplicates_except_first(DuplicateResults* results);

// Function to remove all duplicate files except the one specified by index in each group
int remove_duplicates_except_index(DuplicateResults* results, int index);

// Function to interactively select which files to keep/remove
int interactive_removal(DuplicateResults* results);

// NEW: Functions for re-referencing files
int create_hard_links(DuplicateResults* results, int keep_index);
int create_symbolic_links(DuplicateResults* results, int keep_index);

// NEW: Functions for moving files
int move_duplicates_to_folder(DuplicateResults* results, int keep_index, const char* dest_folder);
int organize_duplicates_by_type(DuplicateResults* results, int keep_index, const char* base_folder);
int organize_duplicates_by_hash(DuplicateResults* results, int keep_index, const char* base_folder);

// Helper function to create a directory if it doesn't exist
bool ensure_directory_exists(const char* path);

#endif