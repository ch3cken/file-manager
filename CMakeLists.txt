cmake_minimum_required(VERSION 3.15)
project(FileManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SQLite3 package provided by vcpkg
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# Core application (Database setup and Indexing)
add_executable(file_manager_core src/core/main.cpp src/core/database.cpp src/core/indexer.cpp)
target_include_directories(file_manager_core PRIVATE include)
target_link_libraries(file_manager_core PRIVATE unofficial::sqlite3::sqlite3)

# Test executable for Quick Search
add_executable(test_quick_search src/search/test.cpp src/core/database.cpp src/search/quick_search.cpp)
target_include_directories(test_quick_search PRIVATE include)
target_link_libraries(test_quick_search PRIVATE unofficial::sqlite3::sqlite3)