#include "core/indexer.h"
#include <filesystem>
#include <iostream>
#include <chrono>
#include <format>
#include <sstream>

namespace fs = std::filesystem;

Indexer::Indexer(DatabaseManager& database) : db(database) {}

// Helper function to format file time to string
std::string formatFileTime(const fs::file_time_type& ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
    
    // Convert to string safely (thread-safe formatting isn't strictly needed for this prototype, but standard strftime is fine)
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&cftime));
    return std::string(buffer);
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
