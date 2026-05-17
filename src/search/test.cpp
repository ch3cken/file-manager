#include <iostream>
#include <chrono>
#include "core/database.h"
#include "search/quick_search.h"

int main() {
    std::cout << "Starting Quick Search Test Module..." << std::endl;
    
    // Connect to the existing local database
    DatabaseManager db("local_database.db");
    QuickSearch searcher(db);
    
    std::string query;
    std::cout << "Enter your query: ";
    std::getline(std::cin, query);
    
    std::cout << "\nExecuting Quick Search for: '" << query << "'" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto results = searcher.search(query);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Found " << results.size() << " results in " << duration.count() << "ms:" << std::endl;
    for (const auto& res : results) {
        std::cout << " - [" << res.last_modified << "] " << res.file_name << " (" << res.file_path << ")" << std::endl;
    }
    
    return 0;
}
