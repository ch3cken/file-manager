#include "config/app_config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path makeTempRoot(const std::string& name) {
    fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void testLoadOrCreateConfig() {
    const fs::path root = makeTempRoot("fm_config_create");
    const fs::path configPath = root / "config.json";

    const auto config = config::loadOrCreateConfig(configPath);

    assert(fs::exists(configPath));
    assert(config.databasePath.find("local_database.db") != std::string::npos);
    assert(config.globalHotkey == "Alt+Space");
    assert(config.theme == "dark");

    fs::remove_all(root);
    std::cout << "[PASS] testLoadOrCreateConfig\n";
}

void testSaveLoadRoundTrip() {
    const fs::path root = makeTempRoot("fm_config_roundtrip");
    const fs::path configPath = root / "config.json";

    config::AppConfig written = config::defaultConfig(root);
    written.databasePath = (root / "data" / "index.db").string();
    written.quickSearchDirectories = {"C:/Users/test/Documents"};
    written.smartSearchDirectories = {"C:/Users/test/Downloads"};
    written.smartSearchExtensions = {"PDF", ".TXT", "pdf"};
    written.categorizationDirectories = {"C:/Users/test/Downloads"};
    written.globalHotkey = "Ctrl+Space";
    written.theme = "light";
    assert(config::saveConfig(configPath, written));

    const auto loaded = config::loadConfig(configPath);

    assert(loaded.databasePath == written.databasePath);
    assert(loaded.quickSearchDirectories.size() == 1);
    assert(loaded.smartSearchDirectories.size() == 1);
    assert(loaded.smartSearchExtensions.size() == 2);
    assert(loaded.smartSearchExtensions[0] == ".pdf");
    assert(loaded.smartSearchExtensions[1] == ".txt");
    assert(loaded.globalHotkey == "Ctrl+Space");
    assert(loaded.theme == "light");

    fs::remove_all(root);
    std::cout << "[PASS] testSaveLoadRoundTrip\n";
}

void testWritableDirectoryAndCopy() {
    const fs::path root = makeTempRoot("fm_config_copy");
    const fs::path source = root / "source.db";
    const fs::path destination = root / "nested" / "target.db";

    {
        std::ofstream output(source, std::ios::binary);
        output << "sqlite bytes";
    }

    std::string error;
    assert(config::verifyWritableDirectory(root / "nested"));
    assert(config::copyDatabaseFile(source, destination, &error));
    assert(error.empty());
    assert(fs::exists(destination));
    assert(fs::file_size(source) == fs::file_size(destination));

    fs::remove_all(root);
    std::cout << "[PASS] testWritableDirectoryAndCopy\n";
}

void testCategorizationScopeProjection() {
    config::AppConfig appConfig;
    appConfig.categorizationDirectories = {"C:/watched"};
    appConfig.smartSearchExtensions = {".pdf"};
    appConfig.includeSubdirectories = false;

    const auto scope = config::toCategorizationScope(appConfig);
    assert(scope.watchedDirectories.size() == 1);
    assert(scope.targetExtensions.size() == 1);
    assert(!scope.includeSubdirectories);
    std::cout << "[PASS] testCategorizationScopeProjection\n";
}

} // namespace

int main() {
    std::cout << "=== App Config Tests ===\n";
    testLoadOrCreateConfig();
    testSaveLoadRoundTrip();
    testWritableDirectoryAndCopy();
    testCategorizationScopeProjection();
    std::cout << "All app config tests passed.\n";
    return 0;
}
