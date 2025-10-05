#include "user interface.h"
#include<string.h>

void display_main_menu()
{ 
  printf("\n FILE DEDUPLICATION SYSTEM\n");
  printf("1. Scan Directories\n");
  printf("2. Find Duplicate files\n");
  printf("3. ");
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
