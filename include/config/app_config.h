#pragma once

#include "categorization/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace config {

struct AppConfig {
    std::string appInstallationPath;
    std::string databasePath;
    std::vector<std::string> quickSearchDirectories;
    std::vector<std::string> smartSearchDirectories;
    std::vector<std::string> smartSearchExtensions;
    std::vector<std::string> categorizationDirectories;
    std::string globalHotkey = "Alt+Space";
    std::string theme = "dark";
    bool includeSubdirectories = true;
};

std::filesystem::path defaultConfigPath();
AppConfig defaultConfig(const std::filesystem::path& appDirectory = std::filesystem::current_path());
AppConfig loadConfig(const std::filesystem::path& configPath);
AppConfig loadOrCreateConfig(const std::filesystem::path& configPath = defaultConfigPath());
bool saveConfig(const std::filesystem::path& configPath,
                const AppConfig& config,
                std::string* errorMessage = nullptr);
bool verifyWritableDirectory(const std::filesystem::path& directory);
bool copyDatabaseFile(const std::filesystem::path& sourcePath,
                      const std::filesystem::path& destinationPath,
                      std::string* errorMessage = nullptr);
categorization::CategorizationScope toCategorizationScope(const AppConfig& config);

} // namespace config
