#pragma once

#include "core/database.h"
#include <cstddef>
#include <string>
#include <memory>

class UsnJournal;
namespace nlp {
class Embedder;
}

class Indexer {
private:
    DatabaseManager& db;
    std::unique_ptr<UsnJournal> journal;
    bool usingUsn;

public:
    Indexer(DatabaseManager& database, const std::string& driveLetter = "C:", bool enableUsn = true);
    ~Indexer();
    
    // Initial static scan of a directory to build the baseline
    void scanDirectory(const std::string& directoryPath);

    // Static scan that also stores semantic embeddings for Smart Search.
    void scanDirectoryWithEmbeddings(const std::string& directoryPath,
                                     const nlp::Embedder& embedder,
                                     std::size_t maxContentEmbeddings = 500);
    
    // Polls for real-time changes
    void update();
};
