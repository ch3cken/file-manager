// tests/test_quick_search.cpp
// Covers issues #19 (null last_modified crash), #20 (basic search contract),
// and #21 (LIKE with Unicode note).
// No Administrator rights required.

#include "core/database.h"
#include "search/quick_search.h"
#include <cassert>
#include <iostream>
#include <cstdio>

static const char* TEST_DB = "test_qs_cases.db";

void cleanUp() {
    std::remove(TEST_DB);
    std::remove((std::string(TEST_DB) + "-wal").c_str());
    std::remove((std::string(TEST_DB) + "-shm").c_str());
}

// ─── Test 1: Search by filename keyword ──────────────────────────────────────

void test_SearchByFileName() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "software_engineering_notes.pdf";
    r.file_path    = "C:/docs/software_engineering_notes.pdf";
    r.extension    = ".pdf";
    r.last_modified= "2026-05-01 09:00:00";
    db.insertFile(r);

    QuickSearch qs(db);
    auto results = qs.search("engineering");

    assert(!results.empty() && "search by filename substring should return a result");
    assert(results[0].file_name == "software_engineering_notes.pdf");
    std::cout << "[PASS] test_SearchByFileName\n";
}

// ─── Test 2: Search by tag ────────────────────────────────────────────────────

void test_SearchByTag() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "lecture01.pptx";
    r.file_path    = "C:/docs/lecture01.pptx";
    r.last_modified= "2026-04-10 11:00:00";
    db.insertFile(r);

    // Get file_id of inserted record
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(),
        "SELECT file_id FROM files WHERE file_name = 'lecture01.pptx';",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int fileId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    db.addTagToFile(fileId, "machine-learning");

    QuickSearch qs(db);
    auto results = qs.search("machine-learning");

    assert(!results.empty() && "search by tag should return a result");
    assert(results[0].file_name == "lecture01.pptx");
    std::cout << "[PASS] test_SearchByTag\n";
}

// ─── Test 3: No results returns empty vector ──────────────────────────────────

void test_SearchNoResults() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "readme.txt";
    r.file_path    = "C:/readme.txt";
    r.last_modified= "2026-01-01 00:00:00";
    db.insertFile(r);

    QuickSearch qs(db);
    auto results = qs.search("zzz_unlikely_keyword_xyz");

    assert(results.empty() && "search with no match should return empty vector");
    std::cout << "[PASS] test_SearchNoResults\n";
}

// ─── Test 4: NULL last_modified does not crash (issue #19) ───────────────────

void test_SearchNullLastModifiedNoCrash() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    // Insert a file with no last_modified (will be stored as NULL)
    FileRecord r;
    r.file_name = "nodates.txt";
    r.file_path = "C:/nodates.txt";
    // last_modified left empty
    db.insertFile(r);

    QuickSearch qs(db);
    // Before fix this would crash via std::string(nullptr)
    std::vector<FileRecord> results;
    bool threw = false;
    try {
        results = qs.search("nodates");
    } catch (...) {
        threw = true;
    }

    assert(!threw && "search on record with NULL last_modified must not throw/crash");
    assert(!results.empty() && "the file should still be found");
    // last_modified should be empty string (default), not garbage
    assert(results[0].last_modified.empty() || results[0].last_modified == "");
    std::cout << "[PASS] test_SearchNullLastModifiedNoCrash\n";
}

// ─── Test 5: DISTINCT prevents duplicate results when multiple tags match ─────

void test_SearchDistinctResult() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "multi_tag.cpp";
    r.file_path    = "C:/src/multi_tag.cpp";
    r.last_modified= "2026-05-15 10:00:00";
    db.insertFile(r);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(),
        "SELECT file_id FROM files WHERE file_name = 'multi_tag.cpp';",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int fileId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    // Two tags that both match the keyword "cpp"
    db.addTagToFile(fileId, "cpp-project");
    db.addTagToFile(fileId, "cpp-homework");

    QuickSearch qs(db);
    auto results = qs.search("cpp");

    // SELECT DISTINCT ensures the file appears only once
    assert(results.size() == 1 && "DISTINCT should prevent duplicate results");
    std::cout << "[PASS] test_SearchDistinctResult\n";
}

int main() {
    std::cout << "=== Quick Search Tests ===\n";
    test_SearchByFileName();
    test_SearchByTag();
    test_SearchNoResults();
    test_SearchNullLastModifiedNoCrash();
    test_SearchDistinctResult();
    cleanUp();
    std::cout << "All quick search tests passed.\n";
    return 0;
}
