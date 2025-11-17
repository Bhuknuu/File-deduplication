#include "common.h"

bool save_scan_data(const char* filename, const ScanData* data) {
    if (!filename || !data) return false;
    
    FILE* file = fopen(filename, "wb");
    if (!file) return false;
    
    bool success = true;
    success &= fwrite(&data->version, sizeof(int), 1, file) == 1;
    success &= fwrite(&data->scan_time, sizeof(time_t), 1, file) == 1;
    success &= fwrite(&data->file_count, sizeof(int), 1, file) == 1;
    success &= fwrite(&data->dir_list, sizeof(DirectoryList), 1, file) == 1;
    success &= fwrite(&data->filter_config, sizeof(FilterConfig), 1, file) == 1;
    
    if (success && data->files) {
        for (int i = 0; i < data->file_count; i++) {
            success &= fwrite(&data->files[i], sizeof(FileInfo), 1, file) == 1;
            if (!success) break;
        }
    }
    
    fclose(file);
    return success;
}

bool load_scan_data(const char* filename, ScanData* data) {
    if (!filename || !data) return false;
    
    FILE* file = fopen(filename, "rb");
    if (!file) return false;
    
    bool success = true;
    success &= fread(&data->version, sizeof(int), 1, file) == 1;
    
    if (!success || data->version != SCAN_DATA_VERSION) {
        fclose(file);
        return false;
    }
    
    success &= fread(&data->scan_time, sizeof(time_t), 1, file) == 1;
    success &= fread(&data->file_count, sizeof(int), 1, file) == 1;
    success &= fread(&data->dir_list, sizeof(DirectoryList), 1, file) == 1;
    success &= fread(&data->filter_config, sizeof(FilterConfig), 1, file) == 1;
    
    if (success && data->file_count > 0) {
        data->files = malloc(data->file_count * sizeof(FileInfo));
        if (!data->files) {
            fclose(file);
            return false;
        }
        
        for (int i = 0; i < data->file_count; i++) {
            success &= fread(&data->files[i], sizeof(FileInfo), 1, file) == 1;
            if (!success) break;
        }
    }
    
    fclose(file);
    return success;
}

void free_scan_data(ScanData* data) {
    if (!data) return;
    if (data->files) {
        free(data->files);
        data->files = NULL;
    }
    memset(data, 0, sizeof(ScanData));
}