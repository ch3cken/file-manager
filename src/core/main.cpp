#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <thread>

#include "config/app_config.h"
#include "core/database.h"
#include "core/indexer.h"
#include "core/path_utils.h"

namespace fs = std::filesystem;

int main() {
    std::cout << "Starting FileManager Engine..." << std::endl;

    config::AppConfig appConfig;
    try {
        appConfig = config::loadOrCreateConfig();
        std::cout << "Loaded config.json from " << config::defaultConfigPath() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Could not load config.json: " << e.what() << std::endl;
        return 1;
    }

    DatabaseManager db(appConfig.databasePath);
    std::cout << "Database initialized." << std::endl;

    Indexer indexer(db, "C:");
    for (const auto& directory : appConfig.quickSearchDirectories) {
        fs::path scanPath = fs::absolute(pathutil::fromUtf8(directory));
        indexer.scanDirectory(scanPath.string());
    }
    if (appConfig.quickSearchDirectories.empty()) {
        std::cout << "No quickSearchDirectories configured; real-time USN sync will update the database when available." << std::endl;
    }

    std::cout << "Initialization and Static Indexing complete." << std::endl;
    std::cout << "Entering background polling loop (Press Ctrl+C to stop)..." << std::endl;

    while (true) {
        indexer.update();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
}
