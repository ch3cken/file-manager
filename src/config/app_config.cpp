#include "config/app_config.h"

#include "core/path_utils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

namespace config {
namespace {

using json = nlohmann::json;

std::string pathToUtf8(const std::filesystem::path& path) {
    return pathutil::toUtf8(std::filesystem::absolute(path));
}

std::vector<std::string> stringArray(const json& value) {
    std::vector<std::string> result;
    if (!value.is_array()) {
        return result;
    }
    for (const auto& item : value) {
        if (item.is_string()) {
            std::string text = item.get<std::string>();
            if (!text.empty() && std::find(result.begin(), result.end(), text) == result.end()) {
                result.push_back(std::move(text));
            }
        }
    }
    return result;
}

std::string getStringOr(const json& value, const char* key, std::string fallback) {
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
}

bool getBoolOr(const json& value, const char* key, bool fallback) {
    const auto it = value.find(key);
    return it != value.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void normalizeExtensions(std::vector<std::string>& extensions) {
    std::vector<std::string> normalized;
    for (std::string extension : extensions) {
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (!extension.empty() && extension.front() != '.') {
            extension.insert(extension.begin(), '.');
        }
        if (!extension.empty() && std::find(normalized.begin(), normalized.end(), extension) == normalized.end()) {
            normalized.push_back(std::move(extension));
        }
    }
    extensions = std::move(normalized);
}

json toJson(const AppConfig& config) {
    return json{
        {"appInstallationPath", config.appInstallationPath},
        {"databasePath", config.databasePath},
        {"quickSearchDirectories", config.quickSearchDirectories},
        {"smartSearchDirectories", config.smartSearchDirectories},
        {"smartSearchExtensions", config.smartSearchExtensions},
        {"categorizationDirectories", config.categorizationDirectories},
        {"globalHotkey", config.globalHotkey},
        {"theme", config.theme},
        {"includeSubdirectories", config.includeSubdirectories}
    };
}

AppConfig fromJson(const json& value, const std::filesystem::path& appDirectory) {
    AppConfig fallback = defaultConfig(appDirectory);
    AppConfig config;
    config.appInstallationPath = getStringOr(value, "appInstallationPath", fallback.appInstallationPath);
    config.databasePath = getStringOr(value, "databasePath", fallback.databasePath);
    config.quickSearchDirectories = stringArray(value.value("quickSearchDirectories", json::array()));
    config.smartSearchDirectories = stringArray(value.value("smartSearchDirectories", json::array()));
    config.smartSearchExtensions = stringArray(value.value("smartSearchExtensions", json::array()));
    config.categorizationDirectories = stringArray(value.value("categorizationDirectories", json::array()));
    config.globalHotkey = getStringOr(value, "globalHotkey", fallback.globalHotkey);
    config.theme = getStringOr(value, "theme", fallback.theme);
    config.includeSubdirectories = getBoolOr(value, "includeSubdirectories", fallback.includeSubdirectories);

    if (config.appInstallationPath.empty()) {
        config.appInstallationPath = fallback.appInstallationPath;
    }
    if (config.databasePath.empty()) {
        config.databasePath = fallback.databasePath;
    }
    if (config.smartSearchExtensions.empty()) {
        config.smartSearchExtensions = fallback.smartSearchExtensions;
    }
    normalizeExtensions(config.smartSearchExtensions);
    return config;
}

} // namespace

std::filesystem::path defaultConfigPath() {
    return std::filesystem::current_path() / "config.json";
}

AppConfig defaultConfig(const std::filesystem::path& appDirectory) {
    const std::filesystem::path appPath = std::filesystem::absolute(appDirectory);
    AppConfig config;
    config.appInstallationPath = pathToUtf8(appPath);
    config.databasePath = pathToUtf8(appPath / "local_database.db");
    config.smartSearchExtensions = {".pdf", ".txt", ".md", ".docx"};
    config.globalHotkey = "Alt+Space";
    config.theme = "dark";
    config.includeSubdirectories = true;
    return config;
}

AppConfig loadConfig(const std::filesystem::path& configPath) {
    std::ifstream input(configPath);
    if (!input) {
        throw std::runtime_error("Failed to open config file: " + pathutil::toUtf8(configPath));
    }

    json value;
    input >> value;
    return fromJson(value, std::filesystem::absolute(configPath).parent_path());
}

bool saveConfig(const std::filesystem::path& configPath,
                const AppConfig& config,
                std::string* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }

    std::error_code ec;
    const auto parent = std::filesystem::absolute(configPath).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (errorMessage) {
                *errorMessage = ec.message();
            }
            return false;
        }
    }

    std::ofstream output(configPath, std::ios::trunc);
    if (!output) {
        if (errorMessage) {
            *errorMessage = "failed to open config for writing";
        }
        return false;
    }
    output << toJson(config).dump(4) << '\n';
    return output.good();
}

AppConfig loadOrCreateConfig(const std::filesystem::path& configPath) {
    if (std::filesystem::exists(configPath)) {
        return loadConfig(configPath);
    }

    AppConfig config = defaultConfig(std::filesystem::absolute(configPath).parent_path());
    std::string errorMessage;
    if (!saveConfig(configPath, config, &errorMessage)) {
        throw std::runtime_error("Failed to create config file: " + errorMessage);
    }
    return config;
}

bool verifyWritableDirectory(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec || !std::filesystem::is_directory(directory, ec)) {
        return false;
    }

    const auto probe = directory / ".file_manager_write_test.tmp";
    {
        std::ofstream output(probe, std::ios::trunc);
        if (!output) {
            return false;
        }
        output << "ok";
    }
    std::filesystem::remove(probe, ec);
    return true;
}

bool copyDatabaseFile(const std::filesystem::path& sourcePath,
                      const std::filesystem::path& destinationPath,
                      std::string* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }
    std::error_code ec;
    const auto destinationDirectory = std::filesystem::absolute(destinationPath).parent_path();
    if (!verifyWritableDirectory(destinationDirectory)) {
        if (errorMessage) {
            *errorMessage = "destination directory is not writable";
        }
        return false;
    }
    std::filesystem::copy_file(sourcePath,
                               destinationPath,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
        if (errorMessage) {
            *errorMessage = ec.message();
        }
        return false;
    }
    return true;
}

categorization::CategorizationScope toCategorizationScope(const AppConfig& config) {
    categorization::CategorizationScope scope;
    scope.watchedDirectories = config.categorizationDirectories;
    scope.targetExtensions = config.smartSearchExtensions;
    scope.includeSubdirectories = config.includeSubdirectories;
    return scope;
}

} // namespace config
