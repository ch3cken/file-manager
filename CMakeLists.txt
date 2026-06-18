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
find_package(nlohmann_json CONFIG REQUIRED)

pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

# The vcpkg Debug ONNX Runtime package can trip ONNX schema debug assertions on
# model load. Use the Release runtime DLL/import library for all MSVC configs;
# ONNX Runtime is a shared library boundary, so this keeps Debug app builds
# usable without changing our own debug symbols.
if(WIN32 AND TARGET onnxruntime::onnxruntime)
    get_target_property(ORT_RELEASE_IMPLIB onnxruntime::onnxruntime IMPORTED_IMPLIB_RELEASE)
    get_target_property(ORT_RELEASE_LOCATION onnxruntime::onnxruntime IMPORTED_LOCATION_RELEASE)
    if(ORT_RELEASE_IMPLIB AND ORT_RELEASE_LOCATION)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${ORT_RELEASE_IMPLIB}"
            IMPORTED_LOCATION_DEBUG "${ORT_RELEASE_LOCATION}"
        )
    endif()
endif()

enable_testing()

function(copy_onnxruntime_runtime target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:onnxruntime::onnxruntime>
                $<TARGET_FILE_DIR:${target_name}>
        )
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(ORT_RUNTIME_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
            foreach(ORT_DLL
                    onnxruntime_providers_shared.dll
                    abseil_dll.dll
                    libprotobuf.dll
                    libprotobuf-lite.dll
                    re2.dll)
                if(EXISTS "${ORT_RUNTIME_DIR}/${ORT_DLL}")
                    add_custom_command(TARGET ${target_name} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${ORT_RUNTIME_DIR}/${ORT_DLL}"
                            $<TARGET_FILE_DIR:${target_name}>
                    )
                endif()
            endforeach()
        endif()
    endif()
endfunction()

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

add_library(file_manager_config
    src/config/app_config.cpp
)
add_library(file_manager::config ALIAS file_manager_config)
target_include_directories(file_manager_config PUBLIC include)
target_link_libraries(file_manager_config PUBLIC nlohmann_json::nlohmann_json)

# ─── Core application ────────────────────────────────────────────────────────
add_executable(file_manager_core
    src/core/main.cpp
    src/core/database.cpp
    src/core/indexer.cpp
    src/core/usn_journal.cpp
)
target_include_directories(file_manager_core PRIVATE include)
target_link_libraries(file_manager_core PRIVATE unofficial::sqlite3::sqlite3 file_manager::config)

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
    src/search/query_parser.cpp
)
target_include_directories(test_quick_search PRIVATE include)
target_link_libraries(test_quick_search PRIVATE unofficial::sqlite3::sqlite3)

# Basic Smart Search CLI: optionally indexes a directory with embeddings and
# ranks files by cosine similarity to a prompt.
add_executable(smart_search_cli
    src/search/smart_search_cli.cpp
    src/search/smart_search.cpp
    src/search/query_parser.cpp
    src/search/embedder.cpp
    src/core/database.cpp
    src/core/indexer.cpp
    src/core/indexer_embeddings.cpp
    src/core/usn_journal.cpp
)
target_include_directories(smart_search_cli PRIVATE include)
target_link_libraries(smart_search_cli PRIVATE unofficial::sqlite3::sqlite3 onnxruntime::onnxruntime)
if(WIN32)
    target_link_libraries(smart_search_cli PRIVATE Shell32)
endif()
copy_onnxruntime_runtime(smart_search_cli)

if(WIN32)
    add_executable(file_manager_overlay WIN32
        src/ui/overlay_ui.cpp
        src/search/quick_search.cpp
        src/search/smart_search.cpp
        src/search/query_parser.cpp
        src/search/embedder.cpp
        src/core/database.cpp
    )
    target_include_directories(file_manager_overlay PRIVATE include)
    target_link_libraries(file_manager_overlay PRIVATE
        unofficial::sqlite3::sqlite3
        onnxruntime::onnxruntime
        file_manager::config
        Shell32
        User32
        Gdi32
    )
    copy_onnxruntime_runtime(file_manager_overlay)
endif()

# Test: Smart Search ranking over stored embedding BLOBs.
add_executable(test_smart_search
    tests/test_smart_search.cpp
    src/search/smart_search.cpp
    src/search/query_parser.cpp
    src/search/embedder.cpp
    src/core/database.cpp
)
target_include_directories(test_smart_search PRIVATE include)
target_link_libraries(test_smart_search PRIVATE unofficial::sqlite3::sqlite3 onnxruntime::onnxruntime)
copy_onnxruntime_runtime(test_smart_search)
add_test(NAME SmartSearchTest COMMAND test_smart_search)

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
copy_onnxruntime_runtime(test_embedder)
add_test(NAME EmbedderTest COMMAND test_embedder)

add_executable(test_search_query_parser
    tests/test_search_query_parser.cpp
    src/search/query_parser.cpp
)
target_include_directories(test_search_query_parser PRIVATE include)
add_test(NAME SearchQueryParserTest COMMAND test_search_query_parser)

add_executable(test_app_config
    tests/test_app_config.cpp
)
target_link_libraries(test_app_config PRIVATE file_manager::config)
add_test(NAME AppConfigTest COMMAND test_app_config)

# ─── Test: Categorization ───────────────────────────────────────────────────
add_executable(test_categorization
    tests/test_categorization.cpp
)
target_link_libraries(test_categorization PRIVATE file_manager::categorization)
add_test(NAME CategorizationTest COMMAND test_categorization)
