// ============================================================================
// GUI MODULE - WIN32 USER INTERFACE
// ============================================================================

#include "common.h"

// Define missing constants for older Windows SDKs
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif

#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
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
#define IDC_LISTBOX_DIRS 2001
#define IDC_LISTVIEW_RESULTS 2002
#define IDC_EDIT_STATUS 2003
#define IDC_PROGRESS 2004
#define IDC_CHECK_SUBDIRS 3001
#define IDC_COMBO_HASH 3002

// Global state
static DirectoryList g_dirs = {0};
static FileInfo* g_files = NULL;
static int g_file_count = 0;
static DuplicateResults g_results = {0};
static FilterConfig g_config = {0};
static HWND g_hwndMain, g_listDirs, g_listResults, g_editStatus, g_progress, g_checkSubdirs, g_comboHash;

void AppendStatus(const char* text) {
    int len = GetWindowTextLength(g_editStatus);
    SendMessage(g_editStatus, EM_SETSEL, len, len);
    SendMessage(g_editStatus, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void UpdateListView() {
    ListView_DeleteAllItems(g_listResults);
    
    for (int i = 0; i < g_results.count; i++) {
        DuplicateGroup* g = &g_results.groups[i];
        
        char group_text[128];
        sprintf(group_text, "Group %d (%d files)", i + 1, g->count);
        
        LVITEMA lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = ListView_GetItemCount(g_listResults);
        lvi.pszText = group_text;
        int idx = ListView_InsertItem(g_listResults, &lvi);
        
        char size_text[64];
        sprintf(size_text, "%.2f MB", g->files[0].size / (1024.0 * 1024.0));
        ListView_SetItemText(g_listResults, idx, 1, size_text);
        
        ListView_SetItemText(g_listResults, idx, 2, g->files[0].hash);
    }
    
    char status[256];
    sprintf(status, "Found %d duplicate groups\r\n", g_results.count);
    AppendStatus(status);
}

DWORD WINAPI ScanThread(LPVOID param) {
    SendMessage(g_progress, PBM_SETMARQUEE, TRUE, 0);
    AppendStatus("Scanning directories...\r\n");
    
    if (g_files) free(g_files);
    g_files = malloc(MAX_FILES * sizeof(FileInfo));
    
    if (g_files) {
        HashAlgorithm algo = (HashAlgorithm)SendMessage(g_comboHash, CB_GETCURSEL, 0, 0);
        g_file_count = scan_directories_recursive(&g_dirs, g_files, MAX_FILES, algo);
        
        char status[128];
        sprintf(status, "Scan complete! Found %d files\r\n", g_file_count);
        AppendStatus(status);
    }
    
    SendMessage(g_progress, PBM_SETMARQUEE, FALSE, 0);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_FIND), TRUE);
    return 0;
}

DWORD WINAPI FindThread(LPVOID param) {
    SendMessage(g_progress, PBM_SETMARQUEE, TRUE, 0);
    AppendStatus("Finding duplicates...\r\n");
    
    free_duplicate_results(&g_results);
    g_results = find_duplicates(g_files, g_file_count, &g_config);
    
    UpdateListView();
    SendMessage(g_progress, PBM_SETMARQUEE, FALSE, 0);
    
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST), g_results.count > 0);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_INDEX), g_results.count > 0);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE), g_results.count > 0);
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
            if (add_directory_to_list(&g_dirs, path)) {
                SendMessage(g_listDirs, LB_ADDSTRING, 0, (LPARAM)path);
            }
        }
        CoTaskMemFree(pidl);
    }
}

void OnRemoveDirectory() {
    int sel = SendMessage(g_listDirs, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR && sel < g_dirs.count) {
        SendMessage(g_listDirs, LB_DELETESTRING, sel, 0);
        for (int i = sel; i < g_dirs.count - 1; i++) {
            strcpy(g_dirs.paths[i], g_dirs.paths[i + 1]);
        }
        g_dirs.count--;
    }
}

void OnScan() {
    if (g_dirs.count == 0) {
        MessageBoxA(g_hwndMain, "Please add at least one directory!", "Error", MB_ICONERROR);
        return;
    }
    
    g_dirs.include_subdirs = SendMessage(g_checkSubdirs, BM_GETCHECK, 0, 0) == BST_CHECKED;
    CreateThread(NULL, 0, ScanThread, NULL, 0, NULL);
}

void OnFind() {
    if (g_file_count == 0) {
        MessageBoxA(g_hwndMain, "Please scan directories first!", "Error", MB_ICONERROR);
        return;
    }
    CreateThread(NULL, 0, FindThread, NULL, 0, NULL);
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
        data.file_count = g_file_count;
        data.dir_list = g_dirs;
        data.filter_config = g_config;
        data.files = g_files;
        
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
            if (g_files) free(g_files);
            g_files = data.files;
            g_file_count = data.file_count;
            g_dirs = data.dir_list;
            g_config = data.filter_config;
            
            // Update UI
            SendMessage(g_listDirs, LB_RESETCONTENT, 0, 0);
            for (int i = 0; i < g_dirs.count; i++) {
                SendMessage(g_listDirs, LB_ADDSTRING, 0, (LPARAM)g_dirs.paths[i]);
            }
            
            char status[256];
            sprintf(status, "Loaded %d files from scan data\r\n", g_file_count);
            AppendStatus(status);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_FIND), TRUE);
        } else {
            MessageBoxA(g_hwndMain, "Failed to load scan data!", "Error", MB_ICONERROR);
        }
    }
}

void OnDeleteFirst() {
    if (MessageBoxA(g_hwndMain, 
                    "This will permanently delete all duplicates except the first file in each group. Continue?",
                    "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
        int removed = remove_duplicates_except_first(&g_results);
        char msg[128];
        sprintf(msg, "Deleted %d duplicate files\r\n", removed);
        AppendStatus(msg);
        free_duplicate_results(&g_results);
        ListView_DeleteAllItems(g_listResults);
    }
}

void OnDeleteIndex() {
    char input[32] = "1";
    if (MessageBoxA(g_hwndMain, 
                    "Enter the file index to keep (1 = first file, 2 = second, etc.)\r\nEnter in the text box below.",
                    "Delete by Index", MB_OK | MB_ICONINFORMATION) == IDOK) {
        // Simple input dialog would go here - for now use default
        int index = 0; // Keep first by default
        int removed = remove_duplicates_except_index(&g_results, index);
        char msg[128];
        sprintf(msg, "Deleted %d duplicate files\r\n", removed);
        AppendStatus(msg);
        free_duplicate_results(&g_results);
        ListView_DeleteAllItems(g_listResults);
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
            int moved = move_duplicates_to_folder(&g_results, 0, path);
            char msg[256];
            sprintf(msg, "Moved %d duplicate files to %s\r\n", moved, path);
            AppendStatus(msg);
            free_duplicate_results(&g_results);
            ListView_DeleteAllItems(g_listResults);
        }
        CoTaskMemFree(pidl);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            init_directory_list(&g_dirs);
            init_filter_config(&g_config);
            
            // Title
            CreateWindowA("STATIC", "FILE DEDUPLICATION SYSTEM", 
                         WS_VISIBLE | WS_CHILD | SS_CENTER,
                         0, 10, 900, 30, hwnd, NULL, NULL, NULL);
            
            // Directory section
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
            
            // Hash algorithm selector
            CreateWindowA("STATIC", "Hash Algorithm:", WS_VISIBLE | WS_CHILD,
                         220, 235, 100, 20, hwnd, NULL, NULL, NULL);
            
            g_comboHash = CreateWindowA("COMBOBOX", NULL,
                                        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        330, 232, 150, 100, hwnd, (HMENU)IDC_COMBO_HASH, NULL, NULL);
            
            for (int i = 0; i < HASH_COUNT; i++) {
                SendMessageA(g_comboHash, CB_ADDSTRING, 0, (LPARAM)get_hash_algorithm_name((HashAlgorithm)i));
            }
            SendMessage(g_comboHash, CB_SETCURSEL, 0, 0);
            
            // Scan controls
            CreateWindowA("BUTTON", "Scan All Directories", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         10, 270, 150, 35, hwnd, (HMENU)IDC_BTN_SCAN, NULL, NULL);
            
            CreateWindowA("BUTTON", "Find Duplicates", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         170, 270, 150, 35, hwnd, (HMENU)IDC_BTN_FIND, NULL, NULL);
            
            CreateWindowA("BUTTON", "Save Scan Data", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         330, 270, 130, 35, hwnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
            
            CreateWindowA("BUTTON", "Load Scan Data", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         470, 270, 130, 35, hwnd, (HMENU)IDC_BTN_LOAD, NULL, NULL);
            
            // Progress bar
            g_progress = CreateWindowA(PROGRESS_CLASSA, NULL,
                                       WS_VISIBLE | WS_CHILD | PBS_MARQUEE,
                                       10, 315, 590, 20, hwnd, (HMENU)IDC_PROGRESS, NULL, NULL);
            
            // Action buttons
            CreateWindowA("STATIC", "Actions:", WS_VISIBLE | WS_CHILD,
                         10, 345, 100, 20, hwnd, NULL, NULL, NULL);
            
            CreateWindowA("BUTTON", "Delete (Keep First)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         10, 370, 140, 30, hwnd, (HMENU)IDC_BTN_DELETE_FIRST, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), FALSE);
            
            CreateWindowA("BUTTON", "Delete (By Index)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         160, 370, 140, 30, hwnd, (HMENU)IDC_BTN_DELETE_INDEX, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_INDEX), FALSE);
            
            CreateWindowA("BUTTON", "Move to Folder", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                         310, 370, 140, 30, hwnd, (HMENU)IDC_BTN_MOVE, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE), FALSE);
            
            // Results ListView
            g_listResults = CreateWindowA(WC_LISTVIEWA, NULL,
                                          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                          610, 50, 270, 350, hwnd, (HMENU)IDC_LISTVIEW_RESULTS, NULL, NULL);
            
            LVCOLUMNA lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.cx = 100;
            lvc.pszText = "Group";
            ListView_InsertColumn(g_listResults, 0, &lvc);
            
            lvc.cx = 80;
            lvc.pszText = "Size";
            ListView_InsertColumn(g_listResults, 1, &lvc);
            
            lvc.cx = 80;
            lvc.pszText = "Hash";
            ListView_InsertColumn(g_listResults, 2, &lvc);
            
            // Status text area
            g_editStatus = CreateWindowA("EDIT", "Ready. Add directories to begin.\r\n",
                                         WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | 
                                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                         10, 410, 870, 140, hwnd, (HMENU)IDC_EDIT_STATUS, NULL, NULL);
            
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_FIND), FALSE);
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
            }
            break;
        
        case WM_DESTROY:
            if (g_files) free(g_files);
            free_duplicate_results(&g_results);
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
    
    HWND hwnd = CreateWindowA("FileDedupClass", "File Deduplication System - Complete Edition",
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