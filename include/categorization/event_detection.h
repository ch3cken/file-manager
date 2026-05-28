#pragma once

/* ==========================================================================
 *  SRS Feature Map - Categorization Event Detection
 * --------------------------------------------------------------------------
 *  Implements REQ-4.2.3.2: Polling-based detector that watches configured
 *  directories for file changes (create, modify, delete). Complements
 *  OS-level watchers like the Windows USN Journal.
 * ========================================================================== */
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "file_metadata.h"
#include "scope.h"
#include "types.h"

namespace categorization {
    namespace detail {
        /// REQ-4.2.3.2: path -> file metadata snapshot.
        using CategorizationSnapshot = std::unordered_map<std::string, FileMetadata>;
        inline bool fileStateChanged(const FileMetadata& a, const FileMetadata& b) {
            return a.fileSize != b.fileSize || a.lastModified != b.lastModified || a.extension != b.extension || a.fileName != b.fileName;
        }

        inline void addSnapshotFile(CategorizationSnapshot& snap, const CategorizationScope& scope, const std::filesystem::path& path) {
            std::string ex;
            FileMetadata f = collectMetadata(path.string(), &ex);
            if (!ex.empty())
                return;
            if (!scope.targetExtensions.empty() && std::find(scope.targetExtensions.begin(), scope.targetExtensions.end(), extensionOf(f)) == scope.targetExtensions.end())
                return;
            const std::string key = normalizePath(f.filePath.empty() ? path.string() : f.filePath);
            if (!key.empty())
                snap[key] = std::move(f);
        }

        /// REQ-4.2.3.2: scan one watched directory (recursively if configured) into snap.
        inline void scanWatchedDirectory(CategorizationSnapshot& snap, const CategorizationScope& scope, const std::string& directory) {
            std::error_code ec;
            const std::filesystem::path root(directory);
            if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))\
                return;
            const auto opts = std::filesystem::directory_options::skip_permission_denied;
            if (scope.includeSubdirectories) {
                std::filesystem::recursive_directory_iterator it(root, opts, ec), end;
                for (; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec))
                        addSnapshotFile(snap, scope, it->path());
            } else {
                std::filesystem::directory_iterator it(root, opts, ec), end;
                for (; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec))
                        addSnapshotFile(snap, scope, it->path());
            }
        }
    }

    /// REQ-4.2.3.2: build a file snapshot for the current categorization scope.
    inline detail::CategorizationSnapshot captureCategorizationSnapshot(const CategorizationScope& scope) {
        detail::CategorizationSnapshot snap;
        for (const auto& dir : scope.watchedDirectories)
            detail::scanWatchedDirectory(snap, scope, dir);
        return snap;
    }

    /// REQ-4.2.3.2 / REQ-4.2.3.12: polling detector that emits created, modified, and deleted categorization events.
    class CategorizationEventDetector {
    public:
        explicit CategorizationEventDetector(CategorizationScope scope = {}) : scope_(std::move(scope)) {}
        /// REQ-4.2.3.1: replace scope and reset baseline.
        void setScope(CategorizationScope scope) {
            scope_ = std::move(scope); snapshot_.clear();
        }
        const CategorizationScope& scope() const {
            return scope_;
        }
        /// REQ-4.2.3.2: establish baseline without emitting events.
        void prime() { snapshot_ = captureCategorizationSnapshot(scope_); }

        /// REQ-4.2.3.2 / REQ-4.2.3.12: return events since the last poll and advance the baseline.
        std::vector<CategorizationEvent> poll() {
            std::vector<CategorizationEvent> events;
            auto current = captureCategorizationSnapshot(scope_);
            for (const auto& [key, file] : current) {
                const auto prev = snapshot_.find(key);
                if (prev == snapshot_.end())
                    events.push_back({CategorizationEventType::Created,  file, file.filePath, {}});
                else if (detail::fileStateChanged(prev->second, file))
                    events.push_back({CategorizationEventType::Modified, file, file.filePath, {}});
            }
            for (const auto& [key, file] : snapshot_)
                if (!current.count(key))
                    events.push_back({CategorizationEventType::Deleted, file, file.filePath, {}});
            snapshot_ = std::move(current);
            return events;
        }

    private:
        CategorizationScope scope_;
        detail::CategorizationSnapshot snapshot_;
    };
}
