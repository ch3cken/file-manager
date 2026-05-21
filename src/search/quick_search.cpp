#include "search/quick_search.h"
#include <iostream>

QuickSearch::QuickSearch(DatabaseManager& database) : db(database) {}

std::vector<FileRecord> QuickSearch::search(const std::string& keyword) {
    std::vector<FileRecord> results;
    sqlite3* sqlite_db = db.getDb();
    
    // Quick Search matches either the file_name or any tags associated with the file
    const char* sql = R"(
        SELECT DISTINCT f.file_id, f.file_name, f.file_path, f.extension, f.created_date, f.last_modified 
        FROM files f 
        LEFT JOIN tags t ON f.file_id = t.file_id 
        WHERE f.file_name LIKE ? OR t.tag_name LIKE ?
        LIMIT 100;
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(sqlite_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare search statement: " << sqlite3_errmsg(sqlite_db) << std::endl;
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
        
        const char* lm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (lm) record.last_modified = lm;
        
        results.push_back(record);
    }
    
    sqlite3_finalize(stmt);
    return results;
}
