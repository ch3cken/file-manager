#pragma once

#include "core/sqlite3.h"
#include <string>

#include <vector>

struct FileRecord {
    int file_id = 0;
    std::string file_name;
    std::string file_path;
    std::string extension;
    std::string created_date;
    std::string last_modified;
    std::vector<float> embedding; // Vector embedding for Smart Search
};

class DatabaseManager {
private:
    sqlite3* db;
    void initializeTables();

public:
    DatabaseManager(const std::string& dbPath);
    ~DatabaseManager();

    // Quick Search & Smart Search Core API
    bool insertFile(const FileRecord& record);
    bool addTagToFile(int file_id, const std::string& tag);
    
    // Allow other modules to execute their own queries
    sqlite3* getDb() const { return db; }
};
