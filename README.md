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
4. [Database & Indexing API](#database--indexing-api)

## Repository Structure
```
file-manager/
├── src/               # All .cpp and .h files here
│   ├── core/          # NTFS/USN Journal + Database Logic
|   ├── categorization # Categorization logic (Write to DB)
│   ├── search/        # Quick + Smart Search logic (Read from DB)
│   └── ui/            # WebUI interface
├── include/           # Public headers
├── assets/            # Assets for the Overlay UI
├── docs/              # Relevant documents (SRS)
├── tests/             # Test cases (?)
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
2. **Build the Project**:
   ```bash
   cmake --build build
   ```

## Running the Application

The project compiles into two distinct executables to separate background logic from testing logic:

1. **Core Engine (`file_manager_core.exe`)**:
   Responsible for initializing the database schema and running the background indexing. It does not provide an interactive prompt.
   ```bash
   .\build\Debug\file_manager_core.exe
   ```

2. **Quick Search Test Module (`test_quick_search.exe`)**:
   An interactive testing module that connects to your existing local database and allows you to test `LIKE` queries against indexed files.
   ```bash
   .\build\Debug\test_quick_search.exe
   ```

## Database & Indexing API

### Database Initialization
The local SQLite database backend is automatically initialized in the code. Upon running the `file_manager_core` executable, the `DatabaseManager` (in `src/core/database.cpp`) will:
1. Create the database file (`local_database.db`) in the root directory.
2. Run the `CREATE TABLE IF NOT EXISTS` commands to ensure all necessary tables are created. The `files` table is structured to handle data for both Quick Search (metadata) and Smart Search (embeddings).

There is no need for team members to install a separate SQL server or run external SQL scripts!

### The `FileRecord` Struct
Data is passed between the UI, the Database, and the Search modules using the `FileRecord` struct (defined in `include/core/database.h`). It contains optional fields that can default to NULL:
- `file_id`
- `file_name`, `file_path`, `extension`
- `created_date`, `last_modified`
- `embedding` (For Smart Search vectors)

### Directory Indexing
The `Indexer` class (`src/core/indexer.cpp`) uses standard C++17 `<filesystem>` to recursively crawl directories. 
- You can pass a directory path to `indexer.scanDirectory(path)`.
- It will automatically extract file metadata (like extensions and last modified dates), instantiate a `FileRecord`, and insert it into the SQLite database.

> **Note on NTFS USN Journal:** Currently, the system uses a standard recursive directory scanner rather than the low-level Windows NTFS USN Journal. This means the database does not actively "listen" for file creation/deletion events in the background. It simply builds a static index of the directory at the moment `scanDirectory` is called. Implementing the USN Journal listener will be required in the future for real-time synchronization.
does not provide an interactive prompt.
   ```bash
   .\build\Debug\file_manager_core.exe
   ```

2. **Quick Search Test Module (`test_quick_search.exe`)**:
   An interactive testing module that connects to your existing local database and allows you to test `LIKE` queries against indexed files.
   ```bash
   .\build\Debug\test_quick_search.exe
   ```

## Database & Indexing API

### Database Initialization
The local SQLite database backend is automatically initialized in the code. Upon running the `file_manager_core` executable, the `DatabaseManager` (in `src/core/database.cpp`) will:
1. Create the database file (`local_database.db`) in the root directory.
2. Run the `CREATE TABLE IF NOT EXISTS` commands to ensure all necessary tables are created. The `files` table is structured to handle data for both Quick Search (metadata) and Smart Search (embeddings).

There is no need for team members to install a separate SQL server or run external SQL scripts!

### The `FileRecord` Struct
Data is passed between the UI, the Database, and the Search modules using the `FileRecord` struct (defined in `include/core/database.h`). It contains optional fields that can default to NULL:
- `file_id`
- `file_name`, `file_path`, `extension`
- `created_date`, `last_modified`
- `embedding` (For Smart Search vectors)

### Directory Indexing
The `Indexer` class (`src/core/indexer.cpp`) uses standard C++17 `<filesystem>` to recursively crawl directories. 
- You can pass a directory path to `indexer.scanDirectory(path)`.
- It will automatically extract file metadata (like extensions and last modified dates), instantiate a `FileRecord`, and insert it into the SQLite database.
