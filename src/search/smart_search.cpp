#include "search/smart_search.h"
#include "embedder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace {
std::string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    if (!text) {
        return {};
    }
    return reinterpret_cast<const char*>(text);
}

std::vector<float> embeddingFromBlob(sqlite3_stmt* stmt, int column) {
    const void* blob = sqlite3_column_blob(stmt, column);
    const int bytes = sqlite3_column_bytes(stmt, column);
    if (!blob || bytes <= 0 || bytes % static_cast<int>(sizeof(float)) != 0) {
        return {};
    }

    std::vector<float> values(static_cast<std::size_t>(bytes) / sizeof(float));
    std::memcpy(values.data(), blob, static_cast<std::size_t>(bytes));
    return values;
}

FileRecord recordFromStatement(sqlite3_stmt* stmt, bool includeEmbedding) {
    FileRecord record;
    record.file_id = sqlite3_column_int(stmt, 0);
    record.file_name = columnText(stmt, 1);
    record.file_path = columnText(stmt, 2);
    record.extension = columnText(stmt, 3);
    record.created_date = columnText(stmt, 4);
    record.last_modified = columnText(stmt, 5);
    if (includeEmbedding) {
        record.embedding = embeddingFromBlob(stmt, 6);
    }
    return record;
}

std::string lowerAscii(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::vector<std::string> queryTokens(std::string_view prompt) {
    std::vector<std::string> tokens;
    std::string current;
    const std::string lowered = lowerAscii(prompt);

    for (unsigned char ch : lowered) {
        if (std::isalnum(ch)) {
            current.push_back(static_cast<char>(ch));
        } else if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

float lexicalScore(const std::string& promptLower,
                   const std::vector<std::string>& tokens,
                   const FileRecord& record,
                   const std::string& tagsText = {}) {
    if (promptLower.empty() && tokens.empty()) {
        return 0.0F;
    }

    const std::string name = lowerAscii(record.file_name);
    const std::string haystack = name + " " +
                                 lowerAscii(record.file_path) + " " +
                                 lowerAscii(record.extension) + " " +
                                 lowerAscii(tagsText);

    float score = 0.0F;
    if (!promptLower.empty() && haystack.find(promptLower) != std::string::npos) {
        score = 0.85F;
    }
    if (!promptLower.empty() && name.find(promptLower) != std::string::npos) {
        score = 0.95F;
    }

    int matched = 0;
    int nameMatched = 0;
    for (const std::string& token : tokens) {
        if (haystack.find(token) != std::string::npos) {
            matched++;
        }
        if (name.find(token) != std::string::npos) {
            nameMatched++;
        }
    }

    if (matched > 0) {
        const float tokenCoverage = static_cast<float>(matched) / static_cast<float>(tokens.size());
        const float nameCoverage = static_cast<float>(nameMatched) / static_cast<float>(tokens.size());
        score = std::max(score, 0.15F + (0.65F * tokenCoverage) + (0.15F * nameCoverage));
    }

    return std::min(score, 0.99F);
}

void appendInClause(std::ostringstream& sql, const char* expression, std::size_t count) {
    sql << expression << " IN (";
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            sql << ", ";
        }
        sql << "?";
    }
    sql << ")";
}

void appendStructuredFilters(std::ostringstream& sql,
                             const search::ParsedSearchQuery& query,
                             bool& hasCondition) {
    auto appendConditionPrefix = [&]() {
        sql << (hasCondition ? " AND " : " WHERE ");
        hasCondition = true;
    };

    if (query.hasDateFilter()) {
        appendConditionPrefix();
        sql << "f.last_modified >= ? AND f.last_modified < ?";
    }
    if (query.hasExtensionFilter()) {
        appendConditionPrefix();
        appendInClause(sql, "LOWER(COALESCE(f.extension, ''))", query.extensions.size());
    }
}

void bindText(sqlite3_stmt* stmt, int& index, const std::string& value) {
    sqlite3_bind_text(stmt, index++, value.c_str(), -1, SQLITE_TRANSIENT);
}

void bindStructuredFilters(sqlite3_stmt* stmt,
                           int& bindIndex,
                           const search::ParsedSearchQuery& query) {
    if (query.hasDateFilter()) {
        bindText(stmt, bindIndex, query.modifiedRange.startInclusive);
        bindText(stmt, bindIndex, query.modifiedRange.endExclusive);
    }
    for (const auto& extension : query.extensions) {
        bindText(stmt, bindIndex, extension);
    }
}

float reflectedTagBoostFromText(const std::string& tagsText,
                                const std::vector<std::string>& reflectedTags) {
    if (tagsText.empty() || reflectedTags.empty()) {
        return 0.0F;
    }

    const std::string normalizedTags = " " + lowerAscii(tagsText) + " ";
    int matches = 0;
    for (const auto& tag : reflectedTags) {
        if (normalizedTags.find(" " + lowerAscii(tag) + " ") != std::string::npos) {
            ++matches;
        }
    }
    return std::min(0.20F, 0.05F * static_cast<float>(matches));
}

float reflectedTagBoostForFile(sqlite3* db,
                               int fileId,
                               const std::vector<std::string>& reflectedTags) {
    if (!db || fileId <= 0 || reflectedTags.empty()) {
        return 0.0F;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT tag_name FROM tags WHERE file_id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
        return 0.0F;
    }
    sqlite3_bind_int(stmt, 1, fileId);

    std::string tagsText;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tagsText += columnText(stmt, 0);
        tagsText.push_back(' ');
    }
    sqlite3_finalize(stmt);
    return reflectedTagBoostFromText(tagsText, reflectedTags);
}

float dotProduct(const std::vector<float>& a, const std::vector<float>& b) {
    float score = 0.0F;
    for (std::size_t i = 0; i < a.size(); ++i) {
        score += a[i] * b[i];
    }
    return score;
}

float l2Norm(const std::vector<float>& values) {
    float sum = 0.0F;
    for (float value : values) {
        sum += value * value;
    }
    return std::sqrt(sum);
}

bool isFiniteVector(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(), [](float value) {
        return std::isfinite(value);
    });
}

bool cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b, float& score) {
    if (a.empty() || a.size() != b.size() || !isFiniteVector(a) || !isFiniteVector(b)) {
        return false;
    }

    const float aNorm = l2Norm(a);
    const float bNorm = l2Norm(b);
    if (aNorm <= 0.0F || bNorm <= 0.0F) {
        return false;
    }

    score = dotProduct(a, b) / (aNorm * bNorm);
    return std::isfinite(score);
}
}

SmartSearch::SmartSearch(DatabaseManager& database) : db(database) {}

std::vector<SmartSearchResult> SmartSearch::search(const std::string& prompt,
                                                   const nlp::Embedder& embedder,
                                                   std::size_t topK) const {
    const search::ParsedSearchQuery parsed = search::parseSearchQuery(prompt);
    std::vector<SmartSearchResult> results = searchByEmbedding(embedder.embedText(parsed.semanticText.empty() ? prompt : parsed.semanticText),
                                                               parsed,
                                                               topK);
    std::vector<SmartSearchResult> lexicalResults = searchLexically(parsed, topK);

    std::unordered_map<std::string, std::size_t> resultByPath;
    for (std::size_t i = 0; i < results.size(); ++i) {
        resultByPath[results[i].record.file_path] = i;
    }

    for (SmartSearchResult& lexicalResult : lexicalResults) {
        const auto existing = resultByPath.find(lexicalResult.record.file_path);
        if (existing == resultByPath.end()) {
            resultByPath[lexicalResult.record.file_path] = results.size();
            results.push_back(std::move(lexicalResult));
        } else if (lexicalResult.score > results[existing->second].score) {
            results[existing->second].score = lexicalResult.score;
        }
    }

    std::sort(results.begin(), results.end(), [](const SmartSearchResult& lhs,
                                                 const SmartSearchResult& rhs) {
        return lhs.score > rhs.score;
    });
    if (results.size() > topK) {
        results.resize(topK);
    }
    return results;
}

std::vector<SmartSearchResult> SmartSearch::searchLexically(const std::string& prompt,
                                                            std::size_t topK) const {
    return searchLexically(search::parseSearchQuery(prompt), topK);
}

std::vector<SmartSearchResult> SmartSearch::searchLexically(const search::ParsedSearchQuery& query,
                                                            std::size_t topK) const {
    std::vector<SmartSearchResult> results;
    if (topK == 0) {
        return results;
    }

    const std::string promptLower = lowerAscii(query.semanticText);
    const std::vector<std::string> tokens = query.keywords.empty()
        ? queryTokens(query.semanticText)
        : query.keywords;

    std::ostringstream sql;
    sql << R"(
        SELECT f.file_id, f.file_name, f.file_path, f.extension, f.created_date, f.last_modified, f.embedding,
               COALESCE(GROUP_CONCAT(t.tag_name, ' '), '')
        FROM files f
        LEFT JOIN tags t ON f.file_id = t.file_id
    )";

    bool hasCondition = false;
    appendStructuredFilters(sql, query, hasCondition);
    sql << " GROUP BY f.file_id, f.file_name, f.file_path, f.extension, f.created_date, f.last_modified, f.embedding;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3* sqliteDb = db.getDb();
    const std::string sqlText = sql.str();
    if (sqlite3_prepare_v2(sqliteDb, sqlText.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare lexical smart search statement: "
                  << sqlite3_errmsg(sqliteDb) << std::endl;
        return results;
    }

    int bindIndex = 1;
    bindStructuredFilters(stmt, bindIndex, query);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord record = recordFromStatement(stmt, false);
        const std::string tagsText = columnText(stmt, 7);
        float score = lexicalScore(promptLower, tokens, record, tagsText);
        score = std::min(0.99F, score + reflectedTagBoostFromText(tagsText, query.reflectedTags));
        if (score <= 0.0F && (query.hasDateFilter() || query.hasExtensionFilter())) {
            score = 0.20F;
        }
        if (score <= 0.0F) {
            continue;
        }
        results.push_back({std::move(record), score});
    }
    sqlite3_finalize(stmt);

    std::sort(results.begin(), results.end(), [](const SmartSearchResult& lhs,
                                                 const SmartSearchResult& rhs) {
        return lhs.score > rhs.score;
    });
    if (results.size() > topK) {
        results.resize(topK);
    }
    return results;
}

std::vector<SmartSearchResult> SmartSearch::searchByEmbedding(const std::vector<float>& queryEmbedding,
                                                              std::size_t topK) const {
    return searchByEmbedding(queryEmbedding, search::ParsedSearchQuery{}, topK);
}

std::vector<SmartSearchResult> SmartSearch::searchByEmbedding(const std::vector<float>& queryEmbedding,
                                                              const search::ParsedSearchQuery& query,
                                                              std::size_t topK) const {
    std::vector<SmartSearchResult> results;
    if (topK == 0 || queryEmbedding.empty()) {
        return results;
    }

    std::ostringstream sql;
    sql << R"(
        SELECT f.file_id, f.file_name, f.file_path, f.extension, f.created_date, f.last_modified, f.embedding
        FROM files f
        WHERE f.embedding IS NOT NULL
    )";
    bool hasCondition = true;
    appendStructuredFilters(sql, query, hasCondition);
    sql << ";";

    sqlite3_stmt* stmt = nullptr;
    sqlite3* sqliteDb = db.getDb();
    const std::string sqlText = sql.str();
    if (sqlite3_prepare_v2(sqliteDb, sqlText.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare smart search statement: "
                  << sqlite3_errmsg(sqliteDb) << std::endl;
        return results;
    }

    int bindIndex = 1;
    bindStructuredFilters(stmt, bindIndex, query);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::vector<float> embedding = embeddingFromBlob(stmt, 6);
        float score = 0.0F;
        if (!cosineSimilarity(queryEmbedding, embedding, score)) {
            continue;
        }

        FileRecord record = recordFromStatement(stmt, false);
        record.embedding = std::move(embedding);
        score = std::min(0.99F, score + reflectedTagBoostForFile(sqliteDb,
                                                                 record.file_id,
                                                                 query.reflectedTags));

        results.push_back({std::move(record), score});
    }
    sqlite3_finalize(stmt);

    std::sort(results.begin(), results.end(), [](const SmartSearchResult& lhs,
                                                 const SmartSearchResult& rhs) {
        return lhs.score > rhs.score;
    });
    if (results.size() > topK) {
        results.resize(topK);
    }
    return results;
}
