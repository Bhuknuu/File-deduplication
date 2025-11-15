#ifndef GUI_WIN32_H
#define GUI_WIN32_H

#include <windows.h>
#include "common.h"

// Function to register the window class
ATOM RegisterWindowClass(HINSTANCE hInstance);

// Function to create the main window
HWND CreateMainWindow(HINSTANCE hInstance, int nCmdShow);

// Window procedure for the main window
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// GUI control IDs
#define IDC_BTN_ADD_DIR 1001
#define IDC_BTN_REMOVE_DIR 1002
#define IDC_BTN_SCAN 1003
#define IDC_BTN_FIND 1004
#define IDC_BTN_SAVE 1005
#define IDC_BTN_LOAD 1006
#define IDC_BTN_DELETE_FIRST 1007
#define IDC_BTN_DELETE_INDEX 1008
#define IDC_BTN_MOVE 1009
#define IDC_LISTBOX_DIRS 2001
#define IDC_LISTVIEW_RESULTS 2002
#define IDC_EDIT_STATUS 2003
#define IDC_PROGRESS 2004
#define IDC_CHECK_SUBDIRS 3001
#define IDC_COMBO_HASH 3002

#endif