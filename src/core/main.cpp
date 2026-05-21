#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include "core/database.h"
#include "core/indexer.h"

namespace fs = std::filesystem;

int main() {
    std::cout << "Starting FileManager Engine..." << std::endl;
    
    // Initialize the database in the local directory for testing.
    // In production, this path will be read from config.json (SRS §4.3.3.7).
    DatabaseManager db("local_database.db");
    std::cout << "Database initialized." << std::endl;
    
    // Issue #6: drive letter is now configurable; default "C:" used here.
    // In production this will come from config.json.
    Indexer indexer(db, "C:");

    // Issue #23: use absolute path so stored file_path entries are always
    // fully qualified, enabling reliable file launch from the UI.
    fs::path testPath = fs::absolute("./src");
    indexer.scanDirectory(testPath.string());
    
    std::cout << "Initialization and Static Indexing complete." << std::endl;
    std::cout << "Entering background polling loop (Press Ctrl+C to stop)..." << std::endl;
    
    // Background Daemon Loop
    while (true) {
        indexer.update();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    return 0;
}
