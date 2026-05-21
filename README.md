# File Manager
(20230866) Antonio Recuero Buleje  
(20220825) Enrique Jose Delgado Garcia  
(20230951) Elmoursi Ahmad  
(20230959) Murad Ibrahimov  

This repo is the implementation of Team 12's File Manager, by Team 7.

## Table of Contents
1. [Repository Structure](#repository-structure)
2. [Build Instructions](#build-instructions)
3. [Running the Application](#running-the-application)
4. [Testing](#testing)
5. [Database & Indexing API](#database--indexing-api)

## Repository Structure
```
file-manager/
├── src/               # All .cpp source files
│   ├── core/          # Database, Indexer, USN Journal
│   ├── categorization/# Categorization logic (Write to DB)
│   ├── search/        # Quick + Smart Search logic (Read from DB)
│   └── ui/            # WebUI interface
├── include/           # Public headers
├── tests/             # Test executables (one per subsystem)
│   ├── test_database.cpp
│   ├── test_quick_search.cpp
│   ├── test_indexer.cpp
│   └── test_usn_journal.cpp
├── assets/            # Assets for the Overlay UI
├── docs/              # Relevant documents (SRS)
├── vcpkg.json         # Dependency manager
└── CMakeLists.txt     # Build script
```

## Build Instructions (Setting up the Database and Dependencies)

This project uses **CMake** as the build system and **vcpkg** in Manifest Mode for dependency management (including the local SQLite backend).

### Prerequisites
1. **C++ Compiler** (e.g., MSVC, GCC, or Clang)
2. **CMake** (v3.15+)
3. **vcpkg** (included as a submodule):
   - Initialize the submodule and bootstrap vcpkg by running:
     ```bash
     git submodule update --init
     .\vcpkg\bootstrap-vcpkg.bat
     ```

### Building the Project
When configuring the project for the first time, you must pass the vcpkg toolchain file to CMake. This tells CMake to read the `vcpkg.json` file and automatically download/build dependencies like SQLite3.

1. **Configure the Project**:
   ```bash
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
2. **Build the Project** (builds all targets including tests):
   ```bash
   cmake --build build
   ```

## Running the Application

The project compiles into one core executable and four test executables.

**Core Engine (`file_manager_core.exe`)**:
Responsible for initializing the database schema, running the initial static directory scan, and then entering a background loop that polls the USN Journal every 5 seconds for real-time file changes.
```bash
.\build\Debug\file_manager_core.exe
```
> **Note:** The USN Journal listener requires **Administrator privileges** to open the NTFS volume handle (`\\.\C:`). If run without admin rights, the engine automatically falls back to the static scanner and logs:
> `[USN Journal] Real-time sync disabled. Falling back to static scanner.`

---

## Testing

All test executables write their own temporary SQLite databases (e.g., `test_database_cases.db`) and clean them up on exit. **No Administrator rights are required** except for the one USN test that probes the volume path — and even that test gracefully accepts "access denied" as a valid result.

### Quick overview

| Executable | What it tests | Admin needed? |
|---|---|---|
| `test_database.exe` | DB init, WAL mode, foreign keys, insertFile, null fields, cascade delete, upsert | No |
| `test_quick_search.exe` | Filename search, tag search, no-results, null `last_modified` crash fix, DISTINCT | No |
| `test_indexer.exe` | `formatFileTime` year correctness, absolute paths, invalid path handling | No |
| `test_usn_journal.exe` | Volume path construction, reason flag logic, modify events, bounds guard, USN type | No (probes path only) |

### Running all tests

Run each executable from the **project root** (the directory containing `CMakeLists.txt`), so relative paths resolve correctly:

```bash
.\build\Debug\test_database.exe
.\build\Debug\test_quick_search.exe
.\build\Debug\test_indexer.exe
.\build\Debug\test_usn_journal.exe
```

A passing run prints `[PASS]` for each test case and ends with `All <subsystem> tests passed.`

---

### Test details

#### `test_database` — `tests/test_database.cpp`
Tests the `DatabaseManager` class in isolation.

| Test case | What it verifies |
|---|---|
| `test_DbOpensAndSchemaCreated` | `files` and `tags` tables exist after construction |
| `test_WalModeEnabled` | `PRAGMA journal_mode` returns `wal` |
| `test_ForeignKeysEnabled` | `PRAGMA foreign_keys` returns `1` |
| `test_InsertFileFullRecord` | Row is inserted and queryable by path |
| `test_InsertFileNullLastModified` | Empty `last_modified` is stored as SQL `NULL`, not an empty string |
| `test_TagsCascadeDeletedWithFile` | Deleting a file row removes its tags via `ON DELETE CASCADE` |
| `test_InsertOrReplaceUpserts` | Re-inserting the same path updates the row instead of duplicating it |

#### `test_quick_search` — `tests/test_quick_search.cpp`
Tests the `QuickSearch` class end-to-end against a live (temporary) SQLite DB.

| Test case | What it verifies |
|---|---|
| `test_SearchByFileName` | Substring match on `file_name` returns the correct record |
| `test_SearchByTag` | Match on a tag name returns the associated file |
| `test_SearchNoResults` | A keyword with no match returns an empty `vector` |
| `test_SearchNullLastModifiedNoCrash` | Records with `NULL` `last_modified` do not crash or throw |
| `test_SearchDistinctResult` | A file with two matching tags appears exactly once in results |

#### `test_indexer` — `tests/test_indexer.cpp`
Tests the `Indexer` class and the `formatFileTime` helper.

| Test case | What it verifies |
|---|---|
| `test_FormatFileTimeValidFormat` | Output matches `YYYY-MM-DD HH:MM:SS` and year ≥ 2026 (catches the ~116-year MSVC epoch bug) |
| `test_ScanDirectoryStoresAbsolutePaths` | All `file_path` values stored in the DB are absolute paths |
| `test_ScanDirectoryInvalidPath` | Passing a non-existent path does not throw; DB stays empty |

#### `test_usn_journal` — `tests/test_usn_journal.cpp`
Unit-tests the USN Journal processing logic. Tests that require an open volume handle are automatically skipped when run without admin.

| Test case | Admin? | What it verifies |
|---|---|---|
| `test_VolumePathConstruction` | No (CreateFile will just fail) | Drive inputs `"C"`, `"C:"`, `"C:\"` all produce `\\.\C:` — error is access-denied, not invalid-name |
| `test_PollChangesNoOpWhenNotInitialized` | No | `pollChanges()` returns `false` when `init()` failed; prevents journalId=0 crash |
| `test_ReasonFlagPrecedence` | No | Demonstrates the old operator-precedence bug and confirms the fix correctly detects `USN_REASON_FILE_CREATE` |
| `test_ModifyEventsDetected` | No | `USN_REASON_DATA_OVERWRITE` and `DATA_EXTEND` are captured by the corrected reason check |
| `test_CharNameBoundsCheck` | No | A filename at the `MAX_PATH` boundary triggers the bounds guard and is skipped safely |
| `test_UsnTypeSigned` | No | Confirms `USN` is a signed `LONGLONG`, not an unsigned `DWORDLONG` |

### Manual USN Journal test (requires Administrator)

To verify real-time file tracking end-to-end:

1. Right-click `file_manager_core.exe` → **Run as administrator**.
2. Confirm the console shows:
   ```
   [USN Journal] Successfully initialized real-time listener on C:
   ```
3. While it is running, create a new file anywhere on C: (e.g., in PowerShell: `New-Item -Path "$env:TEMP\fm_test.txt" -ItemType File`).
4. Wait up to 5 seconds (the poll interval).
5. The console should print:
   ```
   [USN Journal] Synchronized file: C:\Users\...\fm_test.txt
   ```
6. Verify the file appears in `local_database.db` using [DB Browser for SQLite](https://sqlitebrowser.org/) (free).

To test on a secondary NTFS drive instead (safer, avoids antivirus alerts):
- Edit `main.cpp` line with `Indexer indexer(db, "C:");` → change `"C:"` to your drive letter (e.g., `"D:"`), rebuild, and run as admin.

---

## Database & Indexing API

### Database Initialization
The local SQLite database backend is automatically initialized in the code. Upon running the `file_manager_core` executable, the `DatabaseManager` (in `src/core/database.cpp`) will:
1. Create the database file (`local_database.db`) in the root directory.
2. Enable `PRAGMA foreign_keys = ON` and `PRAGMA journal_mode = WAL` for cascade deletes and safe concurrent access.
3. Run the `CREATE TABLE IF NOT EXISTS` commands to ensure all necessary tables are created. The `files` table is structured to handle data for both Quick Search (metadata) and Smart Search (embeddings).

There is no need for team members to install a separate SQL server or run external SQL scripts!

### The `FileRecord` Struct
Data is passed between the UI, the Database, and the Search modules using the `FileRecord` struct (defined in `include/core/database.h`). All fields except `file_name`, `file_path`, and `last_modified` are optional and will be stored as SQL `NULL` if left empty:
- `file_id`
- `file_name`, `file_path`, `extension`
- `created_date`, `last_modified`
- `embedding` (for Smart Search vectors)

### Directory Indexing
The `Indexer` class (`src/core/indexer.cpp`) uses standard C++17 `<filesystem>` to recursively crawl directories.
- You can pass an absolute directory path to `indexer.scanDirectory(path)`.
- It will automatically extract file metadata (extensions, last-modified dates), instantiate a `FileRecord`, and insert it into the SQLite database.
- Paths are stored as fully qualified absolute paths to enable reliable file launch from the UI.

### Real-Time Sync (USN Journal)
The `UsnJournal` class (`src/core/usn_journal.cpp`) listens for file system changes on NTFS volumes using the Windows USN Change Journal API (`FSCTL_QUERY_USN_JOURNAL` / `FSCTL_READ_USN_JOURNAL`). It handles:
- **File creation and rename** — resolves the full path via `OpenFileById` and upserts the record.
- **File modification** (`DATA_OVERWRITE`, `DATA_EXTEND`) — upserts the record with the updated metadata.
- **File deletion** — removes the record from the database by filename.

The listener is initialized automatically at startup. If it fails (no admin rights or non-NTFS volume), the engine falls back silently to the static scanner.
