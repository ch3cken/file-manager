cmake_minimum_required(VERSION 3.15)
project(FileManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SQLite3 package provided by vcpkg
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# Assuming your core library or executable is named file_manager_core
add_executable(file_manager_core src/core/main.cpp src/core/database.cpp src/core/indexer.cpp)

# Add include directories
target_include_directories(file_manager_core PRIVATE include)

# Link the library
target_link_libraries(file_manager_core PRIVATE unofficial::sqlite3::sqlite3)