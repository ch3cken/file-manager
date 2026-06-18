using System.Text.Json;
using File2Manager.Core.Models;

namespace File2Manager.Core.Services;

public sealed class ConfigService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    public ConfigService(string applicationDirectory)
    {
        ApplicationDirectory = Path.GetFullPath(applicationDirectory);
        ConfigPath = Path.Combine(ApplicationDirectory, "config.json");
    }

    public string ApplicationDirectory { get; }

    public string ConfigPath { get; }

    public AppConfig Load()
    {
        if (!File.Exists(ConfigPath))
        {
            var defaultConfig = AppConfig.CreateDefault(ApplicationDirectory);
            defaultConfig.Normalize();
            return defaultConfig;
        }

        var json = File.ReadAllText(ConfigPath);
        var config = JsonSerializer.Deserialize<AppConfig>(json, JsonOptions) ?? AppConfig.CreateDefault(ApplicationDirectory);
        config.Normalize();
        return config;
    }

    public void Save(AppConfig config)
    {
        config.Normalize();
        Directory.CreateDirectory(ApplicationDirectory);
        File.WriteAllText(ConfigPath, JsonSerializer.Serialize(config, JsonOptions));
    }

    public void VerifyWritableDirectory(string directoryPath)
    {
        if (string.IsNullOrWhiteSpace(directoryPath))
        {
            throw new InvalidOperationException("A directory path is required.");
        }

        var fullPath = Path.GetFullPath(Environment.ExpandEnvironmentVariables(directoryPath));
        Directory.CreateDirectory(fullPath);
        var probeFile = Path.Combine(fullPath, ".file2manager-write-test");
        File.WriteAllText(probeFile, "ok");
        File.Delete(probeFile);
    }

    public void MoveDatabaseIfNeeded(AppConfig oldConfig, AppConfig newConfig)
    {
        oldConfig.Normalize();
        newConfig.Normalize();

        var oldFile = DatabaseService.GetDatabaseFilePath(oldConfig);
        var newFile = DatabaseService.GetDatabaseFilePath(newConfig);

        if (string.Equals(oldFile, newFile, StringComparison.OrdinalIgnoreCase) || !File.Exists(oldFile))
        {
            return;
        }

        Directory.CreateDirectory(Path.GetDirectoryName(newFile)!);
        File.Copy(oldFile, newFile, overwrite: true);
    }
}
