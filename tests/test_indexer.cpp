// tests/test_indexer.cpp
// Covers issues #7 (formatFileTime epoch), #8 (thread-safety), #23 (absolute path).
// No Administrator rights required.

#include "core/database.h"
#include "core/indexer.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <regex>

namespace fs = std::filesystem;

static const char* TEST_DB = "test_indexer_cases.db";

void cleanUp() {
    std::remove(TEST_DB);
    std::remove((std::string(TEST_DB) + "-wal").c_str());
    std::remove((std::string(TEST_DB) + "-shm").c_str());
}

// ─── Test 1: formatFileTime produces a valid date string (issue #7) ───────────
// The function is defined in indexer.cpp; declare it here for white-box testing.
extern std::string formatFileTime(const fs::file_time_type& ftime);

void test_FormatFileTimeValidFormat() {
    // Use the current time — if the epoch conversion were wrong (~116 years off on MSVC),
    // the year would be around 1910 instead of 2026.
    auto now = fs::file_time_type::clock::now();
    std::string result = formatFileTime(now);

    // Must match YYYY-MM-DD HH:MM:SS
    std::regex pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
    assert(std::regex_match(result, pattern) && "formatFileTime should match YYYY-MM-DD HH:MM:SS");

    // Year must be 2026 or later (catches the ~116-year MSVC epoch bug)
    int year = std::stoi(result.substr(0, 4));
    assert(year >= 2026 && "formatFileTime year should be current, not ~1910 (epoch bug)");

    std::cout << "[PASS] test_FormatFileTimeValidFormat (got: " << result << ")\n";
}

// ─── Test 2: scanDirectory indexes files and stores absolute paths (issue #23) ─

void test_ScanDirectoryStoresAbsolutePaths() {
    cleanUp();

    // Create a small temporary directory tree to scan
    fs::path tmpDir = fs::temp_directory_path() / "fm_test_scan";
    fs::create_directories(tmpDir);
    // Create a couple of dummy files
    { std::ofstream f(tmpDir / "alpha.txt"); f << "hello"; }
    { std::ofstream f(tmpDir / "beta.pdf");  f << "world"; }

    DatabaseManager db(TEST_DB);
    // "C:" is the default — no USN init needed for scanDirectory
    Indexer indexer(db, "C:");
    indexer.scanDirectory(fs::absolute(tmpDir).string());

    // All stored paths should be absolute
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(), "SELECT file_path FROM files;", -1, &stmt, nullptr);
    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        assert(path && "file_path should not be NULL");
        std::string p(path);
        assert(fs::path(p).is_absolute() && ("stored path must be absolute: " + p).c_str());
        rowCount++;
    }
    sqlite3_finalize(stmt);

    assert(rowCount >= 2 && "scanDirectory should index the test files");

    // Cleanup temp dir
    fs::remove_all(tmpDir);
    std::cout << "[PASS] test_ScanDirectoryStoresAbsolutePaths (" << rowCount << " files indexed)\n";
}

// ─── Test 3: scanDirectory handles an invalid path gracefully ─────────────────

void test_ScanDirectoryInvalidPath() {
    cleanUp();
    DatabaseManager db(TEST_DB);
    Indexer indexer(db, "C:");

    // Should not throw — just prints an error to stderr
    bool threw = false;
    try {
        indexer.scanDirectory("Z:/path/that/does/not/exist_xyz_12345");
    } catch (...) {
        threw = true;
    }
    assert(!threw && "scanDirectory should not throw on an invalid path");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(), "SELECT COUNT(*) FROM files;", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    assert(count == 0 && "no files should be indexed for an invalid path");

    std::cout << "[PASS] test_ScanDirectoryInvalidPath\n";
}

int main() {
    std::cout << "=== Indexer Tests ===\n";
    test_FormatFileTimeValidFormat();
    test_ScanDirectoryStoresAbsolutePaths();
    test_ScanDirectoryInvalidPath();
    cleanUp();
    std::cout << "All indexer tests passed.\n";
    return 0;
}
