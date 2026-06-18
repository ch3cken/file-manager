#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace search {

struct SearchDateRange {
    std::string startInclusive;
    std::string endExclusive;
    bool active = false;
};

struct ParsedSearchQuery {
    std::string originalText;
    std::string semanticText;
    std::vector<std::string> keywords;
    std::vector<std::string> extensions;
    std::vector<std::string> reflectedTags;
    SearchDateRange modifiedRange;

    bool hasExtensionFilter() const { return !extensions.empty(); }
    bool hasDateFilter() const { return modifiedRange.active; }
    bool hasStructuredFilters() const {
        return hasExtensionFilter() || hasDateFilter() || !reflectedTags.empty();
    }
};

ParsedSearchQuery parseSearchQuery(const std::string& query);
ParsedSearchQuery parseSearchQuery(const std::string& query,
                                   std::chrono::system_clock::time_point now);

} // namespace search
