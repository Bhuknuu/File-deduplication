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
#define IDC_BTN_DELETE_BY_INDEX  1010
#define IDC_BTN_MOVE_ALL         1011

// Delete-by-Choice dialog control IDs
#define IDC_DBI_COMBO_GROUP    6001
#define IDC_DBI_LIST           6002
#define IDC_DBI_LABEL_TOP      6003
#define IDC_DBI_LABEL_STATUS   6004
#define IDC_DBI_LABEL_WARN     6005
#define IDC_DBI_BTN_DELETE     6006
#define IDC_DBI_BTN_CANCEL     6007

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

static ScanConfig g_config = {0};
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

// ============================================================================
// DELETE-BY-CHOICE DIALOG -- data types and static helpers
// ============================================================================

// Heap snapshot of one duplicate group, taken under lock before the dialog opens.
typedef struct {
    int        count;                       // number of files
    char**     paths;                       // count heap strings
    long long* sizes;                       // count file sizes
    char       anchor[MAX_PATH_LENGTH];     // files[0].path -- stale-data identity check
} DbiGroupSnap;

// State block allocated before dialog opens; passed via lParam to the dialog proc.
typedef struct {
    int            group_count;
    DbiGroupSnap*  groups;
    // Output -- written by the proc before DestroyWindow
    bool           confirmed;
    int            chosen_group;    // which group the user acted on
    bool*          delete_mask;     // delete_mask[j] == true -> permanently delete file j
    int            mask_count;      // length of delete_mask
} DbiDialogData;

// Prevents LVN_ITEMCHANGED from triggering status updates during list repopulation.
static bool g_dbi_repopulating = false;

// Portable strdup (avoids _strdup / strdup portability questions).
static char* dbi_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// Free everything inside a DbiDialogData and the struct itself.
static void DbiDataFree(DbiDialogData* d) {
    if (!d) return;
    if (d->groups) {
        for (int i = 0; i < d->group_count; i++) {
            DbiGroupSnap* g = &d->groups[i];
            if (g->paths) {
                for (int j = 0; j < g->count; j++) free(g->paths[j]);
                free(g->paths);
            }
            free(g->sizes);
        }
        free(d->groups);
    }
    free(d->delete_mask);
    free(d);
}

// Rebuild the checkbox ListView for group gi from the snapshot.
// Clears the list, inserts all files, and sets the default check state:
//   file 0 unchecked (to keep), all others checked (to delete).
static void DbiRepopulateList(HWND hDlg, const DbiDialogData* d, int gi) {
    HWND hList = GetDlgItem(hDlg, IDC_DBI_LIST);
    if (!hList) return;

    g_dbi_repopulating = true;
    ListView_DeleteAllItems(hList);

    if (gi < 0 || gi >= d->group_count) {
        g_dbi_repopulating = false;
        return;
    }

    const DbiGroupSnap* grp = &d->groups[gi];
    for (int j = 0; j < grp->count; j++) {
        char num[8];
        snprintf(num, sizeof(num), "%d", j + 1);

        LVITEMA lvi = {0};
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = j;
        lvi.pszText = num;
        ListView_InsertItem(hList, &lvi);

        const char* fname = strrchr(grp->paths[j], '\\');
        fname = fname ? fname + 1 : grp->paths[j];
        ListView_SetItemText(hList, j, 1, (char*)fname);

        char sz[32];
        format_file_size(grp->sizes[j], sz, sizeof(sz));
        ListView_SetItemText(hList, j, 2, sz);
        ListView_SetItemText(hList, j, 3, grp->paths[j]);

        // Default: keep file[0], delete the rest
        ListView_SetCheckState(hList, j, (j != 0));
    }

    g_dbi_repopulating = false;
}

// Recount checked/unchecked items and refresh the status label + button state.
static void DbiUpdateStatus(HWND hDlg, const DbiDialogData* d, int gi) {
    HWND hList = GetDlgItem(hDlg, IDC_DBI_LIST);
    int total   = (gi >= 0 && gi < d->group_count) ? d->groups[gi].count : 0;
    int checked = 0;
    for (int j = 0; j < total; j++) {
        if (ListView_GetCheckState(hList, j)) checked++;
    }
    int kept = total - checked;

    char status[320];
    if (total == 0) {
        snprintf(status, sizeof(status), "No group selected.");
    } else if (checked == 0) {
        snprintf(status, sizeof(status),
            "Nothing is ticked.  Tick the files you want to DELETE.");
    } else if (kept == 0) {
        snprintf(status, sizeof(status),
            "All %d files are ticked -- you must keep at least one."
            "  Untick a file to keep it.", total);
    } else {
        snprintf(status, sizeof(status),
            "%d file(s) will be PERMANENTLY DELETED.   "
            "%d file(s) will be KEPT in place.",
            checked, kept);
    }
    SetDlgItemTextA(hDlg, IDC_DBI_LABEL_STATUS, status);
    EnableWindow(GetDlgItem(hDlg, IDC_DBI_BTN_DELETE), (checked > 0 && kept > 0));
}

// Forward declaration
INT_PTR CALLBACK DeleteByIndexDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

INT_PTR CALLBACK DeleteByIndexDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DbiDialogData* d = (DbiDialogData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_INITDIALOG: {
            d = (DbiDialogData*)lParam;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);
            // Resize to desired pixel dimensions and center over parent
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 590, 430,
                         SWP_NOZORDER | SWP_NOMOVE);
            RECT rc, rcP;
            GetWindowRect(hwnd, &rc);
            GetWindowRect(GetParent(hwnd), &rcP);
            int px = rcP.left + (rcP.right  - rcP.left - (rc.right  - rc.left)) / 2;
            int py = rcP.top  + (rcP.bottom - rcP.top  - (rc.bottom - rc.top )) / 2;
            SetWindowPos(hwnd, HWND_TOP, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            // Controls are not built yet; defer population until they exist
            PostMessage(hwnd, WM_APP + 1, 0, 0);
            return TRUE;
        }

        case WM_APP + 1: {
            // Controls exist now -- populate group combo then default list
            if (!d) return TRUE;
            HWND hCombo = GetDlgItem(hwnd, IDC_DBI_COMBO_GROUP);
            if (!hCombo) return TRUE;
            for (int i = 0; i < d->group_count; i++) {
                const DbiGroupSnap* g = &d->groups[i];
                const char* sample = strrchr(g->anchor, '\\');
                sample = sample ? sample + 1 : g->anchor;
                char sz[32];
                format_file_size(g->sizes[0], sz, sizeof(sz));
                char entry[512];
                snprintf(entry, sizeof(entry),
                    "Group %d  --  %d identical copies  --  \"%s\"  (%s each)",
                    i + 1, g->count, sample, sz);
                SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)entry);
            }
            SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            DbiRepopulateList(hwnd, d, 0);
            DbiUpdateStatus(hwnd, d, 0);
            return TRUE;
        }

        case WM_COMMAND: {
            WORD ctrl  = LOWORD(wParam);
            WORD notif = HIWORD(wParam);

            if (ctrl == IDC_DBI_COMBO_GROUP && notif == CBN_SELCHANGE) {
                int gi = (int)SendMessage(
                    GetDlgItem(hwnd, IDC_DBI_COMBO_GROUP), CB_GETCURSEL, 0, 0);
                DbiRepopulateList(hwnd, d, gi);
                DbiUpdateStatus(hwnd, d, gi);
                return TRUE;
            }

            if (ctrl == IDC_DBI_BTN_CANCEL) {
                if (d) d->confirmed = false;
                DestroyWindow(hwnd);
                return TRUE;
            }

            if (ctrl == IDC_DBI_BTN_DELETE) {
                if (!d) return TRUE;
                int gi = (int)SendMessage(
                    GetDlgItem(hwnd, IDC_DBI_COMBO_GROUP), CB_GETCURSEL, 0, 0);
                if (gi < 0 || gi >= d->group_count) return TRUE;

                HWND hList = GetDlgItem(hwnd, IDC_DBI_LIST);
                int  total  = d->groups[gi].count;
                int  n_del = 0, n_keep = 0;
                for (int j = 0; j < total; j++) {
                    if (ListView_GetCheckState(hList, j)) n_del++;
                    else                                   n_keep++;
                }

                if (n_del == 0 || n_keep == 0) {
                    MessageBoxA(hwnd,
                        "Invalid selection.\n\n"
                        "You must tick at least one file to delete "
                        "AND leave at least one unticked to keep.",
                        "Invalid Selection", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                // Build an explicit confirmation listing every path
                char* confirm = (char*)malloc(4096);
                if (!confirm) {
                    MessageBoxA(hwnd, "Out of memory.", "Error", MB_ICONERROR);
                    return TRUE;
                }
                int pos = 0;
                pos += snprintf(confirm + pos, 4096 - pos,
                    "The following %d file(s) will be PERMANENTLY DELETED:\n\n",
                    n_del);
                for (int j = 0; j < total && pos < 3800; j++) {
                    if (ListView_GetCheckState(hList, j))
                        pos += snprintf(confirm + pos, 4096 - pos,
                            "  [DELETE]  %s\n", d->groups[gi].paths[j]);
                }
                pos += snprintf(confirm + pos, 4096 - pos,
                    "\nThe following %d file(s) will be KEPT:\n\n", n_keep);
                for (int j = 0; j < total && pos < 3800; j++) {
                    if (!ListView_GetCheckState(hList, j))
                        pos += snprintf(confirm + pos, 4096 - pos,
                            "  [KEEP]    %s\n", d->groups[gi].paths[j]);
                }
                snprintf(confirm + pos, 4096 - pos,
                    "\nThis cannot be undone.  Proceed?");

                int answer = MessageBoxA(hwnd, confirm,
                    "Confirm Permanent Deletion", MB_YESNO | MB_ICONWARNING);
                free(confirm);
                if (answer != IDYES) return TRUE;

                // Build delete mask
                bool* mask = (bool*)calloc(total, sizeof(bool));
                if (!mask) {
                    MessageBoxA(hwnd, "Out of memory.", "Error", MB_ICONERROR);
                    return TRUE;
                }
                for (int j = 0; j < total; j++)
                    mask[j] = ListView_GetCheckState(hList, j) ? true : false;

                d->delete_mask  = mask;
                d->mask_count   = total;
                d->chosen_group = gi;
                d->confirmed    = true;
                DestroyWindow(hwnd);
                return TRUE;
            }
            break;
        }

        case WM_NOTIFY: {
            if (!d) break;
            NMHDR* pnm = (NMHDR*)lParam;
            if (pnm->idFrom == IDC_DBI_LIST &&
                pnm->code == LVN_ITEMCHANGED) {
                if (g_dbi_repopulating) break;
                NMLISTVIEW* pnmlv = (NMLISTVIEW*)lParam;
                if (!(pnmlv->uChanged & LVIF_STATE)) break;
                int gi = (int)SendMessage(
                    GetDlgItem(hwnd, IDC_DBI_COMBO_GROUP), CB_GETCURSEL, 0, 0);
                DbiUpdateStatus(hwnd, d, gi);
            }
            break;
        }

        case WM_CLOSE:
            if (d) d->confirmed = false;
            DestroyWindow(hwnd);
            return TRUE;
    }
    return FALSE;
}

// Snapshot g_results, show the unified Delete-by-Choice dialog, act on result.
static void ShowDeleteByIndexDialog(void) {

    // --- 1. Snapshot all group data under lock ---
    EnterCriticalSection(&g_dataLock);
    if (g_results.count == 0) {
        LeaveCriticalSection(&g_dataLock);
        MessageBoxA(g_hwndMain,
            "No duplicate groups found.\nRun Find Duplicates first.",
            "Nothing to Do", MB_ICONINFORMATION);
        return;
    }

    DbiDialogData* d = (DbiDialogData*)calloc(1, sizeof(DbiDialogData));
    if (!d) { LeaveCriticalSection(&g_dataLock); goto dbi_oom; }

    d->group_count = g_results.count;
    d->groups = (DbiGroupSnap*)calloc(d->group_count, sizeof(DbiGroupSnap));
    if (!d->groups) {
        LeaveCriticalSection(&g_dataLock);
        DbiDataFree(d);
        goto dbi_oom;
    }

    for (int i = 0; i < d->group_count; i++) {
        DuplicateGroup* src  = &g_results.groups[i];
        DbiGroupSnap*   snap = &d->groups[i];
        snap->count = src->count;
        snap->paths = (char**)calloc(src->count, sizeof(char*));
        snap->sizes = (long long*)calloc(src->count, sizeof(long long));
        if (!snap->paths || !snap->sizes) {
            LeaveCriticalSection(&g_dataLock);
            DbiDataFree(d);
            goto dbi_oom;
        }
        strncpy(snap->anchor, src->files[0].path, MAX_PATH_LENGTH - 1);
        snap->anchor[MAX_PATH_LENGTH - 1] = '\0';
        for (int j = 0; j < src->count; j++) {
            snap->paths[j] = dbi_strdup(src->files[j].path);
            snap->sizes[j] = src->files[j].size;
            if (!snap->paths[j]) {
                LeaveCriticalSection(&g_dataLock);
                DbiDataFree(d);
                goto dbi_oom;
            }
        }
    }
    LeaveCriticalSection(&g_dataLock);

    // --- 2. Create the dialog shell ---
    {
        HINSTANCE hInst = GetModuleHandle(NULL);
        typedef struct { DLGTEMPLATE t; WORD menu; WORD cls; WORD title; } BlankDlg;
        BlankDlg dlgt;
        memset(&dlgt, 0, sizeof(dlgt));
        dlgt.t.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER;
        dlgt.t.cx = 300;
        dlgt.t.cy = 200;

        HWND hDlg = CreateDialogIndirectParamA(hInst, (LPCDLGTEMPLATE)&dlgt,
                                                g_hwndMain,
                                                DeleteByIndexDlgProc, (LPARAM)d);
        if (!hDlg) {
            DbiDataFree(d);
            MessageBoxA(g_hwndMain, "Could not create the dialog window.",
                       "Error", MB_ICONERROR);
            return;
        }
        SetWindowTextA(hDlg, "Delete Duplicates by Choice");

        // --- 3. Build child controls ---
        CreateWindowA("STATIC", "Select the duplicate group to work on:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 10, 560, 18, hDlg, (HMENU)IDC_DBI_LABEL_TOP, hInst, NULL);

        CreateWindowA("COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
            10, 32, 560, 200, hDlg, (HMENU)IDC_DBI_COMBO_GROUP, hInst, NULL);

        CreateWindowA("STATIC",
            "Tick the files you want to DELETE.  "
            "Leave at least one unticked -- that one will be KEPT.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 70, 560, 28, hDlg, NULL, hInst, NULL);

        HWND hList = CreateWindowA(WC_LISTVIEWA, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER |
            LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
            10, 102, 560, 200, hDlg, (HMENU)IDC_DBI_LIST, hInst, NULL);

        SendMessage(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);

        LVCOLUMNA lvc;
        memset(&lvc, 0, sizeof(lvc));
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = "#";         lvc.cx = 30;  ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = "File Name"; lvc.cx = 155; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = "Size";      lvc.cx = 70;  ListView_InsertColumn(hList, 2, &lvc);
        lvc.pszText = "Full Path"; lvc.cx = 290; ListView_InsertColumn(hList, 3, &lvc);

        CreateWindowA("STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 308, 560, 40, hDlg, (HMENU)IDC_DBI_LABEL_STATUS, hInst, NULL);

        CreateWindowA("STATIC",
            "Warning: deletion is permanent and cannot be undone.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 352, 560, 18, hDlg, (HMENU)IDC_DBI_LABEL_WARN, hInst, NULL);

        HWND hBtnDel = CreateWindowA("BUTTON", "Delete Checked Files",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            10, 378, 185, 26, hDlg, (HMENU)IDC_DBI_BTN_DELETE, hInst, NULL);
        EnableWindow(hBtnDel, FALSE);  // enabled only once a valid selection exists

        CreateWindowA("BUTTON", "Cancel (do nothing)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            205, 378, 160, 26, hDlg, (HMENU)IDC_DBI_BTN_CANCEL, hInst, NULL);

        // --- 4. Show and run local message pump ---
        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (!IsWindow(hDlg)) break;
            if (!IsDialogMessage(hDlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    } /* end dialog scope */

    // --- 5. If cancelled, bail out ---
    if (!d->confirmed) {
        DbiDataFree(d);
        return;
    }

    // --- 6. Re-validate using identity anchor before touching disk ---
    {
        int   gi         = d->chosen_group;
        bool* mask       = d->delete_mask;
        int   mask_count = d->mask_count;
        char  anchor[MAX_PATH_LENGTH];
        strncpy(anchor, d->groups[gi].anchor, MAX_PATH_LENGTH - 1);
        anchor[MAX_PATH_LENGTH - 1] = '\0';

        EnterCriticalSection(&g_dataLock);
        bool stale = (gi >= g_results.count)
                  || (g_results.groups[gi].count != mask_count)
                  || (strcmp(g_results.groups[gi].files[0].path, anchor) != 0);

        if (stale) {
            LeaveCriticalSection(&g_dataLock);
            DbiDataFree(d);
            MessageBoxA(g_hwndMain,
                "The duplicate list changed while the dialog was open.\n\n"
                "The operation has been cancelled to prevent accidental data loss.\n"
                "Please run Find Duplicates again.",
                "Results Changed", MB_ICONWARNING);
            return;
        }

        // --- 7. Delete the ticked files ---
        int removed = 0, failed = 0;
        DuplicateGroup* tgt = &g_results.groups[gi];
        for (int j = 0; j < tgt->count; j++) {
            if (j >= mask_count || !mask[j]) continue;
            if (DeleteFileA(tgt->files[j].path)) removed++;
            else                                  failed++;
        }
        free_duplicate_results(&g_results);
        memset(&g_results, 0, sizeof(g_results));
        LeaveCriticalSection(&g_dataLock);

        DbiDataFree(d);

        // --- 8. Update status box and disable action buttons ---
        char status[256];
        if (failed > 0) {
            snprintf(status, sizeof(status),
                "Deleted %d file(s) from Group %d.  "
                "%d file(s) could not be deleted (check permissions).\r\n",
                removed, gi + 1, failed);
        } else {
            snprintf(status, sizeof(status),
                "Deleted %d file(s) from Group %d.\r\n", removed, gi + 1);
        }
        AppendStatus(status);
        ListView_DeleteAllItems(g_listResults);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_FIRST),    FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE),            FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_HARD_LINK),       FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
        EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE_ALL),        FALSE);
    }
    return;

dbi_oom:
    MessageBoxA(g_hwndMain,
        "Out of memory while reading the duplicate list.", "Error", MB_ICONERROR);
}

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
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_MOVE_ALL),        FALSE);
}

// OnDeleteByIndex: opens the unified Delete-by-Choice dialog.
void OnDeleteByIndex(void) {
    ShowDeleteByIndexDialog();
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
            EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
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
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
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
    EnableWindow(GetDlgItem(g_hwndMain, IDC_BTN_DELETE_BY_INDEX), FALSE);
}

// Handle window resizing
void OnSize(HWND hwnd, UINT state, int cx, int cy) {
    (void)state;  // Suppress unused parameter warning
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
    
    // Update ListView columns to resize
    LVCOLUMNA lvc = {0};
    lvc.mask = LVCF_WIDTH;
    
    // Get current column widths
    lvc.cx = 100;
    ListView_SetColumn(g_listResults, 0, &lvc);
    
    lvc.cx = 80;
    ListView_SetColumn(g_listResults, 1, &lvc);
    
    // Resize filename and path columns proportionally
    int remaining_width = progress_width - 180; // Subtract fixed column widths
    lvc.cx = remaining_width * 0.25; // 25% for filename
    ListView_SetColumn(g_listResults, 2, &lvc);
    
    lvc.cx = remaining_width * 0.75; // 75% for path
    ListView_SetColumn(g_listResults, 3, &lvc);
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
            
            CreateWindowA("STATIC", "Scan Mode:", 
                WS_VISIBLE | WS_CHILD,
                200, 177, 100, 20, hwnd, NULL, NULL, NULL);
            
            g_comboHash = CreateWindowA("COMBOBOX", NULL,
                WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                300, 175, 150, 100, hwnd, (HMENU)IDC_COMBO_HASH, NULL, NULL);
            
            SendMessageA(g_comboHash, CB_ADDSTRING, 0, 
                (LPARAM)"FNV-1a (1MB)(fast)");
            SendMessageA(g_comboHash, CB_ADDSTRING, 0, 
                (LPARAM)"FNV-1a (Full)(slower)");
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
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_DELETE_BY_INDEX), has_results);
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
                case IDC_BTN_DELETE_BY_INDEX: OnDeleteByIndex(); break;
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