// tests/test_database.cpp
// Covers issues #2 (PRAGMA foreign_keys), #3 (null last_modified), #4 (WAL mode).
// Build with:
//   add_executable(test_database tests/test_database.cpp src/core/database.cpp src/core/sqlite3.c)
//   target_include_directories(test_database PRIVATE include)
//
// Run without Administrator rights — no USN access needed.

#include "core/database.h"
#include <cassert>
#include <iostream>
#include <cstdio>      // std::remove
#include <string>

// ─── helpers ─────────────────────────────────────────────────────────────────

static const char* TEST_DB = "test_database_cases.db";

void cleanUp() {
    std::remove(TEST_DB);
    // WAL mode creates two sidecar files
    std::remove((std::string(TEST_DB) + "-wal").c_str());
    std::remove((std::string(TEST_DB) + "-shm").c_str());
}

// ─── Test 1: DB opens and schema is created ───────────────────────────────────

void test_DbOpensAndSchemaCreated() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    // Verify tables exist by running a harmless SELECT
    sqlite3* raw = db.getDb();
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM files;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK && "files table should exist after init");
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM tags;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK && "tags table should exist after init");
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    std::cout << "[PASS] test_DbOpensAndSchemaCreated\n";
}

// ─── Test 2: WAL journal mode is active (issue #4) ────────────────────────────

void test_WalModeEnabled() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    sqlite3* raw = db.getDb();
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(raw, "PRAGMA journal_mode;", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    std::string mode(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);

    assert(mode == "wal" && "journal_mode should be WAL after initializeTables");
    std::cout << "[PASS] test_WalModeEnabled\n";
}

// ─── Test 3: foreign keys are ON (issue #2) ───────────────────────────────────

void test_ForeignKeysEnabled() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    sqlite3* raw = db.getDb();
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(raw, "PRAGMA foreign_keys;", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int fk = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    assert(fk == 1 && "foreign_keys PRAGMA should be ON");
    std::cout << "[PASS] test_ForeignKeysEnabled\n";
}

// ─── Test 4: insertFile with all fields populated ────────────────────────────

void test_InsertFileFullRecord() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "report.pdf";
    r.file_path    = "C:/Users/test/report.pdf";
    r.extension    = ".pdf";
    r.created_date = "2026-01-01 10:00:00";
    r.last_modified= "2026-05-20 08:30:00";

    bool ok = db.insertFile(r);
    assert(ok && "insertFile should succeed with all fields");

    // Verify row is present
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(), "SELECT file_name FROM files WHERE file_path = ?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, r.file_path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW && "inserted row should be queryable");
    std::string name(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    assert(name == "report.pdf");
    sqlite3_finalize(stmt);

    std::cout << "[PASS] test_InsertFileFullRecord\n";
}

// ─── Test 5: insertFile with NULL last_modified (issue #3) ────────────────────

void test_InsertFileNullLastModified() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name = "no_date.txt";
    r.file_path = "C:/Users/test/no_date.txt";
    // last_modified intentionally left empty → should bind NULL, not crash

    bool ok = db.insertFile(r);
    assert(ok && "insertFile should succeed even when last_modified is empty");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(),
        "SELECT last_modified FROM files WHERE file_name = 'no_date.txt';",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int colType = sqlite3_column_type(stmt, 0);
    sqlite3_finalize(stmt);

    assert(colType == SQLITE_NULL && "empty last_modified should be stored as SQL NULL");
    std::cout << "[PASS] test_InsertFileNullLastModified\n";
}

// ─── Test 6: ON DELETE CASCADE removes tags (issue #2) ────────────────────────

void test_TagsCascadeDeletedWithFile() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "cascade_test.txt";
    r.file_path    = "C:/test/cascade_test.txt";
    r.last_modified= "2026-05-20 12:00:00";
    db.insertFile(r);

    // Retrieve auto-generated file_id
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(),
        "SELECT file_id FROM files WHERE file_path = 'C:/test/cascade_test.txt';",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int fileId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    db.addTagToFile(fileId, "homework");
    db.addTagToFile(fileId, "SE");

    // Confirm tags were inserted
    sqlite3_prepare_v2(db.getDb(),
        "SELECT COUNT(*) FROM tags WHERE file_id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, fileId);
    sqlite3_step(stmt);
    int tagCount = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    assert(tagCount == 2 && "two tags should exist before deletion");

    // Delete the file
    sqlite3_exec(db.getDb(),
        "DELETE FROM files WHERE file_path = 'C:/test/cascade_test.txt';",
        nullptr, nullptr, nullptr);

    // Tags should have cascaded away
    sqlite3_prepare_v2(db.getDb(),
        "SELECT COUNT(*) FROM tags WHERE file_id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, fileId);
    sqlite3_step(stmt);
    int remaining = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    assert(remaining == 0 && "ON DELETE CASCADE should remove tags when file is deleted");
    std::cout << "[PASS] test_TagsCascadeDeletedWithFile\n";
}

// ─── Test 7: INSERT OR REPLACE upserts without duplicates ────────────────────

void test_InsertOrReplaceUpserts() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name    = "file.txt";
    r.file_path    = "C:/test/file.txt";
    r.last_modified= "2026-01-01 00:00:00";
    db.insertFile(r);

    // Re-insert the same path with updated last_modified
    r.last_modified = "2026-06-01 12:00:00";
    db.insertFile(r);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(), "SELECT COUNT(*) FROM files WHERE file_path = ?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, r.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    assert(count == 1 && "INSERT OR REPLACE should not create duplicate rows");
    std::cout << "[PASS] test_InsertOrReplaceUpserts\n";
}

void test_UpsertPreservesTagsAndEmbedding() {
    cleanUp();
    DatabaseManager db(TEST_DB);

    FileRecord r;
    r.file_name = "semantic.txt";
    r.file_path = "C:/test/semantic.txt";
    r.extension = ".txt";
    r.last_modified = "2026-01-01 00:00:00";
    r.embedding = {1.0F, 0.0F};
    db.insertFile(r);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(),
        "SELECT file_id FROM files WHERE file_path = 'C:/test/semantic.txt';",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int fileId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    db.addTagToFile(fileId, "keyword:important");

    FileRecord updated;
    updated.file_name = "semantic.txt";
    updated.file_path = "C:/test/semantic.txt";
    updated.extension = ".txt";
    updated.last_modified = "2026-02-01 00:00:00";
    assert(db.insertFile(updated));

    sqlite3_prepare_v2(db.getDb(),
        "SELECT file_id, embedding IS NOT NULL FROM files WHERE file_path = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, updated.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int updatedFileId = sqlite3_column_int(stmt, 0);
    int hasEmbedding = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db.getDb(),
        "SELECT COUNT(*) FROM tags WHERE file_id = ? AND tag_name = 'keyword:important';",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, updatedFileId);
    sqlite3_step(stmt);
    int tagCount = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    assert(updatedFileId == fileId && "upsert should preserve file_id");
    assert(hasEmbedding == 1 && "metadata-only upsert should preserve existing embedding");
    assert(tagCount == 1 && "upsert should preserve existing tags");
    std::cout << "[PASS] test_UpsertPreservesTagsAndEmbedding\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Database Tests ===\n";
    test_DbOpensAndSchemaCreated();
    test_WalModeEnabled();
    test_ForeignKeysEnabled();
    test_InsertFileFullRecord();
    test_InsertFileNullLastModified();
    test_TagsCascadeDeletedWithFile();
    test_InsertOrReplaceUpserts();
    test_UpsertPreservesTagsAndEmbedding();
    cleanUp();
    std::cout << "All database tests passed.\n";
    return 0;
}
