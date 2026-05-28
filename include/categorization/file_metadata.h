#pragma once

/* ==========================================================================
 *  SRS Feature Map - Filesystem Metadata Collection
 * --------------------------------------------------------------------------
 *  Implements the metadata half of SRS 4.2 Categorization:
 *    REQ-4.2.3.3  [Metadata Collection] -> name, path, extension, dates, size
 *    REQ-4.2.3.4  [Primary Categorization] -> normalized path/extension inputs
 *    REQ-4.2.3.12 [Information Update] -> fresh metadata for recategorization
 *    REQ-4.2.3.13 [Exception Handling] -> short reasons for missing/bad files
 * --------------------------------------------------------------------------
 *  NOTE: The functions in this module are strictly read-only operations 
 *  that query filesystem metadata. They do not perform any side effects 
 *  such as moving, modifying, or renaming user files.
 * ========================================================================== */
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "text_utils.h"
#include "types.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace categorization {
    /// REQ-4.2.3.3 / REQ-4.2.3.4: normalize extensions to lowercase dotted form, such as ".pdf".
    inline std::string normalizeExtension(std::string ext) {
        ext = toLower(trim(ext));
        if (!ext.empty() && ext.front() != '.')
            ext.insert(ext.begin(), '.');
        return ext;
    }

    /// REQ-4.2.3.3: extract the file extension without treating dotted directory names as extensions.
    inline std::string extensionFromPath(const std::string& path) {
        const auto dot = path.find_last_of('.');
        const auto slash = path.find_last_of("/\\");
        return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? normalizeExtension(path.substr(dot)) : std::string{};
    }

    /// REQ-4.2.3.3: read extension from a file-like object, falling back to its path/name when the field is empty.
    template <typename FileLike>
    inline std::string extensionOf(const FileLike& file) {
        std::string ext = normalizeExtension(file.extension);
        if (!ext.empty())
            return ext;
        return extensionFromPath(file.filePath.empty() ? file.fileName : file.filePath);
    }

    /// REQ-4.2.3.1 / REQ-4.3.3.3: canonical path form for watched-directory scope comparisons.
    inline std::string normalizePath(std::string path) {
        path = toLower(trim(std::move(path)));
        std::replace(path.begin(), path.end(), '\\', '/');
        while (path.size() > 1 && path.back() == '/')
            path.pop_back();
        return path;
    }

    /// REQ-4.2.3.1: decide whether a file belongs to a watched directory, optionally including subdirectories.
    inline bool pathMatchesDirectory(const std::string& path, const std::string& directory, bool includeSubdirectories) {
        const std::string fp = normalizePath(path), root = normalizePath(directory);
        if (fp.empty() || root.empty())
            return false;
        if (!includeSubdirectories) {
            const auto slash = fp.find_last_of('/');
            return slash != std::string::npos && fp.substr(0, slash) == root;
        }
        return fp == root || (fp.size() > root.size() && fp.compare(0, root.size(), root) == 0 && fp[root.size()] == '/');
    }

    /// REQ-4.2.3.3: format creation/modified timestamps for database storage.
    inline std::string formatSystemTime(std::time_t ctime) {
        std::tm tm{};
    #if defined(_WIN32)
        gmtime_s(&tm, &ctime);
    #else
        gmtime_r(&ctime, &tm);
    #endif
        char buf[20] = {};
        return std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm) > 0 ? std::string(buf) : std::string{};
    }

    /// REQ-4.2.3.3: bridge std::filesystem's implementation-defined clock to system_clock before formatting lastModified.
    inline std::string formatFileTime(const std::filesystem::file_time_type& ftime) {
        const auto st = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return formatSystemTime(std::chrono::system_clock::to_time_t(st));
    }

    /// REQ-4.2.3.3: return file creation time where the platform exposes it; modified time is used later as a fallback.
    inline std::string creationDateOf(const std::filesystem::path& path) {
    #if defined(_WIN32)
        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (!GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &attr))
            return {};
        SYSTEMTIME st{};
        if (!FileTimeToSystemTime(&attr.ftCreationTime, &st))
            return {};
        char buf[20] = {};
        const int n = std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        return n > 0 ? std::string(buf) : std::string{};
    #else
        (void)path; return {};
    }
    #endif

    /// REQ-4.2.2.1-B / REQ-4.2.3.3 / REQ-4.2.3.13: collect file name, path, extension, dates, and size; return partial metadata plus an exception reason on failure.
    inline FileMetadata collectMetadata(const std::string& path, std::string* exceptionReason = nullptr) {
        if (exceptionReason)
            exceptionReason->clear();
        FileMetadata m;
        m.filePath = path;
        try {
            const std::filesystem::path fp(path);
            m.fileName  = fp.filename().string();
            m.extension = normalizeExtension(fp.extension().string());

            std::error_code ec;
            if (!std::filesystem::exists(fp, ec)) {
                if (exceptionReason)
                    *exceptionReason = "missing file";
                return m;
            }
            if (!std::filesystem::is_regular_file(fp, ec)) {
                if (exceptionReason)
                    *exceptionReason = "not a regular file";
                return m;
            }

            const auto abs = std::filesystem::absolute(fp, ec);
            const std::filesystem::path metaPath = ec ? fp : (m.filePath = abs.string(), abs);

            if (const auto bytes = std::filesystem::file_size(fp, ec); !ec)
                m.fileSize = bytes;
            if (const auto mtime = std::filesystem::last_write_time(fp, ec); !ec)
                m.lastModified = formatFileTime(mtime);
            m.createdDate = creationDateOf(metaPath);
            if (m.createdDate.empty())
                m.createdDate = m.lastModified;
        } catch (const std::exception&) {
            if (exceptionReason)
                *exceptionReason = "metadata collection failed";
        }
        return m;
    }

    /// REQ-4.2.3.4 / REQ-4.2.3.6: split storage path into components for project/category inference.
    inline std::vector<std::string> pathSegments(const std::string& path) {
        std::vector<std::string> segments;
        std::string seg;
        for (char c : normalizePath(path)) {
            if (c != '/')
                seg.push_back(c);
            else if (!seg.empty()) {
                segments.push_back(std::move(seg)); seg.clear();
            }
        }
        if (!seg.empty())
            segments.push_back(std::move(seg));
        return segments;
    }
}
