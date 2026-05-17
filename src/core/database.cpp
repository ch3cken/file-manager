#include "core/database.h"
#include <iostream>

DatabaseManager::DatabaseManager(const std::string& dbPath) {
    // The SRS requires DB to be saved locally
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
    } else {
        std::cout << "Opened database successfully at " << dbPath << std::endl;
        initializeTables();
    }
}

DatabaseManager::~DatabaseManager() {
    sqlite3_close(db);
}

void DatabaseManager::initializeTables() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS files (
            file_id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_name TEXT,
            file_path TEXT UNIQUE,
            extension TEXT,
            created_date DATETIME,
            last_modified DATETIME,
            embedding BLOB
        );
        CREATE TABLE IF NOT EXISTS tags (
            tag_id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id INTEGER,
            tag_name TEXT,
            FOREIGN KEY(file_id) REFERENCES files(file_id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_file_name ON files(file_name);
        CREATE INDEX IF NOT EXISTS idx_last_modified ON files(last_modified);
        CREATE INDEX IF NOT EXISTS idx_tag_name ON tags(tag_name);
    )";
    
    char* errMsg = 0;
    int rc = sqlite3_exec(db, schema, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

bool DatabaseManager::insertFile(const FileRecord& record) {
    const char* sql = "INSERT OR REPLACE INTO files (file_name, file_path, extension, created_date, last_modified, embedding) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, record.file_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.file_path.c_str(), -1, SQLITE_TRANSIENT);
    
    if (record.extension.empty()) {
        sqlite3_bind_null(stmt, 3);
    } else {
        sqlite3_bind_text(stmt, 3, record.extension.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    if (record.created_date.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        sqlite3_bind_text(stmt, 4, record.created_date.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    sqlite3_bind_text(stmt, 5, record.last_modified.c_str(), -1, SQLITE_TRANSIENT);
    
    if (record.embedding.empty()) {
        sqlite3_bind_null(stmt, 6);
    } else {
        sqlite3_bind_blob(stmt, 6, record.embedding.data(), record.embedding.size() * sizeof(float), SQLITE_TRANSIENT);
    }
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::addTagToFile(int file_id, const std::string& tag) {
    const char* sql = "INSERT INTO tags (file_id, tag_name) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, file_id);
    sqlite3_bind_text(stmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::vector<FileRecord> DatabaseManager::quickSearch(const std::string& keyword) {
    std::vector<FileRecord> results;
    
    // Quick Search matches either the file_name or any tags associated with the file
    const char* sql = R"(
        SELECT DISTINCT f.file_id, f.file_name, f.file_path, f.extension, f.created_date, f.last_modified 
        FROM files f 
        LEFT JOIN tags t ON f.file_id = t.file_id 
        WHERE f.file_name LIKE ? OR t.tag_name LIKE ?
        LIMIT 100;
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare search statement: " << sqlite3_errmsg(db) << std::endl;
        return results;
    }
    
    std::string search_term = "%" + keyword + "%";
    sqlite3_bind_text(stmt, 1, search_term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, search_term.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord record;
        record.file_id = sqlite3_column_int(stmt, 0);
        record.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        record.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* ext = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (ext) record.extension = ext;
        
        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (created) record.created_date = created;
        
        record.last_modified = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        
        // Note: We don't fetch embeddings in Quick Search to save memory/time.
        
        results.push_back(record);
    }
    
    sqlite3_finalize(stmt);
    return results;
}