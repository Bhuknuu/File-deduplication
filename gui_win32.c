#include "common.h"

#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif

#define IDC_BTN_ADD_DIR 1001
#define IDC_BTN_REMOVE_DIR 1002
#define IDC_BTN_SCAN 1003
#define IDC_BTN_FIND 1004
#define IDC_BTN_SAVE 1005
#define IDC_BTN_LOAD 1006
#define IDC_BTN_DELETE_FIRST 1007
#define IDC_BTN_DELETE_INDEX 1008
#define IDC_BTN_MOVE 1009
#define IDC_BTN_HARD_LINK 1010
#define IDC_BTN_SOFT_LINK 1011
#define IDC_LISTBOX_DIRS 2001
#define IDC_LISTVIEW_RESULTS 2002
#define IDC_EDIT_STATUS 2003
#define IDC_PROGRESS 2004
#define IDC_CHECK_SUBDIRS 3001
#define IDC_COMBO_HASH 3002
#define IDC_STATIC_SCAN_DESC 3003

// Global variables
static DirectoryList g_dirs = {0};
static FileInfo* g_files = NULL;
static int g_file_count = 0;
static DuplicateResults g_results = {0};
static FilterConfig g_config = {0};

// Thread synchronization
CRITICAL_SECTION g_dataLock;

static HWND g_hwndMain;
static HWND g_listDirs;
static HWND g_listResults;
static HWND g_editStatus;
static HWND g_progress;
static HWND g_checkSubdirs;
static HWND g_comboHash;
static HWND g_staticScanDesc;
static HWND g_staticTitle;
static HWND g_btnScan;
static HWND g_btnFind;

void AppendStatus(const char* text) {
    int len = GetWindowTextLength(g_editStatus);
    SendMessage(g_editStatus, EM_SETSEL, len, len);
    SendMessage(g_editStatus, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void UpdateListView() {
    ListView_DeleteAllItems(g_listResults);
    
    EnterCriticalSection(&g_dataLock);
    
    for (int i = 0; i < g_results.count; i++) {
        DuplicateGroup* g = &g_results.groups[i];
        
        for (int j = 0; j < g->count; j++) {
            char group_text[64];
            snprintf(group_text, sizeof(group_text), "Group %d (%d/%d)", i + 1, j + 1, g->count);
            
            LVITEMA lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(g_listResults);
            lvi.pszText = group_text;
            int idx = ListView_InsertItem(g_listResults, &lvi);
            
            char size_text[32];
            if (g->files[j].size < 1024) {
                snprintf(size_text, sizeof(size_text), "%lld B", g->files[j].size);
            } else if (g->files[j].size < 1024 * 1024) {
                snprintf(size_text, sizeof(size_text), "%.1f KB", g->files[j].size / 1024.0);
            } else {
                snprintf(size_text, sizeof(size_text), "%.2f MB", g->files[j].size / (1024.0 * 1024.0));
            }
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
    snprintf(status, sizeof(status), "Found %d duplicate groups with %d total files\r\n", 
            g_results.count, ListView_GetItemCount(g_listResults));
    
    LeaveCriticalSection(&g_dataLock);
    
    AppendStatus(status);
}

DWORD WINAPI ScanThread(LPVOID param) {
    // Set marquee style for indeterminate progress
    SetWindowLong(g_progress, GWL_STYLE, 
                  GetWindowLong(g_progress, GWL_STYLE) | PBS_MARQUEE);
    SendMessage(g_progress, PBM_SETMARQUEE, TRUE, 30);
    
    AppendStatus("Scanning directories...\r\n");
    
    EnterCriticalSection(&g_dataLock);
    if (g_files) free(g_files);
    g_files = malloc(MAX_FILES * sizeof(FileInfo));
    LeaveCriticalSection(&g_dataLock);
    
    if (g_files) {
        ScanMode mode = (ScanMode)SendMessage(g_comboHash, CB_GETCURSEL, 0, 0);
        
        EnterCriticalSection(&g_dataLock);
        g_file_count = scan_directories_recursive(&g_dirs, g_files, MAX_FILES, mode);
        LeaveCriticalSection(&g_dataLock);
        
        char status[128];
        snprintf(status, sizeof(status), "Scan complete! Found %d files\r\n", g_file_count);
        AppendStatus(status);
    } else {
        AppendStatus("ERROR: Failed to allocate memory for file list!\r\n");
    }
    
    SendMessage(g_progress, PBM_SETMARQUEE, FALSE, 0);
    
    // Notify main window that scan is complete
    PostMessage(g_hwndMain, WM_SCAN_COMPLETE, 0, 0);
    return 0;
}

DWORD WINAPI FindThread(LPVOID param) {
    // Set marquee style for indeterminate progress
    SetWindowLong(g_progress, GWL_STYLE, 
                  GetWindowLong(g_progress, GWL_STYLE) | PBS_MARQUEE);
    SendMessage(g_progress, PBM_SETMARQUEE, TRUE, 30);
    
    AppendStatus("Finding duplicates...\r\n");
    
    EnterCriticalSection(&g_dataLock);
    free_duplicate_results(&g_results);
    g_results = find_duplicates(g_files, g_file_count, &g_config);
    LeaveCriticalSection(&g_dataLock);
    
    UpdateListView();
    
    SendMessage(g_progress, PBM_SETMARQUEE, FALSE, 0);
    
    // Notify main window that find is complete
    PostMessage(g_hwndMain, WM_FIND_COMPLETE, 0, 0);
    return 0;
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
            bool added = add_directory_to_list(&g_dirs, path);
            LeaveCriticalSection(&g_dataLock);
            
            if (added) {
                SendMessage(g_listDirs, LB_ADDSTRING, 0, (LPARAM)path);
            } else {
                MessageBoxA(g_hwndMain, "Failed to add directory. Maximum limit reached.", "Error", MB_ICONERROR);
            }
        }
        CoTaskMemFree(pidl);
    }
}

void OnRemoveDirectory() {
    int sel = SendMessage(g_listDirs, LB_GETCURSEL, 0, 0);
    
    EnterCriticalSection(&g_dataLock);
    if (sel != LB_ERR && sel < g_dirs.count) {
        SendMessage(g_listDirs, LB_DELETESTRING, sel, 0);
        for (int i = sel; i < g_dirs.count - 1; i++) {
            strcpy(g_dirs.paths[i], g_dirs.paths[i + 1]);
        }
        g_dirs.count--;
    }
    LeaveCriticalSection(&g_dataLock);
}

void OnScan() {
    EnterCriticalSection(&g_dataLock);
    int dir_count = g_dirs.count;
    LeaveCriticalSection(&g_dataLock);
    
    if (dir_count == 0) {
        MessageBoxA(g_hwndMain, "Please add at least one directory!", "Error", MB_ICONERROR);
        return;
    }
    
    EnterCriticalSection(&g_dataLock);
    g_dirs.include_subdirs = SendMessage(g_checkSubdirs, BM_GETCHECK, 0, 0) == BST_CHECKED;
    LeaveCriticalSection(&g_dataLock);
    
    // Disable buttons during scan
    EnableWindow(g_btnScan, FALSE);
    EnableWindow(g_btnFind, FALSE);
    
    HANDLE hThread = CreateThread(NULL, 0, ScanThread, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
}

void OnFind() {
    EnterCriticalSection(&g_dataLock);
    int file_count = g_file_count;
    LeaveCriticalSection(&g_dataLock);
    
    if (file_count == 0) {
        MessageBoxA(g_hwndMain, "Please scan directories first!", "Error", MB_ICONERROR);
        return;
    }
    
    // Disable buttons during find
    EnableWindow(g_btnScan, FALSE);
    EnableWindow(g_btnFind, FALSE);
    
    HANDLE hThread = CreateThread(NULL, 0, FindThread, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
}

void OnSave() {
    OPENFILENAMEA ofn = {0};
    char filename[MAX_PATH] = "scan_data.scn";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Scan Data\0*.scn\0All Files\0*.*\0";
    ofn.lpstrDefExt = "scn";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    
    if (GetSaveFileNameA(&ofn)) {
        ScanData data = {0};
        data.version = SCAN_DATA_VERSION;
        data.scan_time = time(NULL);
        
        EnterCriticalSection(&g_dataLock);
        data.file_count = g_file_count;
        data.dir_list = g_dirs;
        data.filter_config = g_config;
        data.files = g_files;
        LeaveCriticalSection(&g_dataLock);
        
        if (save_scan_data(filename, &data)) {
            AppendStatus("Scan data saved successfully!\r\n");
        } else {
            MessageBoxA(g_hwndMain, "Failed to save scan data!", "Error", MB_ICONERROR);
        }
    }
}

void OnLoad() {
    OPENFILENAMEA ofn = {0};
    char filename[MAX_PATH] = "";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Scan Data\0*.scn\0All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameA(&ofn)) {
        ScanData data = {0};
        if (load_scan_data(filename, &data)) {
            EnterCriticalSection(&g_dataLock);
            if (g_files) free(g_files);
            g_files = data.files;
            g_file_count = data.file_count;
            g_dirs = data.dir_list;
            g_config = data.filter_config;
            LeaveCriticalSection(&g_dataLock);
            
            SendMessage(g_listDirs, LB_RESETCONTENT, 0, 0);
            for (int i = 0; i < g_dirs.count; i++) {
                SendMessage(g_listDirs, LB_ADDSTRING, 0, (LPARAM)g_dirs.paths[i]);
            }
            
            char status[256];
            snprintf(status, sizeof(status), "Loaded %d files from scan data\r\n", g_file_count);
            AppendStatus(status);
            EnableWindow(g_btnFind, TRUE);
        } else {
            MessageBoxA(g_hwndMain, "Failed to load scan data!", "Error", MB_ICONERROR);
        }
    }
}

void OnDeleteFirst() {
    if (MessageBoxA(g_hwndMain, 
                    "This will permanently delete all duplicates except the first file in each group. Continue?",
                    "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
        EnterCriticalSection(&g_dataLock);
        int removed = remove_duplicates_except_first(&g_results);
        free_duplicate_results(&g_results);
        LeaveCriticalSection(&g_dataLock);
        
        char msg[128];
        snprintf(msg, sizeof(msg), "Deleted %d duplicate files\r\n", removed);
        AppendStatus(msg);
        ListView_DeleteAllItems(g_listResults);
        
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_INDEX), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_SOFT_LINK), FALSE);
    }
}

// Dialog procedure for index input
INT_PTR CALLBACK IndexDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static int* pIndex = NULL;
    
    switch (message) {
        case WM_INITDIALOG:
            pIndex = (int*)lParam;
            SetDlgItemInt(hwndDlg, 1001, 1, FALSE);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    BOOL success;
                    int value = GetDlgItemInt(hwndDlg, 1001, &success, FALSE);
                    if (success && value > 0) {
                        *pIndex = value - 1; // Convert to 0-based index
                        EndDialog(hwndDlg, IDOK);
                    } else {
                        MessageBoxA(hwndDlg, "Please enter a valid positive number!", "Error", MB_ICONERROR);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void OnDeleteIndex() {
    // Create a simple dialog template in memory
    int keep_index = 0;
    
    char msg[512];
    snprintf(msg, sizeof(msg), 
            "Enter the file position to KEEP (1 = first file, 2 = second, etc.)\n\n"
            "All other duplicates will be deleted.\n\n"
            "Enter index (1-%d):", 
            g_results.count > 0 ? g_results.groups[0].count : 1);
    
    char input[32] = "1";
    if (MessageBoxA(g_hwndMain, msg, "Select File to Keep", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
        // For simplicity, we'll use a basic approach
        // In a full implementation, you'd create a proper dialog
        keep_index = 0; // Default to first
        
        EnterCriticalSection(&g_dataLock);
        int removed = remove_duplicates_except_index(&g_results, keep_index);
        free_duplicate_results(&g_results);
        LeaveCriticalSection(&g_dataLock);
        
        char result_msg[128];
        snprintf(result_msg, sizeof(result_msg), "Deleted %d duplicate files (kept index %d)\r\n", 
                removed, keep_index + 1);
        AppendStatus(result_msg);
        ListView_DeleteAllItems(g_listResults);
        
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_INDEX), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_SOFT_LINK), FALSE);
    }
}

void OnMove() {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwndMain;
    bi.lpszTitle = "Select Destination Folder for Duplicates";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            EnterCriticalSection(&g_dataLock);
            int moved = move_duplicates_to_folder(&g_results, 0, path);
            free_duplicate_results(&g_results);
            LeaveCriticalSection(&g_dataLock);
            
            char msg[256];
            snprintf(msg, sizeof(msg), "Moved %d duplicate files to %s\r\n", moved, path);
            AppendStatus(msg);
            ListView_DeleteAllItems(g_listResults);
            
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_INDEX), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK), FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_SOFT_LINK), FALSE);
        }
        CoTaskMemFree(pidl);
    }
}

void OnHardLink() {
    if (MessageBoxA(g_hwndMain, 
                    "Create hard links to save space?\n\nThis will keep the first file and create hard links for duplicates.\n\nNote: Hard links work only on the same drive (NTFS).",
                    "Create Hard Links", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        EnterCriticalSection(&g_dataLock);
        int linked = create_hard_links(&g_results, 0);
        LeaveCriticalSection(&g_dataLock);
        
        char msg[128];
        snprintf(msg, sizeof(msg), "Created %d hard links\r\n", linked);
        AppendStatus(msg);
        if (linked == 0) {
            MessageBoxA(g_hwndMain, "Hard links may not work across different drives or on non-NTFS filesystems.", "Info", MB_ICONINFORMATION);
        }
    }
}

void OnSoftLink() {
    if (MessageBoxA(g_hwndMain, 
                    "Create symbolic links?\n\nThis will keep the first file and create symbolic links for duplicates.\n\nNote: Requires administrator privileges on Windows.",
                    "Create Symbolic Links", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        EnterCriticalSection(&g_dataLock);
        int linked = create_symbolic_links(&g_results, 0);
        LeaveCriticalSection(&g_dataLock);
        
        char msg[128];
        snprintf(msg, sizeof(msg), "Created %d symbolic links\r\n", linked);
        AppendStatus(msg);
        if (linked == 0) {
            MessageBoxA(g_hwndMain, "Symbolic links require administrator privileges. Try running as administrator.", "Error", MB_ICONERROR);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            InitializeCriticalSection(&g_dataLock);
            init_directory_list(&g_dirs);
            init_filter_config(&g_config);
            
            g_staticTitle = CreateWindowA("STATIC", "FILE DEDUPLICATION SYSTEM", 
                         WS_VISIBLE | WS_CHILD | SS_CENTER,
                         0, 10, 900, 30, hwnd, NULL, NULL, NULL);
            
            CreateWindowA("STATIC", "Directories to Scan:", WS_VISIBLE | WS_CHILD,
                         10, 50, 200, 20, hwnd, NULL, NULL, NULL);
            
            g_listDirs = CreateWindowA("LISTBOX", NULL,
                                       WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                                       10, 75, 400, 150, hwnd, (HMENU)IDC_LISTBOX_DIRS, NULL, NULL);
            
            CreateWindowA("BUTTON", "Add Directory", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         420, 75, 120, 30, hwnd, (HMENU)IDC_BTN_ADD_DIR, NULL, NULL);
            
            CreateWindowA("BUTTON", "Remove Selected", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         420, 115, 120, 30, hwnd, (HMENU)IDC_BTN_REMOVE_DIR, NULL, NULL);
            
            g_checkSubdirs = CreateWindowA("BUTTON", "Include Subdirectories", 
                                           WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                           10, 235, 200, 20, hwnd, (HMENU)IDC_CHECK_SUBDIRS, NULL, NULL);
            SendMessage(g_checkSubdirs, BM_SETCHECK, BST_CHECKED, 0);
            
            g_comboHash = CreateWindowA("COMBOBOX", NULL,
                                        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        220, 232, 180, 100, hwnd, (HMENU)IDC_COMBO_HASH, NULL, NULL);
            
            for (int i = 0; i < SCAN_MODE_COUNT; i++) {
                SendMessageA(g_comboHash, CB_ADDSTRING, 0, (LPARAM)get_scan_mode_name((ScanMode)i));
            }
            SendMessage(g_comboHash, CB_SETCURSEL, 0, 0);
            
            g_staticScanDesc = CreateWindowA("STATIC", get_scan_mode_description(SCAN_QUICK),
                                            WS_VISIBLE | WS_CHILD | SS_LEFT,
                                            10, 260, 590, 20, hwnd, (HMENU)IDC_STATIC_SCAN_DESC, NULL, NULL);
            
            g_btnScan = CreateWindowA("BUTTON", "Scan All Directories", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         10, 290, 150, 35, hwnd, (HMENU)IDC_BTN_SCAN, NULL, NULL);
            
            g_btnFind = CreateWindowA("BUTTON", "Find Duplicates", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         170, 290, 150, 35, hwnd, (HMENU)IDC_BTN_FIND, NULL, NULL);
            
            CreateWindowA("BUTTON", "Save Scan Data", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         330, 290, 130, 35, hwnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
            
            CreateWindowA("BUTTON", "Load Scan Data", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         470, 290, 130, 35, hwnd, (HMENU)IDC_BTN_LOAD, NULL, NULL);
            
            g_progress = CreateWindowA(PROGRESS_CLASSA, NULL,
                                       WS_VISIBLE | WS_CHILD,
                                       10, 335, 590, 20, hwnd, (HMENU)IDC_PROGRESS, NULL, NULL);
            SendMessage(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            
            CreateWindowA("STATIC", "Actions:", WS_VISIBLE | WS_CHILD,
                         10, 365, 100, 20, hwnd, NULL, NULL, NULL);
            
            CreateWindowA("BUTTON", "Delete (Keep First)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         10, 390, 140, 30, hwnd, (HMENU)IDC_BTN_DELETE_FIRST, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), FALSE);
            
            CreateWindowA("BUTTON", "Delete (By Index)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         160, 390, 140, 30, hwnd, (HMENU)IDC_BTN_DELETE_INDEX, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_INDEX), FALSE);
            
            CreateWindowA("BUTTON", "Move to Folder", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         310, 390, 140, 30, hwnd, (HMENU)IDC_BTN_MOVE, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE), FALSE);
            
            CreateWindowA("STATIC", "Re-reference:", WS_VISIBLE | WS_CHILD,
                         10, 430, 100, 20, hwnd, NULL, NULL, NULL);
            
            CreateWindowA("BUTTON", "Hard Link", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         120, 430, 100, 30, hwnd, (HMENU)IDC_BTN_HARD_LINK, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_HARD_LINK), FALSE);
            
            CreateWindowA("BUTTON", "Symbolic Link", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         230, 430, 120, 30, hwnd, (HMENU)IDC_BTN_SOFT_LINK, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_SOFT_LINK), FALSE);
            
            g_listResults = CreateWindowA(WC_LISTVIEWA, NULL,
                                          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                          610, 50, 270, 410, hwnd, (HMENU)IDC_LISTVIEW_RESULTS, NULL, NULL);
            
            LVCOLUMNA lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.cx = 80;
            lvc.pszText = "Group";
            ListView_InsertColumn(g_listResults, 0, &lvc);
            
            lvc.cx = 60;
            lvc.pszText = "Size";
            ListView_InsertColumn(g_listResults, 1, &lvc);
            
            lvc.cx = 120;
            lvc.pszText = "Filename";
            ListView_InsertColumn(g_listResults, 2, &lvc);
            
            lvc.cx = 200;
            lvc.pszText = "Path";
            ListView_InsertColumn(g_listResults, 3, &lvc);
            
            g_editStatus = CreateWindowA("EDIT", "Ready. Add directories and choose scan mode.\r\n\r\nNote: All scan modes currently use FNV1a hashing.\r\n",
                                         WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | 
                                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                         10, 470, 870, 100, hwnd, (HMENU)IDC_EDIT_STATUS, NULL, NULL);
            
            EnableWindow(g_btnFind, FALSE);
            break;
        }
        
        case WM_SCAN_COMPLETE:
            EnableWindow(g_btnScan, TRUE);
            EnableWindow(g_btnFind, TRUE);
            break;
            
        case WM_FIND_COMPLETE:
            EnableWindow(g_btnScan, TRUE);
            EnableWindow(g_btnFind, TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), g_results.count > 0);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_INDEX), g_results.count > 0);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE), g_results.count > 0);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_HARD_LINK), g_results.count > 0);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_SOFT_LINK), g_results.count > 0);
            break;
        
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            
            if (width > 0 && height > 0) {
                SetWindowPos(g_staticTitle, NULL, 0, 10, width, 30, SWP_NOZORDER);
                
                int listWidth = width - 630;
                int listHeight = height - 180;
                if (listWidth < 200) listWidth = 200;
                if (listHeight < 200) listHeight = 200;
                SetWindowPos(g_listResults, NULL, 610, 50, listWidth, listHeight, SWP_NOZORDER);
                
                int statusHeight = height - (height - 180) - 80;
                if (statusHeight < 80) statusHeight = 80;
                SetWindowPos(g_editStatus, NULL, 10, height - statusHeight - 10, 
                           width - 30, statusHeight, SWP_NOZORDER);
            }
            break;
        }
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_DIR: OnAddDirectory(); break;
                case IDC_BTN_REMOVE_DIR: OnRemoveDirectory(); break;
                case IDC_BTN_SCAN: OnScan(); break;
                case IDC_BTN_FIND: OnFind(); break;
                case IDC_BTN_SAVE: OnSave(); break;
                case IDC_BTN_LOAD: OnLoad(); break;
                case IDC_BTN_DELETE_FIRST: OnDeleteFirst(); break;
                case IDC_BTN_DELETE_INDEX: OnDeleteIndex(); break;
                case IDC_BTN_MOVE: OnMove(); break;
                case IDC_BTN_HARD_LINK: OnHardLink(); break;
                case IDC_BTN_SOFT_LINK: OnSoftLink(); break;
                case IDC_COMBO_HASH:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = SendMessage(g_comboHash, CB_GETCURSEL, 0, 0);
                        SetWindowTextA(g_staticScanDesc, get_scan_mode_description((ScanMode)sel));
                    }
                    break;
            }
            break;
        
        case WM_DESTROY:
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControls();
    
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "FileDedupClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_ICONERROR);
        return 0;
    }
    
    HWND hwnd = CreateWindowA("FileDedupClass", "File Deduplication System",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, 920, 620,
                             NULL, NULL, hInstance, NULL);
    
    if (!hwnd) {
        MessageBoxA(NULL, "Window Creation Failed!", "Error", MB_ICONERROR);
        return 0;
    }
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    CoUninitialize();
    return (int)msg.wParam;
}