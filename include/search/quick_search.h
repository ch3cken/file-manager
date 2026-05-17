#pragma once

#include "core/database.h"
#include <vector>
#include <string>

class QuickSearch {
private:
    DatabaseManager& db;

public:
    QuickSearch(DatabaseManager& database);

    // Executes a quick search query matching filenames or tags
    std::vector<FileRecord> search(const std::string& keyword);
};
