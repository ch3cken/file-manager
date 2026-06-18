#include "core/indexer.h"
#include "core/path_utils.h"
#include "embedder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

std::string formatFileTime(const fs::file_time_type& ftime);

namespace {
class ScopedTransaction {
public:
    explicit ScopedTransaction(sqlite3* db) : db_(db) {
        active_ = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    ~ScopedTransaction() {
        if (active_) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    void commit() {
        if (active_) {
            if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK) {
                active_ = false;
            }
        }
    }

private:
    sqlite3* db_ = nullptr;
    bool active_ = false;
};

FileRecord createFileRecord(const fs::directory_entry& entry) {
    FileRecord record;
    record.file_path = pathutil::toUtf8(entry.path());
    record.file_name = pathutil::toUtf8(entry.path().filename());

    if (entry.path().has_extension()) {
        record.extension = pathutil::toUtf8(entry.path().extension());
    }

    try {
        record.last_modified = formatFileTime(entry.last_write_time());
    } catch (const std::exception&) {
        record.last_modified = "1970-01-01 00:00:00";
    }

    return record;
}

std::string normalizeMetadataText(std::string text) {
    bool lastWasSpace = false;
    std::string normalized;
    normalized.reserve(text.size());

    for (unsigned char ch : text) {
        if (ch < 0x80 && !std::isalnum(ch)) {
            if (!lastWasSpace) {
                normalized.push_back(' ');
                lastWasSpace = true;
            }
            continue;
        }
        normalized.push_back(static_cast<char>(ch));
        lastWasSpace = false;
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

std::string metadataTextForEmbedding(const FileRecord& record) {
    std::string text = normalizeMetadataText(record.file_name);
    return text.empty() ? std::string("file") : text;
}

bool hasTextLikeExtension(const fs::path& path) {
    std::string extension = pathutil::toUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    static constexpr std::string_view kTextExtensions[] = {
        ".txt", ".md", ".csv", ".tsv", ".json", ".xml", ".html", ".htm",
        ".log", ".ini", ".cfg", ".conf", ".yaml", ".yml",
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".cs", ".java", ".js", ".jsx", ".ts", ".tsx", ".py", ".rb",
        ".go", ".rs", ".php", ".css", ".scss", ".sql", ".cmake"
    };

    return std::find(std::begin(kTextExtensions), std::end(kTextExtensions), extension) !=
           std::end(kTextExtensions);
}

bool shouldEmbedFileContents(const fs::directory_entry& entry) {
    constexpr std::uintmax_t kMaxContentEmbeddingBytes = 1024 * 1024;

    if (!hasTextLikeExtension(entry.path())) {
        return false;
    }

    std::error_code ec;
    const auto size = entry.file_size(ec);
    return !ec && size > 0 && size <= kMaxContentEmbeddingBytes;
}

void assignMetadataEmbeddings(std::vector<FileRecord>& records,
                              const nlp::Embedder& embedder,
                              std::size_t& metadataEmbeddingCount,
                              std::size_t& metadataOnlyCount) {
    if (records.empty()) {
        return;
    }

    std::vector<std::string> texts;
    texts.reserve(records.size());
    for (const auto& record : records) {
        texts.push_back(metadataTextForEmbedding(record));
    }

    try {
        const auto embeddings = embedder.embedTexts(texts);
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (i < embeddings.size() && !embeddings[i].empty()) {
                records[i].embedding = embeddings[i];
                metadataEmbeddingCount++;
            } else {
                metadataOnlyCount++;
            }
        }
    } catch (const std::exception& batchError) {
        for (std::size_t i = 0; i < records.size(); ++i) {
            try {
                records[i].embedding = embedder.embedText(texts[i]);
                metadataEmbeddingCount++;
            } catch (const std::exception& e) {
                metadataOnlyCount++;
                std::cerr << "Could not embed metadata for " << records[i].file_path
                          << ": " << e.what()
                          << " (batch error: " << batchError.what() << ")" << std::endl;
            }
        }
    }
}
}

void Indexer::scanDirectoryWithEmbeddings(const std::string& directoryPath,
                                          const nlp::Embedder& embedder,
                                          std::size_t maxContentEmbeddings) {
    try {
        const fs::path root = pathutil::fromUtf8(directoryPath);
        if (!fs::exists(root) || !fs::is_directory(root)) {
            std::cerr << "Invalid directory path: " << directoryPath << std::endl;
            return;
        }

        std::cout << "Starting Smart Search indexing for directory: " << directoryPath << std::endl;
        std::size_t count = 0;
        std::size_t embeddedCount = 0;
        std::size_t metadataEmbeddingCount = 0;
        std::size_t metadataOnlyCount = 0;
        ScopedTransaction transaction(db.getDb());
        std::vector<FileRecord> metadataBatch;
        metadataBatch.reserve(64);

        auto insertRecord = [&](const FileRecord& record) {
            if (db.insertFile(record)) {
                count++;
                if (count % 1000 == 0) {
                    std::cout << "Smart indexing progress: " << count << " files processed..." << std::endl;
                }
            }
        };

        auto flushMetadataBatch = [&]() {
            assignMetadataEmbeddings(metadataBatch, embedder, metadataEmbeddingCount, metadataOnlyCount);
            for (const auto& batchedRecord : metadataBatch) {
                insertRecord(batchedRecord);
            }
            metadataBatch.clear();
        };

        for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            FileRecord record = createFileRecord(entry);
            if (embeddedCount < maxContentEmbeddings &&
                shouldEmbedFileContents(entry)) {
                try {
                    record.embedding = embedder.embedFile(entry.path());
                    embeddedCount++;
                    insertRecord(record);
                    continue;
                } catch (const std::exception&) {
                    metadataBatch.push_back(std::move(record));
                }
            } else {
                metadataBatch.push_back(std::move(record));
            }

            if (metadataBatch.size() >= 64) {
                flushMetadataBatch();
            }
        }

        flushMetadataBatch();
        transaction.commit();

        std::cout << "Smart indexing complete! Added " << count << " files, "
                  << embeddedCount << " content embeddings, "
                  << metadataEmbeddingCount << " metadata embeddings, "
                  << metadataOnlyCount << " metadata-only records." << std::endl;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error during smart indexing: " << e.what() << std::endl;
    }
}
