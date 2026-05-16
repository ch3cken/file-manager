#pragma once

#include "core/sqlite3.h"
#include <string>

#include <vector>

struct FileRecord {
    int file_id;
    std::string file_name;
    std::string file_path;
    std::string last_modified;
};

class DatabaseManager {
private:
    sqlite3* db;
    void initializeTables();

public:
    DatabaseManager(const std::string& dbPath);
    ~DatabaseManager();

    // Quick Search Core API
    bool insertFile(const std::string& name, const std::string& path, const std::string& modified_date);
    bool addTagToFile(int file_id, const std::string& tag);
    std::vector<FileRecord> quickSearch(const std::string& keyword);
};
