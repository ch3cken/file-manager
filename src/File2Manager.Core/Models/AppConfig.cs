namespace File2Manager.Core.Models;

public sealed class AppConfig
{
    public string InstallationPath { get; set; } = string.Empty;

    public string DatabasePath { get; set; } = string.Empty;

    public List<string> QuickSearchDirectories { get; set; } = new();

    public List<string> SmartSearchDirectories { get; set; } = new();

    public List<string> SmartSearchExtensions { get; set; } = new()
    {
        ".pdf",
        ".docx",
        ".txt",
        ".md",
        ".csv"
    };

    public List<string> CategorizationDirectories { get; set; } = new();

    public string Theme { get; set; } = "Dark";

    public string HotkeyGesture { get; set; } = "Alt+Space";

    public bool IndexAllFixedDrivesForQuickSearch { get; set; }

    public bool IsConfigured { get; set; }

    public void Normalize()
    {
        InstallationPath = NormalizeDirectoryOrFallback(InstallationPath, AppContext.BaseDirectory);
        DatabasePath = NormalizeDirectoryOrFallback(DatabasePath, Path.Combine(AppContext.BaseDirectory, "data"));
        HotkeyGesture = string.IsNullOrWhiteSpace(HotkeyGesture) ? "Alt+Space" : HotkeyGesture.Trim();
        Theme = "Dark";

        QuickSearchDirectories = NormalizeDirectories(QuickSearchDirectories);
        SmartSearchDirectories = NormalizeDirectories(SmartSearchDirectories);
        CategorizationDirectories = NormalizeDirectories(CategorizationDirectories);
        SmartSearchExtensions = NormalizeExtensions(SmartSearchExtensions);
    }

    public static AppConfig CreateDefault(string applicationDirectory)
    {
        var knownDirectories = GetDefaultUserDirectories();

        return new AppConfig
        {
            InstallationPath = applicationDirectory,
            DatabasePath = Path.Combine(applicationDirectory, "data"),
            QuickSearchDirectories = knownDirectories.ToList(),
            SmartSearchDirectories = knownDirectories.ToList(),
            CategorizationDirectories = knownDirectories.ToList(),
            Theme = "Dark",
            HotkeyGesture = "Alt+Space",
            IndexAllFixedDrivesForQuickSearch = false,
            IsConfigured = false
        };
    }

    public IEnumerable<string> GetEffectiveQuickSearchDirectories()
    {
        if (IndexAllFixedDrivesForQuickSearch)
        {
            foreach (var drive in DriveInfo.GetDrives())
            {
                if (drive.IsReady && drive.DriveType == DriveType.Fixed)
                {
                    yield return drive.RootDirectory.FullName;
                }
            }

            yield break;
        }

        foreach (var directory in QuickSearchDirectories
                     .Concat(SmartSearchDirectories)
                     .Concat(CategorizationDirectories)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            yield return directory;
        }
    }

    private static List<string> GetDefaultUserDirectories()
    {
        var candidates = new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
            Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads")
        };

        return candidates
            .Where(directory => !string.IsNullOrWhiteSpace(directory) && Directory.Exists(directory))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static List<string> NormalizeDirectories(IEnumerable<string> directories)
    {
        return directories
            .Where(directory => !string.IsNullOrWhiteSpace(directory))
            .Select(directory => NormalizeDirectoryOrFallback(directory, directory))
            .Where(Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static List<string> NormalizeExtensions(IEnumerable<string> extensions)
    {
        return extensions
            .Where(extension => !string.IsNullOrWhiteSpace(extension))
            .Select(extension => extension.Trim().ToLowerInvariant())
            .Select(extension => extension.StartsWith('.') ? extension : "." + extension)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static string NormalizeDirectoryOrFallback(string directory, string fallback)
    {
        var candidate = string.IsNullOrWhiteSpace(directory) ? fallback : directory.Trim();
        return Path.GetFullPath(Environment.ExpandEnvironmentVariables(candidate));
    }
}
