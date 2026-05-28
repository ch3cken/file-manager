#pragma once

/* ==========================================================================
 *  SRS Feature Map - Unique List Maintenance
 * --------------------------------------------------------------------------
 *  Implements small shared helpers used by:
 *    REQ-4.2.3.1  [Watched Directory]      -> unique scope directories
 *    REQ-4.2.3.8  [Keyword Management]     -> unique user keywords
 *    REQ-4.2.3.11 [Search Reflection]      -> duplicate-free searchable tags
 * ========================================================================== */
#include <algorithm>
#include <string>
#include <vector>

namespace categorization {
    /// REQ-4.2.3.8 / REQ-4.2.3.11: append a non-empty value once.
    inline bool pushUnique(std::vector<std::string>& values, std::string value) {
        if (value.empty() || std::find(values.begin(), values.end(), value) != values.end())
            return false;
        values.push_back(std::move(value));
        return true;
    }

    /// REQ-4.2.3.1 / REQ-4.2.3.8: remove all matching values and report whether the list changed.
    inline bool eraseValue(std::vector<std::string>& values, const std::string& value) {
        const auto it = std::remove(values.begin(), values.end(), value);
        if (it == values.end())
            return false;
        values.erase(it, values.end());
        return true;
    }
}
