#pragma once

#include "core/database.h"
#include "search/query_parser.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nlp {
class Embedder;
}

struct SmartSearchResult {
    FileRecord record;
    float score = 0.0F;
};

class SmartSearch {
private:
    DatabaseManager& db;

    std::vector<SmartSearchResult> searchLexically(const std::string& prompt,
                                                   std::size_t topK) const;

public:
    explicit SmartSearch(DatabaseManager& database);

    // Embeds the prompt with the configured NLP model, then ranks stored files.
    std::vector<SmartSearchResult> search(const std::string& prompt,
                                          const nlp::Embedder& embedder,
                                          std::size_t topK = 10) const;

    // Testable core ranking path. The query vector should match stored embedding dimensions.
    std::vector<SmartSearchResult> searchByEmbedding(const std::vector<float>& queryEmbedding,
                                                     std::size_t topK = 10) const;

    std::vector<SmartSearchResult> searchByEmbedding(const std::vector<float>& queryEmbedding,
                                                     const search::ParsedSearchQuery& query,
                                                     std::size_t topK = 10) const;

    std::vector<SmartSearchResult> searchLexically(const search::ParsedSearchQuery& query,
                                                   std::size_t topK = 10) const;
};
