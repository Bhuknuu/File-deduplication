#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "common.h"

bool save_scan_data(const char* filename, const ScanData* data);
bool load_scan_data(const char* filename, ScanData* data);
void free_scan_data(ScanData* data);

#endif