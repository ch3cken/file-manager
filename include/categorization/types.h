#pragma once

/* ==========================================================================
 *  SRS Feature Map - Categorization Data Contracts
 * --------------------------------------------------------------------------
 *  Stable hand-off objects between indexing, extraction, rule inference,
 *  storage, and Smart Search ranking.
 *    CategoryMetadata     -> REQ-4.2.3.6, REQ-4.2.3.7, REQ-4.2.3.11
 *    FileMetadata         -> REQ-4.2.3.3
 *    CategorizationScope  -> REQ-4.2.3.1, REQ-4.3.3.3
 *    CategorizationEvent  -> REQ-4.2.3.2, REQ-4.2.3.12
 *    TextExtractionResult -> REQ-4.2.3.5, REQ-4.2.3.13
 *    SearchReflection     -> REQ-4.2.3.11
 * ========================================================================== */
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace categorization {
    /// REQ-4.2.3.6 / REQ-4.2.3.11: generated subject, project, type, status, keyword, and tag data later stored for Smart Search use.
    struct CategoryMetadata {
        std::string subject, project, documentType, mediaType, sizeClass, status, exceptionReason;
        std::uintmax_t fileSize = 0;
        std::vector<std::string> customKeywords, tags;
    };

    /// REQ-4.2.3.3: minimal file metadata required by the categorizer; mirrors database/indexer fields while staying independent from the core database layer.
    struct FileMetadata {
        int fileId = 0;
        std::string fileName, filePath, extension, createdDate, lastModified;
        std::uintmax_t fileSize = 0;
    };

    /// REQ-4.2.3.1 / REQ-4.3.3.3: user-configurable boundary that decides which files should be categorized.
    struct CategorizationScope {
        std::vector<std::string> watchedDirectories, targetExtensions;
        bool includeSubdirectories = true;
    };

    /// REQ-4.2.3.2 / REQ-4.2.3.12: categorization-relevant filesystem event type and payload.
    enum class CategorizationEventType { Created, Modified, Deleted };
    struct CategorizationEvent {
        CategorizationEventType type = CategorizationEventType::Modified;
        FileMetadata file;
        std::string filePath, exceptionReason;
    };

    /// REQ-4.2.3.5 / REQ-4.2.3.13: result of optional local text extraction; failed reasons are folded into status/exception tags.
    struct TextExtractionResult {
        std::string text, exceptionReason;
        std::size_t bytesRead = 0;
        bool attempted = false, success = false;
    };

    /// REQ-4.2.3.11: category and keyword tags inferred from a Smart Search query for filtering/ranking.
    struct SearchReflection { std::vector<std::string> tags, keywordTags; };

    /// REQ-4.2.3.10 / REQ-4.2.3.12: outcome of applying one categorization event to storage.
    struct CategorizationUpdateResult {
        bool handled = false, inScope = false, deleted = false, metadataStored = false, categorizationStored = false;
        int fileId = 0;
        FileMetadata file;
        TextExtractionResult extraction;
        CategoryMetadata category;
        std::string exceptionReason;
    };
}
