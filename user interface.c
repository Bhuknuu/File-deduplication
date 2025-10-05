#include "user interface.h"
#include<string.h>

void display_main_menu()
{ 
  printf("\n FILE DEDUPLICATION SYSTEM\n");
  printf("1. Scan Directories\n");
  printf("2. Find Duplicate files\n");
  printf("3. Configure Action\n");
  printf("4. Execute Action\n");
  printf("5. Exit\n");
  printf("Enter your choice:");
}

int get_user_choice()
{
  int ch;
  scanf("%d",&ch);
  return ch;
}

ActionType configure_action()
{
  printf("\nCONFIGURE ACTION\n");
  printf("1. Delete duplicate files\n");
  printf("2. Move duplicate files\n");
  printf("3. Create hard links\n");
  printf("4. Report only\n");
  printf("5.Select Action:");
  
  int action;
  scanf("%d",&action);
  return(ActionType)(action-1);
}

void show_progress(int current, int total)
{
  printf("\rProgress: %d/%d files (%.1f%%)", current, total, (float)current/total*100);
  fflush(stdout);
}

void show_error(const char* message)
{
  fprintf(stderr,"ERROR: %s\n", message);
}

void display_results(FileGroup* duplicates)
{
  printf("\n DUPLICATE FILES \n");
  for(int i=0;i<duplicates->count;i++)
    {
      printf("Group %d: \n", i+1);
      printf("Size: %d bytes\n", duplicates->files[i].size);
      printf("Hash: %s \n" duplicates->files[i].hash);
      printf("Paths: \n");
    }
}
