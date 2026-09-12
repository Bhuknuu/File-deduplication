/*
 * GUI_WIN32.C - Windows GUI interface (FIXED VERSION)
 */

#include "common.h"

#ifndef INITGUID
#define INITGUID
#endif
#include <shobjidl.h>

// Library linking is handled via linker flags in build.bat:
// -lcomctl32 -lshell32 -lole32

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
#define LVS_EX_GRIDLINES  0x00000001
#endif
#ifndef LVS_EX_CHECKBOXES
#define LVS_EX_CHECKBOXES 0x00000004
#endif

// Main window control IDs
#define IDC_BTN_ADD_DIR          1001
#define IDC_BTN_REMOVE_DIR       1002
#define IDC_BTN_ADD_EXCLUSION    1003
#define IDC_BTN_REMOVE_EXCLUSION 1004
#define IDC_BTN_SCAN             1005
#define IDC_BTN_FIND             1006
#define IDC_BTN_DELETE_FIRST     1007
#define IDC_BTN_MOVE             1008
#define IDC_BTN_HARD_LINK        1009
#define IDC_BTN_MOVE_ALL         1011
#define IDC_LABEL_TITLE          4001
#define IDC_LABEL_DIRS           4002
#define IDC_LABEL_EXCLUSIONS     4003
#define IDC_LABEL_SCAN_MODE      4004
#define IDC_LABEL_ACTIONS        4005


#define IDC_LISTBOX_DIRS         2001
#define IDC_LISTBOX_EXCLUSIONS   2002
#define IDC_LISTVIEW_RESULTS     2003
#define IDC_EDIT_STATUS          2004
#define IDC_PROGRESS             2005
#define IDC_CHECK_SUBDIRS        3001
#define IDC_COMBO_HASH           3002

// (old terminal-input dialog IDs removed)

// Global data
CRITICAL_SECTION g_dataLock;
ProgressInfo g_progress = {0};

static AdvancedConfig g_config = {0};
static FileInfo* g_files = NULL;
static int g_file_count = 0;
static DuplicateResults g_results = {0};
static ITaskbarList3* g_pTaskbar = NULL;

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
// static HWND g_btnDeleteByIndex;  // Currently unused

// Thread handles
static HANDLE g_hScanThread = NULL;
static HANDLE g_hFindThread = NULL;

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

// ============================================================================
// DELETE-BY-CHOICE DIALOG
// A single window: group dropdown at top, checkbox list below.
// The user ticks every file they want DELETED, leaves at least one unticked
// (the file to keep), then clicks "Delete Checked Files".
// ============================================================================

DWORD WINAPI ScanThread(LPVOID param) {
    (void)param;  // Suppress unused parameter warning
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
    AdvancedConfig config_copy = g_config;
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
    (void)param;  // Suppress unused parameter warning
    AppendStatus("Finding duplicates...\r\n");
    
    /* Snapshot file data under lock */
    EnterCriticalSection(&g_dataLock);
    FileInfo* files_snapshot = g_files;
    int count_snapshot = g_file_count;
    LeaveCriticalSection(&g_dataLock);
    
    /* Do the heavy computation outside the lock */
    DuplicateResults new_results = find_duplicates(files_snapshot, count_snapshot);
    
    /* Swap results under lock */
    EnterCriticalSection(&g_dataLock);
    free_duplicate_results(&g_results);
    g_results = new_results;
    LeaveCriticalSection(&g_dataLock);
    
    UpdateListView();
    
    PostMessage(g_hwndMain, WM_FIND_COMPLETE, 0, 0);
    return 0;
}

static void UpdateTaskbarProgress(HWND hwnd, int percent, bool is_complete, bool is_error) {
    if (!g_pTaskbar) return;
    if (is_error) {
        g_pTaskbar->lpVtbl->SetProgressState(g_pTaskbar, hwnd, TBPF_ERROR);
    } else if (is_complete) {
        g_pTaskbar->lpVtbl->SetProgressState(g_pTaskbar, hwnd, TBPF_NOPROGRESS);
    } else {
        g_pTaskbar->lpVtbl->SetProgressState(g_pTaskbar, hwnd, TBPF_NORMAL);
        g_pTaskbar->lpVtbl->SetProgressValue(g_pTaskbar, hwnd, (ULONGLONG)percent, 100ULL);
    }
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
        
        UpdateTaskbarProgress(g_hwndMain, 100, true, false);
    } else if (files_scanned > 0) {
        EnterCriticalSection(&g_dataLock);
        int percent = g_progress.current_percent;
        LeaveCriticalSection(&g_dataLock);
        SendMessage(hProgress, PBM_SETPOS, percent, 0);
        UpdateTaskbarProgress(g_hwndMain, percent, false, false);
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
    EnterCriticalSection(&g_dataLock);
    int group_count = g_results.count;
    int total_dups  = 0;
    for (int i = 0; i < group_count; i++)
        total_dups += g_results.groups[i].count - 1;
    LeaveCriticalSection(&g_dataLock);

    char warn[512];
    snprintf(warn, sizeof(warn),
        "This will permanently delete %d duplicate file(s) across %d group(s).\n"
        "In each group, the FIRST file listed is kept; all others are deleted.\n\n"
        "This cannot be undone. Proceed?",
        total_dups, group_count);

    if (MessageBoxA(g_hwndMain, warn,
        "Confirm: Delete All Duplicates (Keep First)",
        MB_YESNO | MB_ICONWARNING) != IDYES) return;

    EnterCriticalSection(&g_dataLock);
    int removed = remove_duplicates_keep_first(&g_results);
    free_duplicate_results(&g_results);
    memset(&g_results, 0, sizeof(g_results));
    LeaveCriticalSection(&g_dataLock);

    char msg[128];
    snprintf(msg, sizeof(msg), "Deleted %d duplicate file(s)\r\n", removed);
    AppendStatus(msg);
    ListView_DeleteAllItems(g_listResults);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST),    FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE),            FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK),       FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE_ALL),        FALSE);
}


void OnMove() {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwndMain;
    bi.lpszTitle = "Select folder to move duplicates into (first copy of each group is kept in place)";
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
            snprintf(msg, sizeof(msg), "Moved %d duplicate file(s) to %s\r\n", moved, path);
            AppendStatus(msg);

            ListView_DeleteAllItems(g_listResults);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST),    FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE),            FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK),       FALSE);
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE_ALL),        FALSE);
        }
        CoTaskMemFree(pidl);
    }
}

// Move All Duplicates -- organized into subfolders where needed
void OnMoveAll() {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwndMain;
    bi.lpszTitle =
        "Select destination folder for all duplicates.\n"
        "Groups with 2+ duplicates will get their own named subfolder.\n"
        "First copy of every group stays in its original location.";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return;

    char dest[MAX_PATH];
    if (!SHGetPathFromIDListA(pidl, dest)) {
        CoTaskMemFree(pidl);
        return;
    }
    CoTaskMemFree(pidl);

    // Describe what will happen before doing it
    EnterCriticalSection(&g_dataLock);
    int total_dups = 0;
    int needs_subfolder = 0;
    for (int i = 0; i < g_results.count; i++) {
        int dups = g_results.groups[i].count - 1;
        total_dups += dups;
        if (dups > 1) needs_subfolder++;
    }
    LeaveCriticalSection(&g_dataLock);

    char preview[512];
    snprintf(preview, sizeof(preview),
        "About to move %d duplicate file(s) into:\n    %s\n\n"
        "%d group(s) will create a named subfolder (because they have more than one duplicate).\n\n"
        "First copy of every group stays where it is.\n\n"
        "Proceed?",
        total_dups, dest, needs_subfolder);

    if (MessageBoxA(g_hwndMain, preview, "Confirm Move All Duplicates",
                   MB_YESNO | MB_ICONINFORMATION) != IDYES) return;

    EnterCriticalSection(&g_dataLock);
    int moved = move_all_duplicates_organized(&g_results, dest);
    free_duplicate_results(&g_results);
    memset(&g_results, 0, sizeof(g_results));
    LeaveCriticalSection(&g_dataLock);

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Moved %d duplicate file(s) into %s\r\n", moved, dest);
    AppendStatus(msg);

    ListView_DeleteAllItems(g_listResults);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST),    FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE),            FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK),       FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE_ALL),        FALSE);
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
}

static BOOL CALLBACK SetChildFont(HWND hwndChild, LPARAM lParam) {
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// Handle window resizing with symmetrical grid scaling
void OnSize(HWND hwnd, UINT state, int cx, int cy) {
    (void)state;
    if (cx <= 0 || cy <= 0) return;

    int margin = 14;
    int spacing = 10;
    int w_full = cx - 2 * margin;

    // Header title
    SetWindowPos(GetDlgItem(hwnd, IDC_LABEL_TITLE), NULL, margin, 8, w_full, 22, SWP_NOZORDER);

    // Top section: Symmetrical 2-column grid
    int col_width = (w_full - spacing) / 2;
    int col1_x = margin;
    int col2_x = margin + col_width + spacing;
    int top_btn_w = 110;
    int top_list_w = col_width - top_btn_w - spacing;
    if (top_list_w < 100) top_list_w = 100;

    int y_top = 34;
    // Left column: Directories
    SetWindowPos(GetDlgItem(hwnd, IDC_LABEL_DIRS), NULL, col1_x, y_top, top_list_w, 18, SWP_NOZORDER);
    SetWindowPos(g_listDirs, NULL, col1_x, y_top + 20, top_list_w, 100, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_ADD_DIR), NULL, col1_x + top_list_w + spacing, y_top + 20, top_btn_w, 26, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_REMOVE_DIR), NULL, col1_x + top_list_w + spacing, y_top + 52, top_btn_w, 26, SWP_NOZORDER);

    // Right column: Exclusions
    SetWindowPos(GetDlgItem(hwnd, IDC_LABEL_EXCLUSIONS), NULL, col2_x, y_top, top_list_w, 18, SWP_NOZORDER);
    SetWindowPos(g_listExclusions, NULL, col2_x, y_top + 20, top_list_w, 100, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_ADD_EXCLUSION), NULL, col2_x + top_list_w + spacing, y_top + 20, top_btn_w, 26, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_REMOVE_EXCLUSION), NULL, col2_x + top_list_w + spacing, y_top + 52, top_btn_w, 26, SWP_NOZORDER);

    // Middle controls row
    int y_mid = y_top + 100 + 12; // 146
    SetWindowPos(g_checkSubdirs, NULL, margin, y_mid + 2, 175, 22, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_LABEL_SCAN_MODE), NULL, margin + 195, y_mid + 4, 75, 18, SWP_NOZORDER);
    SetWindowPos(g_comboHash, NULL, margin + 275, y_mid, 185, 120, SWP_NOZORDER);

    // Scan / Find triggers
    int y_trig = y_mid + 30; // 176
    int trig_w = 150;
    SetWindowPos(g_btnScan, NULL, margin, y_trig, trig_w, 28, SWP_NOZORDER);
    SetWindowPos(g_btnFind, NULL, margin + trig_w + spacing, y_trig, trig_w, 28, SWP_NOZORDER);

    // Progress bar
    int y_prog = y_trig + 36; // 212
    SetWindowPos(g_hwndProgress, NULL, margin, y_prog, w_full, 18, SWP_NOZORDER);

    // Action buttons toolbar
    int y_act_label = y_prog + 24; // 236
    SetWindowPos(GetDlgItem(hwnd, IDC_LABEL_ACTIONS), NULL, margin, y_act_label, 250, 18, SWP_NOZORDER);

    int y_act_btns = y_act_label + 20; // 256
    int act_btn_w = (w_full - 3 * spacing) / 4;
    int act_btn_h = 28;
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), NULL, margin, y_act_btns, act_btn_w, act_btn_h, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_MOVE), NULL, margin + (act_btn_w + spacing), y_act_btns, act_btn_w, act_btn_h, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_MOVE_ALL), NULL, margin + (act_btn_w + spacing) * 2, y_act_btns, act_btn_w, act_btn_h, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_HARD_LINK), NULL, margin + (act_btn_w + spacing) * 3, y_act_btns, act_btn_w, act_btn_h, SWP_NOZORDER);

    // Bottom status and middle results ListView
    int y_list = y_act_btns + act_btn_h + 8; // 292
    int status_h = 80;
    int y_status = cy - margin - status_h;
    int listview_h = y_status - y_list - spacing;
    if (listview_h < 80) listview_h = 80;

    SetWindowPos(g_listResults, NULL, margin, y_list, w_full, listview_h, SWP_NOZORDER);
    SetWindowPos(g_editStatus, NULL, margin, y_status, w_full, status_h, SWP_NOZORDER);

    // Proportional column resizing in ListView
    LVCOLUMNA lvc = {0};
    lvc.mask = LVCF_WIDTH;
    lvc.cx = 105;
    ListView_SetColumn(g_listResults, 0, &lvc);

    lvc.cx = 90;
    ListView_SetColumn(g_listResults, 1, &lvc);

    int rem_w = w_full - 195 - 25; // subtract fixed columns and scrollbar
    if (rem_w > 100) {
        lvc.cx = (int)(rem_w * 0.30);
        ListView_SetColumn(g_listResults, 2, &lvc);

        lvc.cx = rem_w - lvc.cx;
        ListView_SetColumn(g_listResults, 3, &lvc);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            InitializeCriticalSection(&g_dataLock);
            
            /* Initialize Windows 7+ Taskbar Progress API */
            if (SUCCEEDED(CoCreateInstance(&CLSID_TaskbarList, NULL,
                                           CLSCTX_INPROC_SERVER,
                                           &IID_ITaskbarList3,
                                           (void**)&g_pTaskbar))) {
                if (FAILED(g_pTaskbar->lpVtbl->HrInit(g_pTaskbar))) {
                    g_pTaskbar->lpVtbl->Release(g_pTaskbar);
                    g_pTaskbar = NULL;
                }
            }
            
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
            
            CreateWindowA("STATIC", "Folders to Exclude:", WS_VISIBLE | WS_CHILD, 420, 34, 200, 18, hwnd, (HMENU)IDC_LABEL_EXCLUSIONS, NULL, NULL); 
            
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
            
            CreateWindowA("STATIC", "Scan Mode:", WS_VISIBLE | WS_CHILD, 210, 150, 75, 18, hwnd, (HMENU)IDC_LABEL_SCAN_MODE, NULL, NULL); 
            
            g_comboHash = CreateWindowA("COMBOBOX", NULL,
                WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                300, 175, 150, 100, hwnd, (HMENU)IDC_COMBO_HASH, NULL, NULL);
            
            SendMessageA(g_comboHash, CB_ADDSTRING, 0, 
                (LPARAM)"SHA-256 (1MB Quick)");
            SendMessageA(g_comboHash, CB_ADDSTRING, 0, 
                (LPARAM)"SHA-256 (Full Thorough)");
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
            
            CreateWindowA("STATIC", "Actions (Applied to Duplicates):", WS_VISIBLE | WS_CHILD, 14, 236, 250, 18, hwnd, (HMENU)IDC_LABEL_ACTIONS, NULL, NULL); 
            
            CreateWindowA("BUTTON", "Delete (Keep First)", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                10, 295, 130, 28, hwnd, (HMENU)IDC_BTN_DELETE_FIRST, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST), FALSE);
            
            
            CreateWindowA("BUTTON", "Move to Folder",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                290, 295, 130, 28, hwnd, (HMENU)IDC_BTN_MOVE, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE), FALSE);

            CreateWindowA("BUTTON", "Move All (Organized)",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                430, 295, 140, 28, hwnd, (HMENU)IDC_BTN_MOVE_ALL, NULL, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE_ALL), FALSE);

            CreateWindowA("BUTTON", "Create Hard Links",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                580, 295, 130, 28, hwnd, (HMENU)IDC_BTN_HARD_LINK, NULL, NULL);
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

            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            EnumChildWindows(hwnd, SetChildFont, (LPARAM)hFont);
            
            break;
        }
        
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
            mmi->ptMinTrackSize.x = 760;
            mmi->ptMinTrackSize.y = 560;
            return 0;
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
            
        case WM_FIND_COMPLETE: {
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
            
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_FIRST),    has_results);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE),            has_results);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_MOVE_ALL),        has_results);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_HARD_LINK),       has_results);
            break;
        }
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_DIR: OnAddDirectory(); break;
                case IDC_BTN_REMOVE_DIR: OnRemoveDirectory(); break;
                case IDC_BTN_ADD_EXCLUSION: OnAddExclusion(); break;
                case IDC_BTN_REMOVE_EXCLUSION: OnRemoveExclusion(); break;
                case IDC_BTN_SCAN: OnScan(); break;
                case IDC_BTN_FIND: OnFind(); break;
                case IDC_BTN_DELETE_FIRST: OnDeleteFirst(); break;
                case IDC_BTN_MOVE:            OnMove(); break;
                case IDC_BTN_MOVE_ALL:        OnMoveAll(); break;
                case IDC_BTN_HARD_LINK:       OnHardLink(); break;
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
            
            if (g_pTaskbar) {
                g_pTaskbar->lpVtbl->Release(g_pTaskbar);
                g_pTaskbar = NULL;
            }
            
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
    (void)hPrevInstance;  // Suppress unused parameter warning
    (void)lpCmdLine;      // Suppress unused parameter warning
    (void)nCmdShow;       // Suppress unused parameter warning
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
        "File Deduplication System v3.0",
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
