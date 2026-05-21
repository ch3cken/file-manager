#include "core/indexer.h"
#include "core/usn_journal.h"
#include <filesystem>
#include <iostream>
#include <chrono>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

Indexer::Indexer(DatabaseManager& database, const std::string& driveLetter)
    : db(database), usingUsn(false) {
    journal = std::make_unique<UsnJournal>(db, driveLetter);
    if (journal->init()) {
        usingUsn = true;
    }
}

Indexer::~Indexer() = default;

// Helper function to format file time to string.
// Issue #7: fs::file_time_type on MSVC uses the Windows FILETIME epoch (Jan 1 1601),
// not the Unix epoch. Direct arithmetic with system_clock produces dates ~116 years off.
// Issue #8: std::localtime is not thread-safe; use localtime_s on Windows.
std::string formatFileTime(const fs::file_time_type& ftime) {
#ifdef _WIN32
    // Extract the raw 100-ns tick count (Windows FILETIME epoch)
    auto rawCount = ftime.time_since_epoch().count();
    ULARGE_INTEGER uli;
    uli.QuadPart = static_cast<ULONGLONG>(rawCount);
    FILETIME ft;
    ft.dwLowDateTime  = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    char buffer[80];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buffer);
#else
    // Portable fallback for non-Windows builds
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
    struct tm tmBuf;
    gmtime_r(&cftime, &tmBuf);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buffer);
#endif
}

void Indexer::scanDirectory(const std::string& directoryPath) {
    try {
        if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
            std::cerr << "Invalid directory path: " << directoryPath << std::endl;
            return;
        }

        std::cout << "Starting indexing for directory: " << directoryPath << std::endl;
        int count = 0;

        for (const auto& entry : fs::recursive_directory_iterator(directoryPath, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                FileRecord record;
                record.file_path = entry.path().string();
                record.file_name = entry.path().filename().string();
                
                // Extract extension (e.g. ".pdf", ".txt")
                if (entry.path().has_extension()) {
                    record.extension = entry.path().extension().string();
                }
                
                try {
                    record.last_modified = formatFileTime(entry.last_write_time());
                } catch (const std::exception& e) {
                    record.last_modified = "1970-01-01 00:00:00"; // fallback
                }

                // Insert into the database
                if (db.insertFile(record)) {
                    count++;
                }
            }
        }
        
        std::cout << "Indexing complete! Added " << count << " files." << std::endl;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error during indexing: " << e.what() << std::endl;
    }
}

void Indexer::update() {
    if (usingUsn && journal) {
        journal->pollChanges();
    }
}
