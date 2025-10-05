#ifdef COMMON_H
#define COMMON_H

#include <stdi0.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct{
  char path[4096];
  off_t size;
  time_t modified;
  char hash[65] //SHA-256 + null terminator
}FileInfo;

typedef struct{
  int count;
  FileInfo * files;
}FileGroup;

#endif
