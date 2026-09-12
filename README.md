<div align="center">

# WinDedup: Native Win32 File Deduplication Engine

[![Language](https://img.shields.io/badge/Language-C11-00599C.svg?logo=c)](https://en.cppreference.com/w/c/11)
[![Platform](https://img.shields.io/badge/Platform-Windows%207%2B-0078D6.svg?logo=windows)](https://microsoft.com)
[![Subsystem](https://img.shields.io/badge/Subsystem-Win32%20GUI-blue.svg)](https://learn.microsoft.com/en-us/windows/win32/)
[![Crypto](https://img.shields.io/badge/Crypto-Windows%20CNG%20(BCrypt%20SHA--256)-green.svg)](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-features)
[![Dependencies](https://img.shields.io/badge/Dependencies-Zero%20(Pure%20Win32)-brightgreen.svg)](#building--running)

<br/>

<p align="center">
  <b>A lean, deterministic desktop storage analyzer and deduplication utility built directly against the Windows subsystems.</b>
</p>

</div>

---

### Operational Model & Engineering Highlights

> **Design Tenets & Core Capabilities**
> - **Decoupled Asynchronous Dispatch:** Heavy disk enumeration and cryptographic verification run on dedicated background threads, maintaining a fluid Win32 message pump while streaming real-time progress indicators to the Windows taskbar via `ITaskbarList3`.
> - **In-Place Hardlink Re-pointing:** Rather than resorting to unrecoverable file deletion, duplicate nodes can be converted into NTFS hard links, instantly reclaiming physical disk sectors while preserving existing directory paths and programmatic file references.
> - **I/O Overhead Mitigation:** Skips repetitive passes on cold runs through size-bucket pruning and cached metadata signatures, reading only what is strictly required to verify byte identity.

---

## Interface

<div align="center">
  <img src="assets/screenshot.png" alt="WinDedup GUI Screenshot" width="560" />
</div>


---

## Architecture & System Map

### 1. Component Architecture & Data Flow

```mermaid
graph TD
    subgraph UI ["User Interface (gui_win32.c)"]
        WND[Win32 Window & Controls]
        LST[ListView / Progress Bar]
        DLG[Delete-by-Choice Dialog]
    end

    subgraph Threads ["Background Worker Threads"]
        T_SCAN[Scan Thread]
        T_FIND[Find Duplicates Thread]
    end

    subgraph Core ["Core Engine Modules"]
        TRAV["Traversal Engine (Traversal.c)<br/>- DFS Directory Tree Walk<br/>- File Metadata Harvest<br/>- Persistent Hash Cache"]
        HASH["BCrypt CNG SHA-256 Engine<br/>- Quick Scan (First 1MB)<br/>- Thorough Scan (Entire File)"]
        FILT["Duplicate Detector (filter.c)<br/>- Hash Table (Separate Chaining)<br/>- Dynamic Index Arrays<br/>- DuplicateResults Construction"]
        ACT["Action Engine (action.c)<br/>- Delete Duplicates (DeleteFileA)<br/>- Move Duplicates (MoveFileA)<br/>- Hardlink Replacement (CreateHardLinkA)"]
    end

    subgraph Storage ["File System & Cache"]
        FS[Disk Directories]
        CACHE[(dedup_cache.txt)]
    end

    WND -->|Spawn| T_SCAN
    WND -->|Spawn| T_FIND
    WND -->|Trigger| ACT

    T_SCAN --> TRAV
    TRAV --> FS
    TRAV --> HASH
    HASH --> FS
    TRAV <--> CACHE

    T_SCAN -->|PostMessage WM_SCAN_COMPLETE| WND
    T_FIND --> FILT
    FILT -->|PostMessage WM_FIND_COMPLETE| WND
    ACT --> FS
    DLG -->|User Selected Deletion| ACT
```

---

### 2. Execution Pipeline

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant GUI as Win32 Message Loop (gui_win32.c)
    participant Worker as Background Thread
    participant Scan as Traversal (Traversal.c)
    participant CNG as BCrypt CNG (SHA-256)
    participant HashTab as Hash Table (filter.c)
    participant Action as Actions (action.c)

    User->>GUI: Click "Scan Directories"
    GUI->>Worker: _beginthreadex(ScanThread)
    Worker->>Scan: scan_directories(ScanConfig)
    loop Directory DFS Recursion
        Scan->>Scan: Enumerate directory (FindFirstFileA / FindNextFileA)
        Scan->>Scan: Match Exclusion List & Reparse Points
        Scan->>CNG: compute_hash(path, Quick/Thorough)
        CNG-->>Scan: SHA-256 Hex Digest
        Scan->>Scan: Update ProgressInfo & dedup_cache.txt
    end
    Scan-->>GUI: PostMessage(WM_SCAN_COMPLETE, file_count)
    GUI->>User: Enable "Find Duplicates" button

    User->>GUI: Click "Find Duplicates"
    GUI->>Worker: _beginthreadex(FindThread)
    Worker->>HashTab: find_duplicates(FileInfo*, count)
    HashTab->>HashTab: Populate 50,021-bucket hash table
    HashTab->>HashTab: Group files with identical SHA-256
    HashTab-->>GUI: PostMessage(WM_FIND_COMPLETE, DuplicateResults*)
    GUI->>GUI: Populate Virtual ListView with Duplicate Groups

    alt Action: Delete (Keep First)
        User->>GUI: Click "Delete (Keep First)"
        GUI->>Action: remove_duplicates_keep_first(&results)
        Action->>Action: DeleteFileA on non-anchor files
    else Action: Move Organized
        User->>GUI: Click "Move All (Organized)"
        GUI->>Action: move_all_duplicates_organized(&results, dest)
    else Action: Replace with Hard Links
        User->>GUI: Click "Replace with Hard Links"
        GUI->>Action: create_hard_links(&results)
    else Action: Interactive Choice
        User->>GUI: Click "Delete by Choice..."
        GUI->>GUI: Launch DialogBoxParam (DbiDlgProc)
        User->>Action: Confirm deletions per group
    end
```
---

### 3. Core Data Structures

```mermaid
classDiagram
    class FileInfo {
        +char path[4096]
        +long long size
        +time_t modified
        +char hash[65]
    }

    class DuplicateGroup {
        +FileInfo* files
        +int count
        +int capacity
    }

    class DuplicateResults {
        +DuplicateGroup* groups
        +int count
        +int capacity
    }

    class HashNode {
        +char hash[65]
        +int* file_indices
        +int count
        +int capacity
        +HashNode* next
    }

    class CacheEntry {
        +char path[4096]
        +long long size
        +time_t modified
        +char hash[65]
        +CacheEntry* next
    }

    DuplicateResults "1" o-- "*" DuplicateGroup : contains
    DuplicateGroup "1" o-- "*" FileInfo : references
    HashNode "1" o-- "*" FileInfo : index-maps
```

---

## File Map

| File | Role | Key APIs / Responsibilities |
|---|---|---|
| `common.h` | Shared definitions | Structs (`FileInfo`, `DuplicateGroup`, `DuplicateResults`, configs), threading primitives, constants. |
| `gui_win32.c` | UI & Orchestration | Win32 message loop, `CreateWindowExA`, ListView virtual rendering, worker thread management, taskbar progress (`ITaskbarList3`), Delete-by-Choice modal dialog. |
| `Traversal.c` | Filesystem traversal | `FindFirstFileA` / `FindNextFileA` recursive DFS, reparse point skipping, Windows CNG BCrypt SHA-256 computation, persistent cache in `dedup_cache.txt`. |
| `filter.c` | Duplicate detection | 50,021-bucket hash table with separate chaining, amortized O(1) dynamic index array resizing, duplicate group assembly. |
| `action.c` | Duplicate resolution | `DeleteFileA` (keep-first removal), `MoveFileA` (flat & organized folder grouping with collision suffix `_1`), `CreateHardLinkA` deduplication. |
| `features.c` | Standalone QOL module | File logging (`log_operation`), size/extension filters (`should_process_file`), CSV/text export (`export_duplicates_csv`), standalone safe actions with dry-run support. |
| `cli_main.c` | Headless CLI layer | Command-line tool with machine-readable JSON (`--json`), `--dry-run`, size/extension filtering, and batch automation. |
| `build.bat` | Build pipeline | MinGW/GCC build automation compiling GUI (`FileDeduplication.exe`) and CLI (`dedup-cli.exe`). |

---

## Building & Running

### Requirements
- Windows 7 SP1 or newer (`_WIN32_WINNT=0x0601`)
- GCC (MinGW-w64) in `PATH`

### Build Command
```cmd
build.bat
```

Compiles two binaries into `build\`:
1. `build\FileDeduplication.exe` — Interactive Win32 desktop application
2. `build\dedup-cli.exe` — Headless scriptable CLI binary with JSON support

### Manual Compilation
```cmd
gcc -Wall -Wextra -std=c11 -O2 -D_WIN32_WINNT=0x0601 ^
    action.c filter.c gui_win32.c Traversal.c features.c ^
    -o build\FileDeduplication.exe ^
    -lcomctl32 -lshell32 -lole32 -luser32 -lgdi32 -lkernel32 -lbcrypt -luuid
```

---

## Scan Modes & Actions

- **Quick Scan (`SCAN_QUICK`):** Reads and hashes the first 1 MB (`1024 * 1024` bytes) using SHA-256. Ideal for large media collections with distinct headers.
- **Thorough Scan (`SCAN_THOROUGH`):** Memory-maps or reads the entire file to produce an end-to-end SHA-256 digest.
- **Actions:**
  - `Delete (Keep First)`: Deletes duplicate instances, retaining index 0.
  - `Move All (Organized)`: Moves single duplicate pairs into flat folder; multi-duplicate clusters into subdirectories named after original file.
  - `Replace with Hard Links`: Replaces duplicate files with NTFS hard links (`CreateHardLinkA`), freeing physical disk space while keeping all file paths intact.
  - `Delete by Choice`: Interactive dialog to inspect metadata differences and select which specific duplicates to discard.
