#include <iostream>
#include <chrono>
#include "core/database.h"
#include "core/indexer.h"

int main() {
    std::cout << "Starting FileManager Engine..." << std::endl;
    
    // Initialize the database in the local directory for testing
    // In production, this path will be read from config.json
    DatabaseManager db("local_database.db");
    std::cout << "Database initialized." << std::endl;
    
    // Test Phase: Index the current project directory
    Indexer indexer(db);
    std::string testPath = "./src"; // Scanning the src folder as a test
    indexer.scanDirectory(testPath);
    
    // Test Phase: Run a quick search
    std::string query = "main";
    std::cout << "\nExecuting Quick Search for: '" << query << "'" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto results = db.quickSearch(query);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Found " << results.size() << " results in " << duration.count() << "ms:" << std::endl;
    for (const auto& res : results) {
        std::cout << " - [" << res.last_modified << "] " << res.file_name << " (" << res.file_path << ")" << std::endl;
    }
    
    return 0;
}
