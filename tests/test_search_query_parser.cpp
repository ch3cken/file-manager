#include "search/query_parser.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::chrono::system_clock::time_point localTime(int year, int month, int day, int hour = 12) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

void testParsesYesterdayAndPaper() {
    const auto parsed = search::parseSearchQuery("machine learning paper downloaded yesterday",
                                                 localTime(2026, 5, 30));

    assert(parsed.hasDateFilter());
    assert(parsed.modifiedRange.startInclusive == "2026-05-29 00:00:00");
    assert(parsed.modifiedRange.endExclusive == "2026-05-30 00:00:00");
    assert(contains(parsed.extensions, ".pdf"));
    assert(contains(parsed.reflectedTags, "subject:machine learning"));
    assert(contains(parsed.reflectedTags, "document_type:paper"));
    assert(contains(parsed.keywords, "machine"));
    assert(contains(parsed.keywords, "learning"));
    std::cout << "[PASS] testParsesYesterdayAndPaper\n";
}

void testParsesAWeekAgoAsSingleDay() {
    const auto parsed = search::parseSearchQuery("operating systems assignment a week ago",
                                                 localTime(2026, 5, 30));

    assert(parsed.hasDateFilter());
    assert(parsed.modifiedRange.startInclusive == "2026-05-23 00:00:00");
    assert(parsed.modifiedRange.endExclusive == "2026-05-24 00:00:00");
    assert(contains(parsed.reflectedTags, "subject:operating systems"));
    std::cout << "[PASS] testParsesAWeekAgoAsSingleDay\n";
}

void testParsesFileTypeSynonyms() {
    const auto parsed = search::parseSearchQuery("screenshots from last month",
                                                 localTime(2026, 5, 30));

    assert(parsed.hasDateFilter());
    assert(parsed.modifiedRange.startInclusive == "2026-04-01 00:00:00");
    assert(parsed.modifiedRange.endExclusive == "2026-05-01 00:00:00");
    assert(contains(parsed.extensions, ".png"));
    assert(contains(parsed.extensions, ".jpg"));
    assert(contains(parsed.reflectedTags, "media_type:image"));
    std::cout << "[PASS] testParsesFileTypeSynonyms\n";
}

void testExplicitExtension() {
    const auto parsed = search::parseSearchQuery("budget .xlsx");
    assert(contains(parsed.extensions, ".xlsx"));
    assert(contains(parsed.reflectedTags, "extension:xlsx"));
    assert(contains(parsed.keywords, "budget"));
    std::cout << "[PASS] testExplicitExtension\n";
}

} // namespace

int main() {
    std::cout << "=== Search Query Parser Tests ===\n";
    testParsesYesterdayAndPaper();
    testParsesAWeekAgoAsSingleDay();
    testParsesFileTypeSynonyms();
    testExplicitExtension();
    std::cout << "All search query parser tests passed.\n";
    return 0;
}
