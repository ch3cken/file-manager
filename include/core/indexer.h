#pragma once

#include "core/database.h"
#include <string>

class Indexer {
private:
    DatabaseManager& db;

public:
    Indexer(DatabaseManager& database);
    
    // Scans a directory and all its subdirectories, adding files to the database
    void scanDirectory(const std::string& directoryPath);
};
