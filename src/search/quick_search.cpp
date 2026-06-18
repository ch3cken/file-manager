#include "search/quick_search.h"
#include <iostream>
#include <sstream>

QuickSearch::QuickSearch(DatabaseManager& database) : db(database) {}

std::vector<FileRecord> QuickSearch::search(const std::string& keyword) {
    return search(search::parseSearchQuery(keyword), 100);
}

namespace {
std::string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : std::string{};
}

void bindText(sqlite3_stmt* stmt, int& index, const std::string& value) {
    sqlite3_bind_text(stmt, index++, value.c_str(), -1, SQLITE_TRANSIENT);
}

FileRecord recordFromStatement(sqlite3_stmt* stmt) {
    FileRecord record;
    record.file_id = sqlite3_column_int(stmt, 0);
    record.file_name = columnText(stmt, 1);
    record.file_path = columnText(stmt, 2);
    record.extension = columnText(stmt, 3);
    record.created_date = columnText(stmt, 4);
    record.last_modified = columnText(stmt, 5);
    return record;
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
}

std::vector<FileRecord> QuickSearch::search(const search::ParsedSearchQuery& query,
                                            std::size_t limit) {
    std::vector<FileRecord> results;
    sqlite3* sqlite_db = db.getDb();

    if (limit == 0) {
        return results;
    }

    std::ostringstream sql;
    sql << R"(
        SELECT DISTINCT f.file_id, f.file_name, f.file_path, f.extension, f.created_date, f.last_modified 
        FROM files f 
        LEFT JOIN tags t ON f.file_id = t.file_id
    )";

    std::vector<std::string> likes;
    for (const auto& keyword : query.keywords) {
        likes.push_back("%" + keyword + "%");
    }
    if (!query.semanticText.empty() && query.semanticText != query.originalText) {
        likes.push_back("%" + query.semanticText + "%");
    }

    std::vector<std::string> tagLikes;
    for (const auto& tag : query.reflectedTags) {
        tagLikes.push_back("%" + tag + "%");
    }

    bool hasCondition = false;
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

    if (!likes.empty() || !tagLikes.empty()) {
        appendConditionPrefix();
        sql << "(";
        bool hasMatch = false;
        auto appendOr = [&]() {
            if (hasMatch) {
                sql << " OR ";
            }
            hasMatch = true;
        };

        for (std::size_t i = 0; i < likes.size(); ++i) {
            appendOr();
            sql << "LOWER(COALESCE(f.file_name, '')) LIKE ?";
            appendOr();
            sql << "LOWER(COALESCE(f.file_path, '')) LIKE ?";
            appendOr();
            sql << "LOWER(COALESCE(t.tag_name, '')) LIKE ?";
        }
        for (std::size_t i = 0; i < tagLikes.size(); ++i) {
            appendOr();
            sql << "LOWER(COALESCE(t.tag_name, '')) LIKE ?";
        }
        sql << ")";
    }

    sql << " ORDER BY COALESCE(f.last_modified, '') DESC, f.file_name ASC LIMIT ?;";
    
    sqlite3_stmt* stmt;
    const std::string sqlText = sql.str();
    if (sqlite3_prepare_v2(sqlite_db, sqlText.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare search statement: " << sqlite3_errmsg(sqlite_db) << std::endl;
        return results;
    }

    int bindIndex = 1;
    if (query.hasDateFilter()) {
        bindText(stmt, bindIndex, query.modifiedRange.startInclusive);
        bindText(stmt, bindIndex, query.modifiedRange.endExclusive);
    }
    for (const auto& extension : query.extensions) {
        bindText(stmt, bindIndex, extension);
    }
    for (const auto& like : likes) {
        bindText(stmt, bindIndex, like);
        bindText(stmt, bindIndex, like);
        bindText(stmt, bindIndex, like);
    }
    for (const auto& tagLike : tagLikes) {
        bindText(stmt, bindIndex, tagLike);
    }
    sqlite3_bind_int(stmt, bindIndex++, static_cast<int>(limit));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(recordFromStatement(stmt));
    }
    
    sqlite3_finalize(stmt);
    return results;
}
