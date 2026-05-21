#pragma once

#include "core/database.h"
#include <string>
#include <memory>

class UsnJournal;

class Indexer {
private:
    DatabaseManager& db;
    std::unique_ptr<UsnJournal> journal;
    bool usingUsn;

public:
    Indexer(DatabaseManager& database, const std::string& driveLetter = "C:");
    ~Indexer();
    
    // Initial static scan of a directory to build the baseline
    void scanDirectory(const std::string& directoryPath);
    
    // Polls for real-time changes
    void update();
};
