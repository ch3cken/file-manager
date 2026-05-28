cmake_minimum_required(VERSION 3.15)
project(FileManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies provided by vcpkg.
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(libzip CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(pugixml CONFIG REQUIRED)

pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

enable_testing()

# ─── Header-only categorization module ──────────────────────────────────────
add_library(file_manager_categorization INTERFACE)
add_library(file_manager::categorization ALIAS file_manager_categorization)
target_include_directories(file_manager_categorization INTERFACE include)
target_link_libraries(file_manager_categorization INTERFACE
    unofficial::sqlite3::sqlite3
    ICU::uc
    ICU::i18n
    libzip::zip
    PkgConfig::POPPLER_CPP
    pugixml::pugixml
)

# ─── Core application ────────────────────────────────────────────────────────
add_executable(file_manager_core
    src/core/main.cpp
    src/core/database.cpp
    src/core/indexer.cpp
    src/core/usn_journal.cpp
)
target_include_directories(file_manager_core PRIVATE include)
target_link_libraries(file_manager_core PRIVATE unofficial::sqlite3::sqlite3)

# ─── Test: Database (issues #2, #3, #4) ──────────────────────────────────────
add_executable(test_database
    tests/test_database.cpp
    src/core/database.cpp
)
target_include_directories(test_database PRIVATE include)
target_link_libraries(test_database PRIVATE unofficial::sqlite3::sqlite3)

# ─── Test: Quick Search (issues #19, #20, #21) ───────────────────────────────
add_executable(test_quick_search
    tests/test_quick_search.cpp
    src/core/database.cpp
    src/search/quick_search.cpp
    src/search/embedder.cpp
)
target_include_directories(test_quick_search PRIVATE include)
target_link_libraries(test_quick_search PRIVATE unofficial::sqlite3::sqlite3 onnxruntime::onnxruntime)

# ─── Test: Indexer (issues #7, #8, #23) ─────────────────────────────────────
add_executable(test_indexer
    tests/test_indexer.cpp
    src/core/database.cpp
    src/core/indexer.cpp
    src/core/usn_journal.cpp
)
target_include_directories(test_indexer PRIVATE include)
target_link_libraries(test_indexer PRIVATE unofficial::sqlite3::sqlite3)

# ─── Test: USN Journal (issues #10–#18) ──────────────────────────────────────
add_executable(test_usn_journal
    tests/test_usn_journal.cpp
    src/core/database.cpp
    src/core/indexer.cpp
    src/core/usn_journal.cpp
)
target_include_directories(test_usn_journal PRIVATE include)
target_link_libraries(test_usn_journal PRIVATE unofficial::sqlite3::sqlite3)

# ─── Test: NLP Embedder (Antonio) ────────────────────────────────────────────
add_executable(test_embedder
    tests/test_embedder.cpp
    src/search/embedder.cpp
)
target_include_directories(test_embedder PRIVATE include)
target_link_libraries(test_embedder PRIVATE onnxruntime::onnxruntime)
add_test(NAME EmbedderTest COMMAND test_embedder)
