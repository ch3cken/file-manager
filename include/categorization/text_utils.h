#pragma once

/* ==========================================================================
 *  SRS Feature Map - Text Normalization for Categorization
 * --------------------------------------------------------------------------
 *  Implements the normalized matching layer used by:
 *    REQ-4.2.3.4  [Primary Categorization]              -> filename/path terms
 *    REQ-4.2.3.6  [Categorization Metadata Generation]  -> subject/type labels
 *    REQ-4.2.3.11 [Search Reflection]                   -> searchable tag text
 * --------------------------------------------------------------------------
 *  These helpers enforce one canonical text form across categorization rules
 *  and Smart Search reflection so matching remains deterministic and avoids
 *  false positives from case/whitespace/punctuation differences.
 * ========================================================================== */
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <unicode/locid.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

namespace categorization {
    namespace detail {
        inline std::string toUtf8(const icu::UnicodeString& u) { std::string out; u.toUTF8String(out); return out; }
        inline bool isSpace(UChar32 c) { return u_isUWhiteSpace(c) != 0 || u_isspace(c) != 0; }
        inline void appendCodePoint(std::string& out, UChar32 c) { icu::UnicodeString s; s.append(c); out += toUtf8(s); }
    }

    /// REQ-4.2.3.4: lowercase using ICU's root locale for deterministic metadata/content matching.
    inline std::string toLower(std::string value) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(value);
        u.toLower(icu::Locale::getRoot());
        return detail::toUtf8(u);
    }

    /// REQ-4.2.3.4: trim Unicode whitespace before rule matching.
    inline std::string trim(const std::string& value) {
        const icu::UnicodeString u = icu::UnicodeString::fromUTF8(value);
        int32_t first = 0, last = u.length();
        while (first < last && detail::isSpace(u.char32At(first)))
            first = u.moveIndex32(first, 1);
        while (last > first && detail::isSpace(u.char32At(u.moveIndex32(last, -1))))
            last = u.moveIndex32(last, -1);
        return detail::toUtf8(u.tempSubStringBetween(first, last));
    }

    /// REQ-4.2.3.6 / REQ-4.2.3.11: convert text into the canonical label form used for categories and searchable tags.
    inline std::string label(std::string value) {
        value = toLower(trim(value));
        std::string out;
        bool sep = true;
        const icu::UnicodeString u = icu::UnicodeString::fromUTF8(value);
        for (int32_t i = 0; i < u.length(); i = u.moveIndex32(i, 1)) {
            const UChar32 c = u.char32At(i);
            if (u_isalnum(c) != 0) {
                detail::appendCodePoint(out, c); sep = false;
            } else if (!sep) {
                out.push_back(' '); sep = true;
            }
        }
        if (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    /// REQ-4.2.3.4: pad labels so phrase searches do not match inside longer words.
    inline std::string normalized(std::string value) {
        value = label(std::move(value));
        return value.empty() ? " " : " " + value + " ";
    }

    /// REQ-4.2.3.4: detect one normalized whole phrase in filename/path/content.
    inline bool containsPhrase(const std::string& normalizedText, const std::string& phrase) {
        const std::string needle = normalized(phrase);
        return needle != " " && normalizedText.find(needle) != std::string::npos;
    }

    /// REQ-4.2.3.4 / REQ-4.2.3.6: check a rule's pipe-separated candidate terms.
    inline bool containsAny(const std::string& normalizedText, const char* pipeSeparatedWords) {
        const std::string_view words(pipeSeparatedWords);
        for (std::size_t s = 0; s <= words.size();) {
            const std::size_t e = words.find('|', s), stop = e == std::string_view::npos ? words.size() : e;
            if (stop > s && containsPhrase(normalizedText, std::string(words.substr(s, stop - s))))
                return true;
            if (e == std::string_view::npos)
                return false;
            s = e + 1;
        }
        return false;
    }

    /// REQ-4.2.3.4: check whether a normalized extension/type is an exact rule choice.
    inline bool anyOf(const std::string& value, const char* spaceSeparatedChoices) {
        return !value.empty() && std::string(" ").append(spaceSeparatedChoices).append(" ").find(" " + value + " ") != std::string::npos;
    }
}
