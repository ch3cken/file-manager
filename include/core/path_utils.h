#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace pathutil {

inline std::string toUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

inline std::filesystem::path fromUtf8(std::string_view value) {
#ifdef _WIN32
    return std::filesystem::u8path(value);
#else
    return std::filesystem::path(std::string(value));
#endif
}

} // namespace pathutil
