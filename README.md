# File Manager
(20230866) Antonio Recuero Buleje  
(20220825) Enrique Jose Delgado Garcia  
(20230951) Elmoursi Ahmad  
(20230959) Murad Ibrahimov  

This repo is the implementation of Team 12's File Manager, by Team 7.

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

### Database Initialization
The local SQLite database backend is automatically initialized in the code. Upon running the executable for the first time, the `DatabaseManager` (in `src/core/database.cpp`) will:
1. Create the database file at the specified location.
2. Run the `CREATE TABLE IF NOT EXISTS` commands to ensure all necessary tables (like the `files` table for Quick Search) are created. 

There is no need for team members to install a separate SQL server or run external SQL scripts!
