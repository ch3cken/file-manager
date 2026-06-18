#include "core/database.h"
#include "search/smart_search.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

static const char* TEST_DB = "test_smart_search_cases.db";

void cleanUp() {
    std::remove(TEST_DB);
    std::remove((std::string(TEST_DB) + "-wal").c_str());
    std::remove((std::string(TEST_DB) + "-shm").c_str());
}

FileRecord makeRecord(const std::string& name,
                      const std::string& path,
                      const std::vector<float>& embedding) {
    FileRecord record;
    record.file_name = name;
    record.file_path = path;
    record.extension = ".txt";
    record.last_modified = "2026-05-29 00:00:00";
    record.embedding = embedding;
    return record;
}

void test_ReturnsNearestEmbeddingFirst() {
    cleanUp();
    DatabaseManager db(TEST_DB);
    db.insertFile(makeRecord("machine_learning_notes.txt", "C:/docs/ml.txt", {1.0F, 0.0F, 0.0F}));
    db.insertFile(makeRecord("recipe.txt", "C:/docs/recipe.txt", {0.0F, 1.0F, 0.0F}));
    db.insertFile(makeRecord("travel.txt", "C:/docs/travel.txt", {0.0F, 0.0F, 1.0F}));

    SmartSearch search(db);
    const auto results = search.searchByEmbedding({0.9F, 0.1F, 0.0F}, 2);

    assert(results.size() == 2);
    assert(results[0].record.file_name == "machine_learning_notes.txt");
    assert(results[0].score > results[1].score);
    std::cout << "[PASS] test_ReturnsNearestEmbeddingFirst\n";
}

void test_SkipsRowsWithoutCompatibleEmbeddings() {
    cleanUp();
    DatabaseManager db(TEST_DB);
    db.insertFile(makeRecord("good.txt", "C:/docs/good.txt", {0.0F, 1.0F}));
    db.insertFile(makeRecord("wrong_dimension.txt", "C:/docs/wrong.txt", {1.0F, 0.0F, 0.0F}));

    FileRecord missing;
    missing.file_name = "missing.txt";
    missing.file_path = "C:/docs/missing.txt";
    db.insertFile(missing);

    SmartSearch search(db);
    const auto results = search.searchByEmbedding({0.0F, 1.0F}, 10);

    assert(results.size() == 1);
    assert(results[0].record.file_name == "good.txt");
    std::cout << "[PASS] test_SkipsRowsWithoutCompatibleEmbeddings\n";
}

void test_TopKLimit() {
    cleanUp();
    DatabaseManager db(TEST_DB);
    db.insertFile(makeRecord("one.txt", "C:/docs/one.txt", {1.0F, 0.0F}));
    db.insertFile(makeRecord("two.txt", "C:/docs/two.txt", {0.8F, 0.2F}));

    SmartSearch search(db);
    const auto results = search.searchByEmbedding({1.0F, 0.0F}, 1);

    assert(results.size() == 1);
    assert(results[0].record.file_name == "one.txt");
    std::cout << "[PASS] test_TopKLimit\n";
}

void test_SearchByEmbeddingAppliesParsedFilters() {
    cleanUp();
    DatabaseManager db(TEST_DB);
    db.insertFile(makeRecord("recent_paper.pdf", "C:/docs/recent_paper.pdf", {1.0F, 0.0F}));
    db.insertFile(makeRecord("recent_photo.png", "C:/docs/recent_photo.png", {1.0F, 0.0F}));

    sqlite3_exec(db.getDb(),
        "UPDATE files SET extension = '.pdf', last_modified = '2026-05-29 08:00:00' WHERE file_name = 'recent_paper.pdf';",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db.getDb(),
        "UPDATE files SET extension = '.png', last_modified = '2026-05-29 08:00:00' WHERE file_name = 'recent_photo.png';",
        nullptr, nullptr, nullptr);

    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 5 - 1;
    tm.tm_mday = 30;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    const auto now = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    const auto parsed = search::parseSearchQuery("paper yesterday", now);

    SmartSearch search(db);
    const auto results = search.searchByEmbedding({1.0F, 0.0F}, parsed, 10);

    assert(results.size() == 1);
    assert(results[0].record.file_name == "recent_paper.pdf");
    std::cout << "[PASS] test_SearchByEmbeddingAppliesParsedFilters\n";
}

void test_LexicalSmartSearchUsesTags() {
    cleanUp();
    DatabaseManager db(TEST_DB);
    FileRecord record;
    record.file_name = "lecture01.pdf";
    record.file_path = "C:/docs/lecture01.pdf";
    record.extension = ".pdf";
    record.last_modified = "2026-05-29 08:00:00";
    db.insertFile(record);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db.getDb(),
        "SELECT file_id FROM files WHERE file_path = 'C:/docs/lecture01.pdf';",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int fileId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    db.addTagToFile(fileId, "subject:machine learning");
    db.addTagToFile(fileId, "document_type:paper");

    SmartSearch smartSearch(db);
    const auto parsed = search::parseSearchQuery("machine learning paper");
    const auto results = smartSearch.searchLexically(parsed, 10);

    assert(!results.empty());
    assert(results[0].record.file_name == "lecture01.pdf");
    assert(results[0].score > 0.0F);
    std::cout << "[PASS] test_LexicalSmartSearchUsesTags\n";
}

int main() {
    std::cout << "=== Smart Search Tests ===\n";
    test_ReturnsNearestEmbeddingFirst();
    test_SkipsRowsWithoutCompatibleEmbeddings();
    test_TopKLimit();
    test_SearchByEmbeddingAppliesParsedFilters();
    test_LexicalSmartSearchUsesTags();
    cleanUp();
    std::cout << "All smart search tests passed.\n";
    return 0;
}
