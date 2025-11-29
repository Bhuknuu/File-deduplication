/*
 * GUI_WIN32.C - Windows GUI interface (FIXED VERSION)
 */

#include "common.h"

// Add library linking
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// Progress bar support
#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif

// ListView Extended Styles
#ifndef LVM_SETEXTENDEDLISTVIEWSTYLE
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#endif
#ifndef LVS_EX_FULLROWSELECT
#define LVS_EX_FULLROWSELECT 0x00000020
#endif
#ifndef LVS_EX_GRIDLINES
#define LVS_EX_GRIDLINES 0x00000001
#endif

// Control IDs
#define IDC_BTN_ADD_DIR          1001
#define IDC_BTN_REMOVE_DIR       1002
#define IDC_BTN_ADD_EXCLUSION    1003
#define IDC_BTN_REMOVE_EXCLUSION 1004
#define IDC_BTN_SCAN             1005
#define IDC_BTN_FIND             1006
#define IDC_BTN_DELETE_FIRST     1007
#define IDC_BTN_MOVE             1008
#define IDC_BTN_HARD_LINK        1009
#define IDC_BTN_DELETE_BY_INDEX  1010

#define IDC_LISTBOX_DIRS         2001
#define IDC_LISTBOX_EXCLUSIONS   2002
#define IDC_LISTVIEW_RESULTS     2003
#define IDC_EDIT_STATUS          2004
#define IDC_PROGRESS             2005
#define IDC_CHECK_SUBDIRS        3001
#define IDC_COMBO_HASH           3002

// Input dialog IDs
#define ID_INPUT_DIALOG          4000
#define ID_INPUT_EDIT            4001
#define ID_INPUT_OK              4002
#define ID_INPUT_CANCEL          4003
#define ID_STATIC                -1

// Global data
CRITICAL_SECTION g_dataLock;
ProgressInfo g_progress = {0};

static ScanConfig g_config = {0};
static FileInfo* g_files = NULL;
static int g_file_count = 0;
static DuplicateResults g_results = {0};

// Window handles
static HWND g_hwndMain;
static HWND g_listDirs;
static HWND g_listExclusions;
static HWND g_listResults;
static HWND g_editStatus;
static HWND g_hwndProgress;  // Renamed from g_progress to avoid conflict
static HWND g_checkSubdirs;
static HWND g_comboHash;
static HWND g_btnScan;
static HWND g_btnFind;
static HWND g_btnDeleteByIndex;

// Thread handles
static HANDLE g_hScanThread = NULL;
static HANDLE g_hFindThread = NULL;

// Input dialog variables
static int g_inputValue = -1;
static bool g_inputDialogResult = false;

// Function prototypes
BOOL InputBox(HWND hwnd, const char* prompt, char* buffer, int bufferSize);
INT_PTR CALLBACK InputDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void AppendStatus(const char* text) {
    if (!text) return;
    
    int len = GetWindowTextLength(g_editStatus);
    SendMessage(g_editStatus, EM_SETSEL, len, len);
    SendMessage(g_editStatus, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void UpdateListView() {
    ListView_DeleteAllItems(g_listResults);
    
    EnterCriticalSection(&g_dataLock);
    
    for (int i = 0; i < g_results.count; i++) {
        if (i >= g_results.count || !g_results.groups) break;
        
        DuplicateGroup* g = &g_results.groups[i];
        if (!g || !g->files) continue;
        
        for (int j = 0; j < g->count; j++) {
            if (j >= g->count) break;
            
            char group_text[64];
            snprintf(group_text, sizeof(group_text), "Group %d (%d/%d)", 
                    i + 1, j + 1, g->count);
            
            LVITEMA lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(g_listResults);
            lvi.pszText = group_text;
            int idx = ListView_InsertItem(g_listResults, &lvi);
            
            if (idx == -1) continue;
            
            char size_text[32];
            format_file_size(g->files[j].size, size_text, sizeof(size_text));
            ListView_SetItemText(g_listResults, idx, 1, size_text);
            
            const char* filename = strrchr(g->files[j].path, '\\');
            filename = filename ? filename + 1 : g->files[j].path;
            
            char short_name[64];
            strncpy(short_name, filename, 60);
            short_name[60] = '\0';
            if (strlen(filename) > 60) strcat(short_name, "...");
            ListView_SetItemText(g_listResults, idx, 2, short_name);
            
            ListView_SetItemText(g_listResults, idx, 3, g->files[j].path);
        }
    }
    
    char status[256];
    snprintf(status, sizeof(status), 
            "Found %d duplicate groups with %d total files\r\n", 
            g_results.count, ListView_GetItemCount(g_listResults));
    
    LeaveCriticalSection(&g_dataLock);
    
    AppendStatus(status);
}

// Input dialog procedure
INT_PTR CALLBACK InputDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            // Center the dialog
            RECT rc, rcParent;
            GetWindowRect(hwnd, &rc);
            GetWindowRect(GetParent(hwnd), &rcParent);
            SetWindowPos(hwnd, NULL, 
                rcParent.left + (rcParent.right - rcParent.left - (rc.right - rc.left)) / 2,
                rcParent.top + (rcParent.bottom - rcParent.top - (rc.bottom - rc.top)) / 2,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
            
            // Set default value
            SetDlgItemInt(hwnd, ID_INPUT_EDIT, 1, FALSE);
            return TRUE;
        }
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_INPUT_OK:
                    g_inputValue = GetDlgItemInt(hwnd, ID_INPUT_EDIT, NULL, FALSE);
                    g_inputDialogResult = true;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                    
                case ID_INPUT_CANCEL:
                    g_inputDialogResult = false;
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

// Show input dialog and get index value
bool ShowIndexInputDialog(int* result) {
    g_inputDialogResult = false;
    g_inputValue = -1;
    
    // Create a simple input dialog
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    // Create dialog template in memory
    typedef struct {
        DLGTEMPLATE  dlgTemplate;
        WORD         menu;
        WORD         windowClass;
        WCHAR        title[32];
        WORD         pointsize;
        WCHAR        font[32];
    } MyDialogTemplate;
    
    MyDialogTemplate template = {0};
    template.dlgTemplate.style = WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    template.dlgTemplate.dwExtendedStyle = 0;
    template.dlgTemplate.cdit = 3; // Number of controls
    template.dlgTemplate.x = 0;
    template.dlgTemplate.y = 0;
    template.dlgTemplate.cx = 200;
    template.dlgTemplate.cy = 100;
    template.menu = 0;
    template.windowClass = 0;
    MultiByteToWideChar(CP_ACP, 0, "Select Index to Keep", -1, template.title, 32);
    template.pointsize = 8;
    MultiByteToWideChar(CP_ACP, 0, "MS Shell Dlg", -1, template.font, 32);
    
    // Create the dialog
    HWND hDlg = CreateDialogIndirect(hInstance, (LPCDLGTEMPLATE)&template, g_hwndMain, InputDialogProc);
    if (!hDlg) {
        // Fallback to a simpler approach - use MessageBox
        char msg[256];
        sprintf(msg, "Enter index of file to keep in console and press OK when done");
        MessageBoxA(g_hwndMain, msg, "Input Required", MB_OK | MB_ICONINFORMATION);
        
        // Get input from console (fallback)
        printf("Enter index of file to keep: ");
        char consoleInput[32];
        fgets(consoleInput, sizeof(consoleInput), stdin);
        consoleInput[strcspn(consoleInput, "\n")] = '\0';
        *result = atoi(consoleInput);
        return true;
    }
    
    // Add controls manually
    CreateWindowA("STATIC", "Enter index of file to keep:", WS_CHILD | WS_VISIBLE, 
                  10, 10, 180, 20, hDlg, (HMENU)ID_STATIC, hInstance, NULL);
    CreateWindowA("EDIT", "1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 
                  10, 30, 180, 20, hDlg, (HMENU)ID_INPUT_EDIT, hInstance, NULL);
    CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 
                  60, 55, 40, 20, hDlg, (HMENU)ID_INPUT_OK, hInstance, NULL);
    CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                  110, 55, 40, 20, hDlg, (HMENU)ID_INPUT_CANCEL, hInstance, NULL);
    
    // Show dialog
    ShowWindow(hDlg, SW_SHOW);
    
    // Message loop for dialog
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        if (msg.message == WM_NULL && msg.hwnd == hDlg) {
            break;
        }
    }
    
    // Get result
    if (g_inputDialogResult) {
        *result = g_inputValue;
    }
    
    DestroyWindow(hDlg);
    return g_inputDialogResult;
}

DWORD WINAPI ScanThread(LPVOID param) {
    AppendStatus("Scanning directories...\r\n");
    
    EnterCriticalSection(&g_dataLock);
    if (g_files) free(g_files);
    g_files = (FileInfo*)malloc(MAX_FILES * sizeof(FileInfo));
    LeaveCriticalSection(&g_dataLock);
    
    if (!g_files) {
        AppendStatus("ERROR: Out of memory!\r\n");
        PostMessage(g_hwndMain, WM_SCAN_COMPLETE, 0, 0);
        return 1;
    }
    
    EnterCriticalSection(&g_dataLock);
    ScanConfig config_copy = g_config;
    LeaveCriticalSection(&g_dataLock);
    
    int count = scan_directories(&config_copy, g_files, MAX_FILES);
    
    EnterCriticalSection(&g_dataLock);
    g_file_count = count;
    LeaveCriticalSection(&g_dataLock);
    
    char status[128];
    snprintf(status, sizeof(status), "Scan complete! Found %d files\r\n", count);
    AppendStatus(status);
    
    PostMessage(g_hwndMain, WM_SCAN_COMPLETE, 0, 0);
    return 0;
}

DWORD WINAPI FindThread(LPVOID param) {
    AppendStatus("Finding duplicates...\r\n");
    
    EnterCriticalSection(&g_dataLock);
    free_duplicate_results(&g_results);
    g_results = find_duplicates(g_files, g_file_count);
    LeaveCriticalSection(&g_dataLock);
    
    UpdateListView();
    
    PostMessage(g_hwndMain, WM_FIND_COMPLETE, 0, 0);
    return 0;
}

void UpdateProgressBar() {
    EnterCriticalSection(&g_dataLock);
    int files_scanned = g_progress.files_scanned;
    bool is_complete = g_progress.is_complete;
    LeaveCriticalSection(&g_dataLock);
    
    HWND hProgress = GetDlgItem(g_hwndMain, IDC_PROGRESS);
    
    if (is_complete) {
        SendMessage(hProgress, PBM_SETPOS, 100, 0);
        SendMessage(hProgress, PBM_SETMARQUEE, FALSE, 0);
        
        LONG style = GetWindowLong(hProgress, GWL_STYLE);
        style &= ~PBS_MARQUEE;
        SetWindowLong(hProgress, GWL_STYLE, style);
    } else if (files_scanned > 0) {
        int percent = (files_scanned % 100);
        SendMessage(hProgress, PBM_SETPOS, percent, 0);
    }
}

void OnAddDirectory() {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwndMain;
    bi.lpszTitle = "Select Directory to Scan";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            EnterCriticalSection(&g_dataLock);
            bool added = add_directory(&g_config.directories, path);
            LeaveCriticalSection(&g_dataLock);
            
            if (added) {
                SendMessage(g_listDirs, LB_ADDSTRING, 0, (LPARAM)path);
            } else {
                MessageBoxA(g_hwndMain, 
                    "Failed to add directory.\nList is full or path is too long.", 
                    "Error", MB_ICONERROR);
            }
        }
        CoTaskMemFree(pidl);
    }
}

void OnRemoveDirectory() {
    int sel = (int)SendMessage(g_listDirs, LB_GETCURSEL, 0, 0);
    
    if (sel == LB_ERR) {
        MessageBoxA(g_hwndMain, "Please select a directory to remove.", 
                   "Info", MB_ICONINFORMATION);
        return;
    }
    
    EnterCriticalSection(&g_dataLock);
    if (sel < g_config.directories.count) {
        for (int i = sel; i < g_config.directories.count - 1; i++) {
            strcpy(g_config.directories.paths[i], 
                   g_config.directories.paths[i + 1]);
        }
        g_config.directories.count--;
        SendMessage(g_listDirs, LB_DELETESTRING, sel, 0);
    }
    LeaveCriticalSection(&g_dataLock);
}

void OnAddExclusion() {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwndMain;
    bi.lpszTitle = "Select Folder to Exclude";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            EnterCriticalSection(&g_dataLock);
            bool added = add_exclusion(&g_config.exclusions, path);
            LeaveCriticalSection(&g_dataLock);
            
            if (added) {
                SendMessage(g_listExclusions, LB_ADDSTRING, 0, (LPARAM)path);
            } else {
                MessageBoxA(g_hwndMain, "Failed to add exclusion.", 
                    "Error", MB_ICONERROR);
            }
        }
        CoTaskMemFree(pidl);
    }
}

void OnRemoveExclusion() {
    int sel = (int)SendMessage(g_listExclusions, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    
    EnterCriticalSection(&g_dataLock);
    if (sel < g_config.exclusions.count) {
        for (int i = sel; i < g_config.exclusions.count - 1; i++) {
            strcpy(g_config.exclusions.paths[i], 
                   g_config.exclusions.paths[i + 1]);
        }
        g_config.exclusions.count--;
        SendMessage(g_listExclusions, LB_DELETESTRING, sel, 0);
    }
    LeaveCriticalSection(&g_dataLock);
}

void OnScan() {
    EnterCriticalSection(&g_dataLock);
    int dir_count = g_config.directories.count;
    g_config.directories.include_subdirs = 
        (SendMessage(g_checkSubdirs, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.scan_mode = (ScanMode)SendMessage(g_comboHash, CB_GETCURSEL, 0, 0);
    LeaveCriticalSection(&g_dataLock);
    
    if (dir_count == 0) {
        MessageBoxA(g_hwndMain, "Please add at least one directory!", 
                   "Error", MB_ICONERROR);
        return;
    }
    
    HWND hProgress = GetDlgItem(g_hwndMain, IDC_PROGRESS);
    SendMessage(hProgress, PBM_SETPOS, 0, 0);
    
    LONG style = GetWindowLong(hProgress, GWL_STYLE);
    if (!(style & PBS_MARQUEE)) {
        SetWindowLong(hProgress, GWL_STYLE, style | PBS_MARQUEE);
    }
    SendMessage(hProgress, PBM_SETMARQUEE, TRUE, 30);
    
    EnableWindow(g_btnScan, FALSE);
    EnableWindow(g_btnFind, FALSE);
    
    SetTimer(g_hwndMain, 1, 500, NULL);
    
    if (g_hScanThread) {
        WaitForSingleObject(g_hScanThread, INFINITE);
        CloseHandle(g_hScanThread);
        g_hScanThread = NULL;
    }
    
    g_hScanThread = CreateThread(NULL, 0, ScanThread, NULL, 0, NULL);
    if (!g_hScanThread) {
        MessageBoxA(g_hwndMain, "Failed to create scan thread!", 
                   "Error", MB_ICONERROR);
        EnableWindow(g_btnScan, TRUE);
        EnableWindow(g_btnFind, TRUE);
        KillTimer(g_hwndMain, 1);
    }
}

void OnFind() {
    EnterCriticalSection(&g_dataLock);
    int file_count = g_file_count;
    LeaveCriticalSection(&g_dataLock);
    
    if (file_count == 0) {
        MessageBoxA(g_hwndMain, "Please scan directories first!", 
                   "Error", MB_ICONERROR);
        return;
    }
    
    HWND hProgress = GetDlgItem(g_hwndMain, IDC_PROGRESS);
    SendMessage(hProgress, PBM_SETPOS, 0, 0);
    
    LONG style = GetWindowLong(hProgress, GWL_STYLE);
    if (!(style & PBS_MARQUEE)) {
        SetWindowLong(hProgress, GWL_STYLE, style | PBS_MARQUEE);
    }
    SendMessage(hProgress, PBM_SETMARQUEE, TRUE, 30);
    
    EnableWindow(g_btnScan, FALSE);
    EnableWindow(g_btnFind, FALSE);
    
    if (g_hFindThread) {
        WaitForSingleObject(g_hFindThread, INFINITE);
        CloseHandle(g_hFindThread);
        g_hFindThread = NULL;
    }
    
    g_hFindThread = CreateThread(NULL, 0, FindThread, NULL, 0, NULL);
    if (!g_hFindThread) {
        MessageBoxA(g_hwndMain, "Failed to create find thread!", 
                   "Error", MB_ICONERROR);
        EnableWindow(g_btnScan, TRUE);
        EnableWindow(g_btnFind, TRUE);
    }
}

void OnDeleteFirst() {
    if (MessageBoxA(g_hwndMain, 
        "PERMANENTLY DELETE all duplicates except first file?\n\n"
        "This CANNOT be undone!\n\nContinue?",
        "Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    
    EnterCriticalSection(&g_dataLock);
    int removed = remove_duplicates_keep_first(&g_results);
    free_duplicate_results(&g_results);
    memset(&g_results, 0, sizeof(g_results));
    LeaveCriticalSection(&g_dataLock);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Deleted %d duplicate files\r\n", removed);
    AppendStatus(msg);
    
    ListView_DeleteAllItems(g_listResults);
    
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
}

void OnDeleteByIndex() {
    int index;
    if (!ShowIndexInputDialog(&index)) {
        return;
    }
    
    if (index <= 0) {
        MessageBoxA(g_hwndMain, "Index must be greater than 0", 
                   "Error", MB_ICONERROR);
        return;
    }
    
    EnterCriticalSection(&g_dataLock);
    int total_files = 0;
    for (int i = 0; i < g_results.count; i++) {
        total_files += g_results.groups[i].count;
    }
    
    if (index > total_files) {
        LeaveCriticalSection(&g_dataLock);
        MessageBoxA(g_hwndMain, "Index exceeds total number of files", 
                   "Error", MB_ICONERROR);
        return;
    }
    
    // Find the file at the specified index
    int current_index = 0;
    int group_idx = -1;
    int file_idx = -1;
    
    for (int i = 0; i < g_results.count; i++) {
        for (int j = 0; j < g_results.groups[i].count; j++) {
            current_index++;
            if (current_index == index) {
                group_idx = i;
                file_idx = j;
                break;
            }
        }
        if (group_idx != -1) break;
    }
    
    if (group_idx == -1 || file_idx == -1) {
        LeaveCriticalSection(&g_dataLock);
        MessageBoxA(g_hwndMain, "Failed to locate file at specified index", 
                   "Error", MB_ICONERROR);
        return;
    }
    
    // Confirm deletion
    char confirm_msg[512];
    snprintf(confirm_msg, sizeof(confirm_msg), 
             "Keep file at index %d and delete all other duplicates?\n\n"
             "File to keep: %s\n\n"
             "This CANNOT be undone!\n\nContinue?",
             index, g_results.groups[group_idx].files[file_idx].path);
    
    if (MessageBoxA(g_hwndMain, confirm_msg, 
                   "Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
        LeaveCriticalSection(&g_dataLock);
        return;
    }
    
    // Delete all files except the one at the specified index
    int removed = 0;
    for (int i = 0; i < g_results.count; i++) {
        for (int j = 0; j < g_results.groups[i].count; j++) {
            if (i == group_idx && j == file_idx) {
                // Skip the file to keep
                continue;
            }
            
            if (DeleteFileA(g_results.groups[i].files[j].path)) {
                removed++;
            }
        }
    }
    
    free_duplicate_results(&g_results);
    memset(&g_results, 0, sizeof(g_results));
    LeaveCriticalSection(&g_dataLock);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Deleted %d duplicate files\r\n", removed);
    AppendStatus(msg);
    
    ListView_DeleteAllItems(g_listResults);
    
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
}

void OnMove() {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwndMain;
    bi.lpszTitle = "Select Destination Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            EnterCriticalSection(&g_dataLock);
            int moved = move_duplicates(&g_results, path);
            free_duplicate_results(&g_results);
            memset(&g_results, 0, sizeof(g_results));
            LeaveCriticalSection(&g_dataLock);
            
            char msg[256];
            int len = snprintf(msg, sizeof(msg), "Moved %d files to %s\r\n", moved, path);
            if (len > 0 && len < sizeof(msg)) {
                AppendStatus(msg);
            }
            
            ListView_DeleteAllItems(g_listResults);
            
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
        }
        CoTaskMemFree(pidl);
    }
}

void OnHardLink() {
    if (MessageBoxA(g_hwndMain, 
        "Create hard links to save disk space?\n\n"
        "Keeps first file, replaces duplicates with links.\n"
        "Only works on same drive (NTFS).\n\nContinue?",
        "Create Hard Links", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    
    EnterCriticalSection(&g_dataLock);
    int linked = create_hard_links(&g_results);
    free_duplicate_results(&g_results);
    memset(&g_results, 0, sizeof(g_results));
    LeaveCriticalSection(&g_dataLock);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Created %d hard links\r\n", linked);
    AppendStatus(msg);
    
    if (linked == 0) {
        MessageBoxA(g_hwndMain, 
            "No hard links created.\nFiles may be on different drives.", 
            "Info", MB_ICONINFORMATION);
    }
    
    ListView_DeleteAllItems(g_listResults);
    
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
}

// Handle window resizing
void OnSize(HWND hwnd, UINT state, int cx, int cy) {
    // Update control positions based on new window size
    int margin = 10;
    int buttonWidth = 130;
    int buttonHeight = 28;
    int spacing = 10;
    
    // Calculate positions
    int y_actions = cy - 180;
    int y_status = cy - 80;
    int listview_height = y_actions - 345;
    int progress_width = cx - 20;
    
    // Update progress bar
    SetWindowPos(g_hwndProgress, NULL, margin, 245, progress_width, 20, SWP_NOZORDER);
    
    // Update ListView
    SetWindowPos(g_listResults, NULL, margin, 335, progress_width, listview_height, SWP_NOZORDER);
    
    // Update status edit
    SetWindowPos(g_editStatus, NULL, margin, y_status, progress_width, 80, SWP_NOZORDER);
    
    // Update action buttons
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), NULL, margin, y_actions, buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_DELETE_BY_INDEX), NULL, margin + buttonWidth + spacing, y_actions, buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_MOVE), NULL, margin + (buttonWidth + spacing) * 2, y_actions, buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_HARD_LINK), NULL, margin + (buttonWidth + spacing) * 3, y_actions, buttonWidth, buttonHeight, SWP_NOZORDER);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            InitializeCriticalSection(&g_dataLock);
            
            init_directory_list(&g_config.directories);
            init_exclusion_list(&g_config.exclusions);
            g_config.scan_mode = SCAN_QUICK;
            
            CreateWindowA("STATIC", "FILE DEDUPLICATION SYSTEM", 
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                0, 10, 800, 25, hwnd, NULL, NULL, NULL);
            
            CreateWindowA("STATIC", "Directories to Scan:", 
                WS_VISIBLE | WS_CHILD,
                10, 45, 200, 20, hwnd, NULL, NULL, NULL);
            
            g_listDirs = CreateWindowA("LISTBOX", NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                10, 65, 280, 100, hwnd, (HMENU)IDC_LISTBOX_DIRS, NULL, NULL);
            
            CreateWindowA("BUTTON", "Add Directory", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                300, 65, 110, 25, hwnd, (HMENU)IDC_BTN_ADD_DIR, NULL, NULL);
            
            CreateWindowA("BUTTON", "Remove", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                300, 95, 110, 25, hwnd, (HMENU)IDC_BTN_REMOVE_DIR, NULL, NULL);
            
            CreateWindowA("STATIC", "Folders to Exclude:", 
                WS_VISIBLE | WS_CHILD,
                420, 45, 200, 20, hwnd, NULL, NULL, NULL);
            
            g_listExclusions = CreateWindowA("LISTBOX", NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                420, 65, 280, 100, hwnd, (HMENU)IDC_LISTBOX_EXCLUSIONS, NULL, NULL);
            
            CreateWindowA("BUTTON", "Add Exclusion", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                710, 65, 110, 25, hwnd, (HMENU)IDC_BTN_ADD_EXCLUSION, NULL, NULL);
            
            CreateWindowA("BUTTON", "Remove", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                710, 95, 110, 25, hwnd, (HMENU)IDC_BTN_REMOVE_EXCLUSION, NULL, NULL);
            
            g_checkSubdirs = CreateWindowA("BUTTON", "Include Subdirectories", 
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                10, 175, 180, 20, hwnd, (HMENU)IDC_CHECK_SUBDIRS, NULL, NULL);
            SendMessage(g_checkSubdirs, BM_SETCHECK, BST_CHECKED, 0);
            
            CreateWindowA("STATIC", "Hash Algorithm:", 
                WS_VISIBLE | WS_CHILD,
                200, 177, 100, 20, hwnd, NULL, NULL, NULL);
            
            g_comboHash = CreateWindowA("COMBOBOX", NULL,
                WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                300, 175, 150, 100, hwnd, (HMENU)IDC_COMBO_HASH, NULL, NULL);
            
            SendMessageA(g_comboHash, CB_ADDSTRING, 0, 
                (LPARAM)"FNV-1a (1MB)");
            SendMessageA(g_comboHash, CB_ADDSTRING, 0, 
                (LPARAM)"FNV-1a (Full)");
            SendMessage(g_comboHash, CB_SETCURSEL, 0, 0);
            
            g_btnScan = CreateWindowA("BUTTON", "Scan Directories", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                10, 205, 140, 30, hwnd, (HMENU)IDC_BTN_SCAN, NULL, NULL);
            
            g_btnFind = CreateWindowA("BUTTON", "Find Duplicates", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                160, 205, 140, 30, hwnd, (HMENU)IDC_BTN_FIND, NULL, NULL);
            
            g_hwndProgress = CreateWindowA(PROGRESS_CLASSA, NULL,
                WS_VISIBLE | WS_CHILD,
                10, 245, 810, 20, hwnd, (HMENU)IDC_PROGRESS, NULL, NULL);
            SendMessage(g_hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            
            CreateWindowA("STATIC", "Actions:", 
                WS_VISIBLE | WS_CHILD,
                10, 275, 150, 20, hwnd, NULL, NULL, NULL);
            
            CreateWindowA("BUTTON", "Delete (Keep First)", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                10, 295, 130, 28, hwnd, (HMENU)IDC_BTN_DELETE_FIRST, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), FALSE);
            
            CreateWindowA("BUTTON", "Delete by Index", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                150, 295, 130, 28, hwnd, (HMENU)IDC_BTN_DELETE_BY_INDEX, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_BY_INDEX), FALSE);
            
            CreateWindowA("BUTTON", "Move to Folder", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                290, 295, 130, 28, hwnd, (HMENU)IDC_BTN_MOVE, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE), FALSE);
            
            CreateWindowA("BUTTON", "Create Hard Links", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                430, 295, 130, 28, hwnd, (HMENU)IDC_BTN_HARD_LINK, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_HARD_LINK), FALSE);
            
            g_listResults = CreateWindowA(WC_LISTVIEWA, NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                10, 335, 810, 180, hwnd, (HMENU)IDC_LISTVIEW_RESULTS, NULL, NULL);
            
            // Use SendMessage instead of ListView_SetExtendedListViewStyle
            SendMessage(g_listResults, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, 
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            
            LVCOLUMNA lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.cx = 100;
            lvc.pszText = "Group";
            ListView_InsertColumn(g_listResults, 0, &lvc);
            
            lvc.cx = 80;
            lvc.pszText = "Size";
            ListView_InsertColumn(g_listResults, 1, &lvc);
            
            lvc.cx = 150;
            lvc.pszText = "Filename";
            ListView_InsertColumn(g_listResults, 2, &lvc);
            
            lvc.cx = 450;
            lvc.pszText = "Full Path";
            ListView_InsertColumn(g_listResults, 3, &lvc);
            
            g_editStatus = CreateWindowA("EDIT", 
                "Ready. Add directories and click 'Scan Directories'.\r\n",
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | 
                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                10, 525, 810, 100, hwnd, (HMENU)IDC_EDIT_STATUS, NULL, NULL);
            
            break;
        }
        
        case WM_SIZE:
            OnSize(hwnd, (UINT)wParam, LOWORD(lParam), HIWORD(lParam));
            return 0;
        
        case WM_TIMER:
            if (wParam == 1) UpdateProgressBar();
            break;
        
        case WM_SCAN_COMPLETE:
            KillTimer(hwnd, 1);
            EnableWindow(g_btnScan, TRUE);
            EnableWindow(g_btnFind, TRUE);
            {
                HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
                SendMessage(hProgress, PBM_SETMARQUEE, FALSE, 0);
                SendMessage(hProgress, PBM_SETPOS, 100, 0);
            }
            break;
            
        case WM_FIND_COMPLETE:
            KillTimer(hwnd, 1);
            EnableWindow(g_btnScan, TRUE);
            EnableWindow(g_btnFind, TRUE);
            {
                HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
                SendMessage(hProgress, PBM_SETMARQUEE, FALSE, 0);
                SendMessage(hProgress, PBM_SETPOS, 100, 0);
            }
            
            EnterCriticalSection(&g_dataLock);
            bool has_results = (g_results.count > 0);
            LeaveCriticalSection(&g_dataLock);
            
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), has_results);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE), has_results);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_HARD_LINK), has_results);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_BY_INDEX), has_results);
            break;
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_DIR: OnAddDirectory(); break;
                case IDC_BTN_REMOVE_DIR: OnRemoveDirectory(); break;
                case IDC_BTN_ADD_EXCLUSION: OnAddExclusion(); break;
                case IDC_BTN_REMOVE_EXCLUSION: OnRemoveExclusion(); break;
                case IDC_BTN_SCAN: OnScan(); break;
                case IDC_BTN_FIND: OnFind(); break;
                case IDC_BTN_DELETE_FIRST: OnDeleteFirst(); break;
                case IDC_BTN_DELETE_BY_INDEX: OnDeleteByIndex(); break;
                case IDC_BTN_MOVE: OnMove(); break;
                case IDC_BTN_HARD_LINK: OnHardLink(); break;
            }
            break;
        
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        
        case WM_DESTROY:
            if (g_hScanThread) {
                WaitForSingleObject(g_hScanThread, 5000);
                CloseHandle(g_hScanThread);
            }
            if (g_hFindThread) {
                WaitForSingleObject(g_hFindThread, 5000);
                CloseHandle(g_hFindThread);
            }
            
            KillTimer(hwnd, 1);
            
            EnterCriticalSection(&g_dataLock);
            if (g_files) free(g_files);
            free_duplicate_results(&g_results);
            LeaveCriticalSection(&g_dataLock);
            
            DeleteCriticalSection(&g_dataLock);
            PostQuitMessage(0);
            break;
        
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    // Initialize COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxA(NULL, "COM Initialization Failed!", "Error", MB_ICONERROR);
        return 0;
    }
    
    InitCommonControls();
    
    // Register window class
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "FileDedupClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_ICONERROR);
        CoUninitialize();
        return 0;
    }
    
    // Create main window
    HWND hwnd = CreateWindowA("FileDedupClass", 
        "File Deduplication System",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_THICKFRAME | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 700,
        NULL, NULL, hInstance, NULL);
    
    if (!hwnd) {
        MessageBoxA(NULL, "Window Creation Failed!", "Error", MB_ICONERROR);
        CoUninitialize();
        return 0;
    }
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    CoUninitialize();
    return (int)msg.wParam;
}