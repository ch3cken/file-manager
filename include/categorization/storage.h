#pragma once

/* ==========================================================================
 *  SRS Feature Map - Categorization Persistence (SQLite)
 * --------------------------------------------------------------------------
 *  Implements the database-facing side of SRS 4.2 Categorization against
 *  a local SQLite database (SRS 3.3.3, 5.3.1):
 *    REQ-4.2.2.1-D [Automatic Categorization]        -> store generated tags
 *    REQ-4.2.2.3   [Deletion]                        -> delete category tags
 *    REQ-4.2.3.7   [Categorization Metadata Storage] -> local DB tags
 *    REQ-4.2.3.8   [Keyword Management]              -> add/replace/remove
 *    REQ-4.2.3.9   [Keyword Reflection]              -> keyword:* tags
 *    REQ-4.2.3.10  [Categorization Update]           -> recategorize on change
 *    REQ-4.2.3.12  [Information Update]              -> refresh managed tags
 * --------------------------------------------------------------------------
 *  Persists categorization only as local metadata; it does not move or rename
 *  user files (SRS 2.2.2, 4.2.1) and keeps all DB data in local storage
 *  (SRS 2.4.2, 3.3.3, 5.3.1).
 * ========================================================================== */
#include <string>
#include <vector>

#include <sqlite3.h>
#include "rules.h"
#include "scope.h"
#include "text_utils.h"
#include "types.h"

namespace categorization {
    namespace detail {
        inline std::string keywordTag(const std::string& kw) { ///< REQ-4.2.3.9: canonical "keyword:<label>" form.
            const std::string c = label(kw); return c.empty() ? std::string{} : "keyword:" + c;
        }

        /// REQ-4.2.3.7: prepare, bind, step, and finalize a one-shot SQLite statement.
        template <typename Binder>
        inline bool sqliteRun(sqlite3* db, const char* sql, Binder bind) {
            if (!db)
                return false;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                return false;
            bind(stmt);
            const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
            sqlite3_finalize(stmt);
            return ok;
        }

        inline bool sqliteDeleteTagsWhere(sqlite3* db, int id, const char* cond) { ///< REQ-4.2.2.3 / REQ-4.2.3.12
            if (id <= 0)
                return false;
            return sqliteRun(db, (std::string("DELETE FROM tags WHERE file_id = ? AND (") + cond + ");").c_str(),
                [&](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, id); });
        }
        inline bool sqliteClearManagedTags(sqlite3* db, int id) { ///< REQ-4.2.3.12: clears auto-generated tags, preserves keywords.
            return sqliteDeleteTagsWhere(db, id,
                "tag_name LIKE 'subject:%' OR tag_name LIKE 'project:%' OR tag_name LIKE 'document_type:%' OR "
                "tag_name LIKE 'media_type:%' OR tag_name LIKE 'extension:%' OR tag_name LIKE 'created_year:%' OR "
                "tag_name LIKE 'created_month:%' OR tag_name LIKE 'modified_year:%' OR tag_name LIKE 'modified_month:%' OR "
                "tag_name LIKE 'status:%' OR tag_name LIKE 'exception:%' OR tag_name LIKE 'size:%' OR tag_name LIKE 'size_bytes:%'");
        }
        inline bool sqliteClearKeywordTags(sqlite3* db, int id) { return sqliteDeleteTagsWhere(db, id, "tag_name LIKE 'keyword:%'"); } ///< REQ-4.2.3.8
        inline bool sqliteDeleteTag(sqlite3* db, int id, const std::string& tag) { ///< REQ-4.2.3.8: remove one exact tag.
            return id > 0 && !tag.empty() && sqliteRun(db, "DELETE FROM tags WHERE file_id = ? AND tag_name = ?;",
                [&](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, id); sqlite3_bind_text(s, 2, tag.c_str(), -1, SQLITE_TRANSIENT); });
        }
        inline bool sqliteInsertTag(sqlite3* db, int id, const std::string& tag) { ///< REQ-4.2.3.7 / REQ-4.2.3.9: insert tag if not present.
            return id > 0 && !tag.empty() && sqliteRun(db, R"(
                INSERT INTO tags (file_id, tag_name)
                SELECT ?, ? WHERE NOT EXISTS (SELECT 1 FROM tags WHERE file_id = ? AND tag_name = ?);
            )", [&](sqlite3_stmt* s) {
                sqlite3_bind_int(s, 1, id); sqlite3_bind_text(s, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(s, 3, id); sqlite3_bind_text(s, 4, tag.c_str(), -1, SQLITE_TRANSIENT);
            });
        }

        inline void sqliteBindNullableText(sqlite3_stmt* s, int i, const std::string& v) { ///< REQ-4.2.3.3 / REQ-4.2.3.12: NULL for empty strings.
            if (v.empty())
                sqlite3_bind_null(s, i);
            else
                sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT);
        }

        inline int sqliteFileIdByPath(sqlite3* db, const std::string& path) { ///< REQ-4.2.3.7 / REQ-4.2.3.12: look up file_id by normalized path.
            if (!db || path.empty())
                return 0;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT file_id FROM files WHERE file_path = ?;", -1, &stmt, nullptr) != SQLITE_OK)
                return 0;
            sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
            const int id = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : 0;
            sqlite3_finalize(stmt);
            return id;
        }

        /// REQ-4.2.3.3 / REQ-4.2.3.12: upsert file metadata row; returns file_id on success.
        inline int sqliteUpsertFileMetadata(sqlite3* db, const FileMetadata& file) {
            if (!db || file.filePath.empty())
                return 0;
            constexpr const char* sql = R"(
                INSERT INTO files (file_name, file_path, extension, created_date, last_modified)
                VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(file_path) DO UPDATE SET
                    file_name = excluded.file_name, extension = excluded.extension,
                    created_date = COALESCE(excluded.created_date, files.created_date),
                    last_modified = excluded.last_modified;
            )";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                return 0;
            sqliteBindNullableText(stmt, 1, file.fileName);
            sqlite3_bind_text(stmt, 2, file.filePath.c_str(), -1, SQLITE_TRANSIENT);
            sqliteBindNullableText(stmt, 3, file.extension);
            sqliteBindNullableText(stmt, 4, file.createdDate);
            sqliteBindNullableText(stmt, 5, file.lastModified);
            const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
            sqlite3_finalize(stmt);
            return ok ? sqliteFileIdByPath(db, file.filePath) : 0;
        }

        inline bool sqliteDeleteFileByPath(sqlite3* db, const std::string& path) { ///< REQ-4.2.2.3: cascade-deletes tags.
            if (!db || path.empty())
                return false;
            return sqliteRun(db, "DELETE FROM files WHERE file_path = ?;",
                [&](sqlite3_stmt* s) { sqlite3_bind_text(s, 1, path.c_str(), -1, SQLITE_TRANSIENT); });
        }
    }

    // -----------------------------------------------------------------------
    // Public API - all operations target the SQLite database via db.getDb().
    // -----------------------------------------------------------------------

    template <typename Db> inline bool clearManagedTags(Db& db, int id) { ///< REQ-4.2.3.12
        return detail::sqliteClearManagedTags(db.getDb(), id);
    }
    template <typename Db> inline bool clearKeywordTags(Db& db, int id) { ///< REQ-4.2.3.8
        return detail::sqliteClearKeywordTags(db.getDb(), id);
    }

    /// REQ-4.2.3.7 / REQ-4.2.3.11: insert one searchable tag into the SQLite database.
    template <typename Db>
    inline bool insertTag(Db& db, int id, const std::string& tag) {
        return detail::sqliteInsertTag(db.getDb(), id, tag);
    }

    /// REQ-4.2.3.7 / REQ-4.2.3.12: replace generated tags for a file.
    template <typename Db>
    inline bool store(Db& db, int id, const CategoryMetadata& meta) {
        if (id <= 0)
            return false;
        bool ok = clearManagedTags(db, id);
        for (const auto& tag : meta.tags)
            ok = insertTag(db, id, tag) && ok;
        return ok;
    }

    template <typename Db> inline int fileIdByPath(Db& db, const std::string& path) { ///< REQ-4.2.3.7 / REQ-4.2.3.12
        return detail::sqliteFileIdByPath(db.getDb(), path);
    }
    template <typename Db> inline int upsertFileMetadata(Db& db, FileMetadata& file) { ///< REQ-4.2.3.3 / REQ-4.2.3.12
        file.fileId = detail::sqliteUpsertFileMetadata(db.getDb(), file); return file.fileId;
    }
    template <typename Db> inline bool deleteFileByPath(Db& db, const std::string& path) { ///< REQ-4.2.2.3
        return detail::sqliteDeleteFileByPath(db.getDb(), path);
    }

    /// REQ-4.2.2.1-D: categorize and store tags for an already-known file id.
    template <typename Db, typename F>
    inline bool categorizeAndStore(Db& db, int id, const F& file,
                                   const std::string& text = {}, const std::vector<std::string>& kws = {}, const std::string& ex = {}) {
        return store(db, id, categorize(file, text, kws, ex));
    }
    /// REQ-4.2.2.1-D: overload using file.fileId.
    template <typename Db, typename F>
    inline bool categorizeAndStore(Db& db, const F& file,
                                   const std::string& text = {}, const std::vector<std::string>& kws = {}, const std::string& ex = {}) {
        return categorizeAndStore(db, file.fileId, file, text, kws, ex);
    }

    // -----------------------------------------------------------------------
    // Custom keyword lifecycle - REQ-4.2.3.8 through REQ-4.2.3.10, SRS 3.1.4.
    // -----------------------------------------------------------------------

    template <typename Db>
    inline bool addCustomKeywords(Db& db, int id, const std::vector<std::string>& kws) { ///< REQ-4.2.3.8 / REQ-4.2.3.9
        bool ok = id > 0;
        for (const auto& kw : kws) {
            const std::string t = detail::keywordTag(kw);
            if (!t.empty())
                ok = insertTag(db, id, t) && ok;
        }
        return ok;
    }
    template <typename Db, typename F>
    inline bool addKeywordsAndRecategorize(Db& db, int id, const F& file, const std::vector<std::string>& kws, ///< REQ-4.2.3.10
                                           const std::string& text = {}, const std::string& ex = {}) {
        return id > 0 && addCustomKeywords(db, id, kws) && store(db, id, categorize(file, text, kws, ex));
    }
    template <typename Db>
    inline bool replaceCustomKeywords(Db& db, int id, const std::vector<std::string>& kws) { ///< REQ-4.2.3.8
        return id > 0 && clearKeywordTags(db, id) && addCustomKeywords(db, id, kws);
    }
    template <typename Db, typename F>
    inline bool replaceKeywordsAndRecategorize(Db& db, int id, const F& file, const std::vector<std::string>& kws, ///< REQ-4.2.3.10 / REQ-4.2.3.12
                                               const std::string& text = {}, const std::string& ex = {}) {
        return id > 0 && clearKeywordTags(db, id) && store(db, id, categorize(file, text, kws, ex));
    }
    template <typename Db>
    inline bool removeCustomKeyword(Db& db, int id, const std::string& kw) { ///< REQ-4.2.3.8: remove one keyword tag.
        const std::string tag = detail::keywordTag(kw);
        return !tag.empty() && detail::sqliteDeleteTag(db.getDb(), id, tag);
    }
    template <typename Db>
    inline bool deleteCategorization(Db& db, int id) { ///< REQ-4.2.2.3 / REQ-4.3.2.4: wipe all managed+keyword tags.
        return id > 0 && detail::sqliteClearManagedTags(db.getDb(), id) && detail::sqliteClearKeywordTags(db.getDb(), id);
    }

    /// REQ-4.2.2.1-B/C/D / REQ-4.2.3.12: collect, extract, categorize, and store one local file end-to-end.
    template <typename Db>
    inline CategorizationUpdateResult categorizeLocalFileAndStore(Db& db, const std::string& path,
                                                                  const std::vector<std::string>& kws = {},
                                                                  std::size_t maxBytes = 64 * 1024) {
        CategorizationUpdateResult r;
        r.handled = true;
        std::string ex;
        r.file = collectMetadata(path, &ex);
        r.exceptionReason = ex;
        if (!r.file.filePath.empty())
            r.fileId = upsertFileMetadata(db, r.file);
        r.metadataStored = r.fileId > 0;
        if (ex.empty() && isSupportedDocumentExtension(r.file.extension)) {
            r.extraction = extractTextFromFile(r.file.filePath.empty() ? path : r.file.filePath, maxBytes);
            if (!r.extraction.exceptionReason.empty())
                r.exceptionReason = r.extraction.exceptionReason;
        }
        if (r.fileId > 0) {
            r.category = categorize(r.file, r.extraction.text, kws, r.exceptionReason);
            r.categorizationStored = store(db, r.fileId, r.category);
        }
        return r;
    }

    /// REQ-4.2.3.2 / REQ-4.2.3.12: apply one detected filesystem event (no scope filter).
    template <typename Db>
    inline CategorizationUpdateResult applyCategorizationEvent(Db& db, const CategorizationEvent& event,
                                                               const std::vector<std::string>& kws = {},
                                                               std::size_t maxBytes = 64 * 1024) {
        CategorizationUpdateResult r;
        r.handled = true;
        r.file = event.file;
        r.exceptionReason = event.exceptionReason;
        if (event.type == CategorizationEventType::Deleted) {
            const std::string path = !event.filePath.empty() ? event.filePath : event.file.filePath;
            r.fileId = fileIdByPath(db, path);
            r.deleted = deleteFileByPath(db, path);
            return r;
        }
        return categorizeLocalFileAndStore(db, !event.file.filePath.empty() ? event.file.filePath : event.filePath, kws, maxBytes);
    }

    /// REQ-4.2.3.1 / REQ-4.2.3.12: apply one event only when it is inside the configured scope.
    template <typename Db>
    inline CategorizationUpdateResult applyCategorizationEvent(Db& db, const CategorizationScope& scope,
                                                               const CategorizationEvent& event,
                                                               const std::vector<std::string>& kws = {},
                                                               std::size_t maxBytes = 64 * 1024) {
        CategorizationUpdateResult r;
        FileMetadata sf = event.file;
        if (sf.filePath.empty() && !event.filePath.empty())
            sf = collectMetadata(event.filePath);
        r.inScope = event.type == CategorizationEventType::Deleted || isInScope(sf, scope);
        if (!r.inScope)
            return r;
        r = applyCategorizationEvent(db, event, kws, maxBytes);
        r.inScope = true;
        return r;
    }
}
