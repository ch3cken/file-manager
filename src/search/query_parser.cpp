#include "search/query_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <set>
#include <sstream>
#include <string_view>

namespace search {
namespace {

struct TypeRule {
    std::string_view tag;
    std::string_view terms;
    std::string_view extensions;
    std::string_view reflectedTags;
};

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalizeSeparators(std::string value) {
    bool lastWasSpace = true;
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back(' ');

    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '.') {
            out.push_back(static_cast<char>(ch));
            lastWasSpace = false;
        } else if (!lastWasSpace) {
            out.push_back(' ');
            lastWasSpace = true;
        }
    }
    if (out.back() != ' ') {
        out.push_back(' ');
    }
    return out;
}

std::string trimSpaces(const std::string& value) {
    const auto begin = value.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(' ');
    return value.substr(begin, end - begin + 1);
}

std::string collapseSpaces(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool lastWasSpace = true;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!lastWasSpace) {
                out.push_back(' ');
                lastWasSpace = true;
            }
        } else {
            out.push_back(static_cast<char>(ch));
            lastWasSpace = false;
        }
    }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

bool containsPhrase(const std::string& normalizedQuery, std::string_view phrase) {
    const std::string needle = normalizeSeparators(std::string(phrase));
    return needle != " " && normalizedQuery.find(needle) != std::string::npos;
}

void removePhrase(std::string& text, std::string_view phrase) {
    std::string lowered = toLowerAscii(std::string(phrase));
    while (true) {
        std::string textLower = toLowerAscii(text);
        const auto pos = textLower.find(lowered);
        if (pos == std::string::npos) {
            break;
        }
        text.replace(pos, lowered.size(), " ");
    }
}

void pushUnique(std::vector<std::string>& values, std::string value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

void splitChoices(std::string_view choices, std::vector<std::string>& output) {
    std::string current;
    for (char ch : choices) {
        if (ch == ' ' || ch == '|') {
            pushUnique(output, current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    pushUnique(output, current);
}

void splitPipeChoices(std::string_view choices, std::vector<std::string>& output) {
    std::string current;
    for (char ch : choices) {
        if (ch == '|') {
            pushUnique(output, current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    pushUnique(output, current);
}

bool anyTermMatches(const std::string& normalizedQuery, std::string_view pipeSeparatedTerms) {
    std::size_t start = 0;
    while (start <= pipeSeparatedTerms.size()) {
        const std::size_t end = pipeSeparatedTerms.find('|', start);
        const std::size_t stop = end == std::string_view::npos ? pipeSeparatedTerms.size() : end;
        if (stop > start && containsPhrase(normalizedQuery, pipeSeparatedTerms.substr(start, stop - start))) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

void removeTerms(std::string& text, std::string_view pipeSeparatedTerms) {
    std::size_t start = 0;
    while (start <= pipeSeparatedTerms.size()) {
        const std::size_t end = pipeSeparatedTerms.find('|', start);
        const std::size_t stop = end == std::string_view::npos ? pipeSeparatedTerms.size() : end;
        if (stop > start) {
            removePhrase(text, pipeSeparatedTerms.substr(start, stop - start));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
}

std::tm localTm(std::chrono::system_clock::time_point value) {
    const std::time_t raw = std::chrono::system_clock::to_time_t(value);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    return tm;
}

std::chrono::system_clock::time_point fromLocalTm(std::tm tm) {
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point startOfDay(std::chrono::system_clock::time_point value) {
    std::tm tm = localTm(value);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return fromLocalTm(tm);
}

std::chrono::system_clock::time_point addDays(std::chrono::system_clock::time_point value, int days) {
    return value + std::chrono::hours(24 * days);
}

std::chrono::system_clock::time_point addMonths(std::chrono::system_clock::time_point value, int months) {
    std::tm tm = localTm(value);
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mon += months;
    return fromLocalTm(tm);
}

std::chrono::system_clock::time_point addYears(std::chrono::system_clock::time_point value, int years) {
    std::tm tm = localTm(value);
    tm.tm_mon = 0;
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_year += years;
    return fromLocalTm(tm);
}

std::string formatDateTime(std::chrono::system_clock::time_point value) {
    std::tm tm = localTm(value);
    char buffer[20] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

void setRange(ParsedSearchQuery& parsed,
              std::chrono::system_clock::time_point start,
              std::chrono::system_clock::time_point end) {
    parsed.modifiedRange.startInclusive = formatDateTime(start);
    parsed.modifiedRange.endExclusive = formatDateTime(end);
    parsed.modifiedRange.active = true;
}

void parseDateRange(ParsedSearchQuery& parsed,
                    std::string& workingText,
                    const std::string& normalizedQuery,
                    std::chrono::system_clock::time_point now) {
    const auto today = startOfDay(now);
    const auto tomorrow = addDays(today, 1);

    if (containsPhrase(normalizedQuery, "yesterday")) {
        setRange(parsed, addDays(today, -1), today);
        removePhrase(workingText, "yesterday");
        return;
    }
    if (containsPhrase(normalizedQuery, "today")) {
        setRange(parsed, today, tomorrow);
        removePhrase(workingText, "today");
        return;
    }
    if (containsPhrase(normalizedQuery, "a week ago") ||
        containsPhrase(normalizedQuery, "one week ago")) {
        setRange(parsed, addDays(today, -7), addDays(today, -6));
        removePhrase(workingText, "a week ago");
        removePhrase(workingText, "one week ago");
        return;
    }
    if (containsPhrase(normalizedQuery, "last week") ||
        containsPhrase(normalizedQuery, "past week")) {
        setRange(parsed, addDays(today, -7), tomorrow);
        removePhrase(workingText, "last week");
        removePhrase(workingText, "past week");
        return;
    }
    if (containsPhrase(normalizedQuery, "this week")) {
        setRange(parsed, addDays(today, -6), tomorrow);
        removePhrase(workingText, "this week");
        return;
    }
    if (containsPhrase(normalizedQuery, "last month")) {
        const auto thisMonth = addMonths(today, 0);
        setRange(parsed, addMonths(thisMonth, -1), thisMonth);
        removePhrase(workingText, "last month");
        return;
    }
    if (containsPhrase(normalizedQuery, "this month")) {
        const auto thisMonth = addMonths(today, 0);
        setRange(parsed, thisMonth, addMonths(thisMonth, 1));
        removePhrase(workingText, "this month");
        return;
    }
    if (containsPhrase(normalizedQuery, "last year")) {
        const auto thisYear = addYears(today, 0);
        setRange(parsed, addYears(thisYear, -1), thisYear);
        removePhrase(workingText, "last year");
        return;
    }
    if (containsPhrase(normalizedQuery, "this year")) {
        const auto thisYear = addYears(today, 0);
        setRange(parsed, thisYear, addYears(thisYear, 1));
        removePhrase(workingText, "this year");
    }
}

void parseTypes(ParsedSearchQuery& parsed,
                std::string& workingText,
                const std::string& normalizedQuery) {
    static constexpr TypeRule rules[] = {
        {"paper", "paper|thesis|article|publication|preprint", ".pdf", "media_type:document|document_type:paper"},
        {"document", "document|doc|word document", ".pdf .doc .docx .txt .md .rtf", "media_type:document"},
        {"presentation", "presentation|slides|slide deck|powerpoint|ppt", ".ppt .pptx .odp .key .pdf", "document_type:presentation|media_type:document"},
        {"spreadsheet", "spreadsheet|excel|sheet", ".xls .xlsx .ods .csv .tsv", "document_type:spreadsheet|media_type:document"},
        {"note", "note|notes|memo", ".txt .md .rtf", "document_type:note|media_type:document"},
        {"photo", "photo|photos|image|images|picture|pictures|screenshot|screenshots|diagram|diagrams", ".jpg .jpeg .png .gif .bmp .webp .heic .heif .svg", "media_type:image"},
        {"video", "video|movie|clip|recording", ".mp4 .mov .avi .mkv .webm .wmv", "media_type:video"},
        {"audio", "audio|music|song|voice|podcast", ".mp3 .wav .flac .aac .ogg .m4a", "media_type:audio"},
        {"archive", "archive|zip|compressed|backup", ".zip .rar .7z .tar .gz .tgz", "media_type:archive"},
        {"code", "code|source|script|program", ".c .cc .cpp .h .hpp .py .js .ts .java .cs .go .rs .rb .php .sh .ps1 .sql", "media_type:code"},
        {"pdf", "pdf", ".pdf", "extension:pdf|media_type:document"},
        {"docx", "docx", ".docx", "extension:docx|media_type:document"},
        {"txt", "txt|text file|plain text", ".txt", "extension:txt|media_type:document"},
        {"png", "png", ".png", "extension:png|media_type:image"},
        {"jpg", "jpg|jpeg", ".jpg .jpeg", "extension:jpg|extension:jpeg|media_type:image"},
    };

    for (const auto& rule : rules) {
        if (!anyTermMatches(normalizedQuery, rule.terms)) {
            continue;
        }
        splitChoices(rule.extensions, parsed.extensions);
        splitPipeChoices(rule.reflectedTags, parsed.reflectedTags);
        removeTerms(workingText, rule.terms);
    }

    std::istringstream stream(normalizedQuery);
    std::string token;
    while (stream >> token) {
        if (token.size() > 1 && token.front() == '.') {
            pushUnique(parsed.extensions, token);
            pushUnique(parsed.reflectedTags, "extension:" + token.substr(1));
            removePhrase(workingText, token);
        }
    }
}

void parseSubjectTags(ParsedSearchQuery& parsed, const std::string& normalizedQuery) {
    static constexpr TypeRule subjects[] = {
        {"machine learning", "machine learning|deep learning|neural network|ml", "", "subject:machine learning"},
        {"natural language processing", "natural language processing|nlp|language model|llm|embedding|tokenizer", "", "subject:natural language processing"},
        {"operating systems", "operating system|operating systems|os", "", "subject:operating systems"},
        {"software engineering", "software engineering|srs|requirements|uml|agile", "", "subject:software engineering"},
        {"database", "database|sql|sqlite|transaction", "", "subject:database"},
        {"algorithms", "algorithm|algorithms|data structure|graph", "", "subject:algorithms"},
        {"computer networks", "network|networks|protocol|tcp|udp|http", "", "subject:computer networks"},
        {"security", "security|cryptography|privacy|malware", "", "subject:security"},
    };

    for (const auto& subject : subjects) {
        if (anyTermMatches(normalizedQuery, subject.terms)) {
            splitPipeChoices(subject.reflectedTags, parsed.reflectedTags);
        }
    }
}

std::vector<std::string> extractKeywords(const std::string& semanticText) {
    static const std::set<std::string> stopWords = {
        "a", "an", "and", "are", "by", "for", "from", "i", "in", "is", "it",
        "me", "my", "of", "on", "or", "that", "the", "to", "with", "downloaded",
        "created", "modified", "file", "files", "find", "search", "show"
    };

    std::vector<std::string> keywords;
    std::istringstream stream(normalizeSeparators(semanticText));
    std::string token;
    while (stream >> token) {
        if (token.size() < 2 || stopWords.count(token) != 0) {
            continue;
        }
        pushUnique(keywords, token);
    }
    return keywords;
}

} // namespace

ParsedSearchQuery parseSearchQuery(const std::string& query) {
    return parseSearchQuery(query, std::chrono::system_clock::now());
}

ParsedSearchQuery parseSearchQuery(const std::string& query,
                                   std::chrono::system_clock::time_point now) {
    ParsedSearchQuery parsed;
    parsed.originalText = query;

    std::string workingText = toLowerAscii(query);
    const std::string normalizedQuery = normalizeSeparators(workingText);

    parseDateRange(parsed, workingText, normalizedQuery, now);
    parseTypes(parsed, workingText, normalizedQuery);
    parseSubjectTags(parsed, normalizedQuery);

    parsed.semanticText = collapseSpaces(trimSpaces(normalizeSeparators(workingText)));
    if (parsed.semanticText.empty()) {
        parsed.semanticText = collapseSpaces(trimSpaces(normalizedQuery));
    }
    parsed.keywords = extractKeywords(parsed.semanticText);

    for (const auto& keyword : parsed.keywords) {
        pushUnique(parsed.reflectedTags, "keyword:" + keyword);
    }

    return parsed;
}

} // namespace search
