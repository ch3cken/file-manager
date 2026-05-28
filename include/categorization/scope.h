#pragma once

/* ==========================================================================
 *  SRS Feature Map - Categorization Scope
 * --------------------------------------------------------------------------
 *  Implements user-controlled categorization targets:
 *    REQ-4.2.3.1 [Watched Directory]                   -> add/remove/set roots
 *    REQ-4.3.3.3 [Categorization Target Configuration] -> settings integration
 *    REQ-4.3.2.4 [Changing Categorization Directories] -> re-scope support
 * --------------------------------------------------------------------------
 *  NOTE: If the directory or extension lists are empty, it implies an "allow all"
 *  policy. This enables helper utilities to function properly during the initial
 *  setup phase before the user explicitly narrows down the categorization scope.
 * ========================================================================== */
#include <algorithm>
#include <string>
#include <vector>

#include "collection_utils.h"
#include "file_metadata.h"
#include "types.h"

namespace categorization {
    /// REQ-4.2.3.1: register/remove one watched root directory.
    inline bool addWatchedDirectory(CategorizationScope& scope, const std::string& directory) {
        return pushUnique(scope.watchedDirectories, normalizePath(directory));
    }
    inline bool removeWatchedDirectory(CategorizationScope& scope, const std::string& directory) {
        return eraseValue(scope.watchedDirectories, normalizePath(directory));
    }
    /// REQ-4.3.2.4 / REQ-4.3.3.3: replace categorization target directories after settings changes.
    inline void setWatchedDirectories(CategorizationScope& scope, const std::vector<std::string>& directories) {
        scope.watchedDirectories.clear();
        for (const auto& d : directories)
            addWatchedDirectory(scope, d);
    }

    /// REQ-4.1.3.7 / REQ-4.3.3.2: register/remove one extension filter for scoped preprocessing.
    inline bool addTargetExtension(CategorizationScope& scope, const std::string& extension) {
        return pushUnique(scope.targetExtensions, normalizeExtension(extension));
    }
    inline bool removeTargetExtension(CategorizationScope& scope, const std::string& extension) {
        return eraseValue(scope.targetExtensions, normalizeExtension(extension));
    }
    /// REQ-4.3.3.2 / REQ-4.3.3.4: replace extension filters after settings changes.
    inline void setTargetExtensions(CategorizationScope& scope, const std::vector<std::string>& extensions) {
        scope.targetExtensions.clear();
        for (const auto& e : extensions)
            addTargetExtension(scope, e);
    }

    /// REQ-4.2.3.1 / REQ-4.3.3.3: decide whether a detected file is inside the configured categorization scope.
    template <typename FileLike>
    inline bool isInScope(const FileLike& file, const CategorizationScope& scope) {
        const bool pathAllowed = scope.watchedDirectories.empty() || std::any_of(scope.watchedDirectories.begin(), scope.watchedDirectories.end(), [&](const std::string& dir) {
                return pathMatchesDirectory(file.filePath, dir, scope.includeSubdirectories);
            });
        const std::string ext = extensionOf(file);
        return pathAllowed && (scope.targetExtensions.empty() || std::find(scope.targetExtensions.begin(), scope.targetExtensions.end(), ext) != scope.targetExtensions.end());
    }
}
