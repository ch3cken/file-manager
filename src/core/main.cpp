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
    
    std::cout << "Initialization and Indexing complete." << std::endl;
    return 0;
}
