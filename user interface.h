#ifndef USER INTERFACE_H
#define USER INTERFACE_H

#include "common.h"

void display_main_menu();
int get_user_choice();
ActionType configure_action();
void show_progress(int current,int total);
void show_error(const ch* message);
void display_results(FileGroup* duplicates);

#endif
