using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using File2Manager.Core.Models;
using File2Manager.Core.Services;
using Forms = System.Windows.Forms;

namespace FileManager.Installer;

public partial class InstallerWindow
{
    private bool _databasePathTouched;
    private bool _isInstalling;

    public InstallerWindow()
    {
        InitializeComponent();
        InitializeDefaults();
    }

    private void InitializeDefaults()
    {
        var defaultInstallPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "FileManager");

        InstallPathTextBox.Text = defaultInstallPath;
        DatabasePathTextBox.Text = Path.Combine(defaultInstallPath, "data");

        foreach (var directory in GetDefaultIndexDirectories())
        {
            IndexFolderList.Items.Add(directory);
        }
    }

    private void BrowseInstallPath_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var selectedPath = SelectFolder("Choose installation folder", InstallPathTextBox.Text);
        if (selectedPath is null)
        {
            return;
        }

        InstallPathTextBox.Text = selectedPath;
    }

    private void BrowseDatabasePath_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var selectedPath = SelectFolder("Choose database folder", DatabasePathTextBox.Text);
        if (selectedPath is null)
        {
            return;
        }

        _databasePathTouched = true;
        DatabasePathTextBox.Text = selectedPath;
    }

    private void AddIndexFolder_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var selectedPath = SelectFolder("Choose folder to index", Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
        if (selectedPath is null)
        {
            return;
        }

        if (!IndexFolderList.Items.Cast<string>().Any(item => item.Equals(selectedPath, StringComparison.OrdinalIgnoreCase)))
        {
            IndexFolderList.Items.Add(selectedPath);
        }
    }

    private void RemoveIndexFolder_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var selected = IndexFolderList.SelectedItem as string;
        if (selected is null)
        {
            return;
        }

        IndexFolderList.Items.Remove(selected);
    }

    private void InstallPathTextBox_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        if (_databasePathTouched || string.IsNullOrWhiteSpace(InstallPathTextBox.Text))
        {
            return;
        }

        DatabasePathTextBox.Text = Path.Combine(InstallPathTextBox.Text.Trim(), "data");
    }

    private void DatabasePathTextBox_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        if (!DatabasePathTextBox.IsKeyboardFocusWithin)
        {
            return;
        }

        _databasePathTouched = true;
    }

    private async void InstallButton_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        if (_isInstalling)
        {
            return;
        }

        try
        {
            _isInstalling = true;
            SetInstallingState(true);
            await InstallAsync();
            StatusTextBlock.Text = "Installation complete.";
            CountTextBlock.Text = string.Empty;
            InstallProgressBar.IsIndeterminate = false;
            InstallProgressBar.Value = 100;
            InstallButton.Content = "Installed";
            InstallButton.IsEnabled = false;
            _isInstalling = false;
            CloseButton.IsEnabled = true;
        }
        catch (Exception exception)
        {
            StatusTextBlock.Text = "Installation failed.";
            CountTextBlock.Text = string.Empty;
            InstallProgressBar.IsIndeterminate = false;
            InstallProgressBar.Value = 0;
            System.Windows.MessageBox.Show(
                exception.Message,
                "FileManager Setup",
                System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Error);
            SetInstallingState(false);
            _isInstalling = false;
        }
    }

    private void CloseButton_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        if (_isInstalling)
        {
            return;
        }

        Close();
    }

    private async Task InstallAsync()
    {
        var sourceDirectory = AppContext.BaseDirectory;
        var payloadZip = Path.Combine(sourceDirectory, "payload.zip");
        if (!File.Exists(payloadZip))
        {
            throw new FileNotFoundException("The installer payload was not found.", payloadZip);
        }

        var installDirectory = NormalizeDirectory(InstallPathTextBox.Text, "installation folder");
        var databaseDirectory = NormalizeDirectory(DatabasePathTextBox.Text, "database folder");
        EnsureSafeInstallDirectory(installDirectory);
        VerifyWritableDirectory(installDirectory);
        VerifyWritableDirectory(databaseDirectory);

        var indexFolders = IndexFolderList.Items
            .Cast<string>()
            .Where(Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        if (RunInitialIndexCheckBox.IsChecked == true && indexFolders.Count == 0)
        {
            throw new InvalidOperationException("Add at least one folder before running the initial index.");
        }

        var stagingDirectory = Path.Combine(Path.GetTempPath(), "FileManager-install-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stagingDirectory);

        try
        {
            StatusTextBlock.Text = "Extracting application files...";
            InstallProgressBar.IsIndeterminate = true;
            await Task.Run(() => ZipFile.ExtractToDirectory(payloadZip, stagingDirectory, overwriteFiles: true));

            StatusTextBlock.Text = "Closing running FileManager if needed...";
            await Task.Run(StopRunningInstalledApp);

            StatusTextBlock.Text = "Preparing installation folder...";
            CountTextBlock.Text = "Replacing existing application files";
            await Task.Run(() => PrepareInstallDirectoryForOverwrite(installDirectory, databaseDirectory));

            StatusTextBlock.Text = "Copying application files...";
            InstallProgressBar.IsIndeterminate = false;
            InstallProgressBar.Value = 0;
            await CopyDirectoryContentsAsync(
                stagingDirectory,
                installDirectory,
                new Progress<InstallCopyProgress>(UpdateCopyProgress));

            StatusTextBlock.Text = "Writing configuration...";
            CountTextBlock.Text = string.Empty;
            var config = BuildConfig(installDirectory, databaseDirectory, indexFolders);
            var configService = new ConfigService(installDirectory);
            configService.Save(config);

            if (RunInitialIndexCheckBox.IsChecked == true)
            {
                await RunInitialIndexAsync(config);
            }
            else
            {
                StatusTextBlock.Text = "Preparing database...";
                CountTextBlock.Text = "Creating search tables";
                InstallProgressBar.IsIndeterminate = true;
                await Task.Run(async () =>
                {
                    var databaseService = new DatabaseService();
                    await databaseService.InitializeAsync(config, backfillQuickTerms: false).ConfigureAwait(false);
                });
            }

            StatusTextBlock.Text = "Creating shortcuts...";
            CountTextBlock.Text = string.Empty;
            var createDesktopShortcut = DesktopShortcutCheckBox.IsChecked == true;
            await Task.Run(() => CreateShortcuts(installDirectory, createDesktopShortcut));

            if (LaunchAfterInstallCheckBox.IsChecked == true)
            {
                StartInstalledApp(installDirectory);
            }
        }
        finally
        {
            if (Directory.Exists(stagingDirectory))
            {
                await Task.Run(() => Directory.Delete(stagingDirectory, recursive: true));
            }
        }
    }

    private void UpdateCopyProgress(InstallCopyProgress progress)
    {
        StatusTextBlock.Text = progress.Message;
        CountTextBlock.Text = $"{FormatBytes(progress.BytesCopied)} / {FormatBytes(progress.TotalBytes)}";
        InstallProgressBar.IsIndeterminate = false;
        InstallProgressBar.Value = progress.TotalBytes <= 0
            ? 0
            : Math.Clamp((double)progress.BytesCopied / progress.TotalBytes * 100, 0, 100);
    }

    private async Task RunInitialIndexAsync(AppConfig config)
    {
        StatusTextBlock.Text = "Preparing search database...";
        CountTextBlock.Text = "Indexing runs in the background. This can take a while for large folders.";
        InstallProgressBar.IsIndeterminate = true;

        var progress = new Progress<IndexingProgress>(UpdateIndexProgress);
        await Task.Run(async () =>
        {
            var databaseService = new DatabaseService();
            var embeddingService = new SemanticEmbeddingService(Path.Combine(config.InstallationPath, "Embeddings"));
            using var indexingService = new FileIndexingService(
                databaseService,
                new DocumentTextExtractor(),
                new CategorizationService(),
                embeddingService);

            await indexingService.BuildIndexAsync(config, progress).ConfigureAwait(false);
        });
    }

    private void UpdateIndexProgress(IndexingProgress progress)
    {
        StatusTextBlock.Text = progress.Message;
        CountTextBlock.Text = $"{progress.FilesIndexed:n0} indexed / {progress.FilesSeen:n0} found";

        if (progress.FilesSeen > 0)
        {
            InstallProgressBar.IsIndeterminate = false;
            InstallProgressBar.Value = Math.Clamp(
                (double)progress.FilesIndexed / progress.FilesSeen * 100,
                0,
                100);
        }

        if (progress.IsComplete)
        {
            InstallProgressBar.IsIndeterminate = false;
            InstallProgressBar.Value = 100;
        }
    }

    private AppConfig BuildConfig(string installDirectory, string databaseDirectory, IReadOnlyList<string> indexFolders)
    {
        var smartEnabled = SmartCategorizationCheckBox.IsChecked == true;

        var config = new AppConfig
        {
            InstallationPath = installDirectory,
            DatabasePath = databaseDirectory,
            QuickSearchDirectories = indexFolders.ToList(),
            SmartSearchDirectories = smartEnabled ? indexFolders.ToList() : new List<string>(),
            CategorizationDirectories = smartEnabled ? indexFolders.ToList() : new List<string>(),
            SmartSearchExtensions = new List<string> { ".pdf", ".docx", ".txt", ".md", ".csv", ".pptx" },
            Theme = "Dark",
            HotkeyGesture = "Alt+Space",
            IndexAllFixedDrivesForQuickSearch = false,
            IsConfigured = true
        };

        config.Normalize();
        return config;
    }

    private void CreateShortcuts(string installDirectory, bool createDesktopShortcut)
    {
        var executablePath = Path.Combine(installDirectory, "FileManager.exe");
        if (!File.Exists(executablePath))
        {
            throw new FileNotFoundException("The installed executable was not found.", executablePath);
        }

        var programsDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Programs);
        var startMenuDirectory = Path.Combine(programsDirectory, "FileManager");
        Directory.CreateDirectory(startMenuDirectory);
        CreateShortcut(
            Path.Combine(startMenuDirectory, "FileManager.lnk"),
            executablePath,
            installDirectory);

        if (createDesktopShortcut)
        {
            CreateShortcut(
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "FileManager.lnk"),
                executablePath,
                installDirectory);
        }
    }

    private static void CreateShortcut(string shortcutPath, string targetPath, string workingDirectory)
    {
        var shellType = Type.GetTypeFromProgID("WScript.Shell");
        if (shellType is null)
        {
            throw new InvalidOperationException("Windows Script Host is required to create shortcuts.");
        }

        dynamic shell = Activator.CreateInstance(shellType)!;
        dynamic shortcut = shell.CreateShortcut(shortcutPath);
        shortcut.TargetPath = targetPath;
        shortcut.WorkingDirectory = workingDirectory;
        shortcut.Description = "FileManager local search";
        shortcut.Save();
    }

    private static void StartInstalledApp(string installDirectory)
    {
        var executablePath = Path.Combine(installDirectory, "FileManager.exe");
        Process.Start(new ProcessStartInfo(executablePath)
        {
            WorkingDirectory = installDirectory,
            UseShellExecute = true
        });
    }

    private static void StopRunningInstalledApp()
    {
        foreach (var process in Process.GetProcessesByName("FileManager"))
        {
            try
            {
                process.CloseMainWindow();
                if (!process.WaitForExit(2000))
                {
                    process.Kill(entireProcessTree: true);
                    process.WaitForExit(2000);
                }
            }
            catch
            {
                // If Windows denies process details, continue and let file copy report any real lock.
            }
            finally
            {
                process.Dispose();
            }
        }
    }

    private static void PrepareInstallDirectoryForOverwrite(string installDirectory, string databaseDirectory)
    {
        Directory.CreateDirectory(installDirectory);

        var installRoot = NormalizePathForComparison(installDirectory);
        var databaseRoot = NormalizePathForComparison(databaseDirectory);
        if (PathsEqual(installRoot, databaseRoot))
        {
            throw new InvalidOperationException("Choose a database folder that is separate from the installation folder, or inside a subfolder of it.");
        }

        foreach (var filePath in Directory.EnumerateFiles(installDirectory))
        {
            DeleteFileAllowingReadOnly(filePath);
        }

        foreach (var directoryPath in Directory.EnumerateDirectories(installDirectory))
        {
            if (ShouldPreserveDirectory(directoryPath, databaseRoot))
            {
                continue;
            }

            DeleteDirectoryAllowingReadOnly(directoryPath);
        }
    }

    private static bool ShouldPreserveDirectory(string directoryPath, string protectedDirectory)
    {
        var directoryRoot = NormalizePathForComparison(directoryPath);
        return PathsEqual(directoryRoot, protectedDirectory) ||
               IsAncestorPath(directoryRoot, protectedDirectory) ||
               IsAncestorPath(protectedDirectory, directoryRoot);
    }

    private static bool IsAncestorPath(string ancestorPath, string candidatePath)
    {
        var ancestorWithSeparator = EnsureTrailingSeparator(ancestorPath);
        return candidatePath.StartsWith(ancestorWithSeparator, StringComparison.OrdinalIgnoreCase);
    }

    private static bool PathsEqual(string firstPath, string secondPath)
    {
        return string.Equals(firstPath, secondPath, StringComparison.OrdinalIgnoreCase);
    }

    private static string NormalizePathForComparison(string path)
    {
        return Path.GetFullPath(path)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    }

    private static string EnsureTrailingSeparator(string path)
    {
        return path.EndsWith(Path.DirectorySeparatorChar)
            ? path
            : path + Path.DirectorySeparatorChar;
    }

    private static void DeleteFileAllowingReadOnly(string filePath)
    {
        File.SetAttributes(filePath, FileAttributes.Normal);
        File.Delete(filePath);
    }

    private static void DeleteDirectoryAllowingReadOnly(string directoryPath)
    {
        foreach (var filePath in Directory.EnumerateFiles(directoryPath, "*", SearchOption.AllDirectories))
        {
            File.SetAttributes(filePath, FileAttributes.Normal);
        }

        foreach (var childDirectory in Directory.EnumerateDirectories(directoryPath, "*", SearchOption.AllDirectories))
        {
            File.SetAttributes(childDirectory, FileAttributes.Directory);
        }

        File.SetAttributes(directoryPath, FileAttributes.Directory);
        Directory.Delete(directoryPath, recursive: true);
    }

    private static async Task CopyDirectoryContentsAsync(
        string sourceDirectory,
        string destinationDirectory,
        IProgress<InstallCopyProgress> progress)
    {
        const int BufferSize = 1024 * 1024;

        Directory.CreateDirectory(destinationDirectory);
        var sourceFiles = Directory
            .EnumerateFiles(sourceDirectory, "*", SearchOption.AllDirectories)
            .Select(path => new FileInfo(path))
            .ToArray();
        var totalBytes = sourceFiles.Sum(file => file.Length);
        var copiedBytes = 0L;
        var buffer = new byte[BufferSize];

        progress.Report(new InstallCopyProgress("Copying application files...", 0, totalBytes));

        foreach (var sourceFile in sourceFiles)
        {
            var relativePath = Path.GetRelativePath(sourceDirectory, sourceFile.FullName);
            var destinationPath = Path.Combine(destinationDirectory, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);

            await using var sourceStream = new FileStream(
                sourceFile.FullName,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                BufferSize,
                useAsync: true);
            await using var destinationStream = new FileStream(
                destinationPath,
                FileMode.Create,
                FileAccess.Write,
                FileShare.None,
                BufferSize,
                useAsync: true);

            int bytesRead;
            while ((bytesRead = await sourceStream.ReadAsync(buffer)) > 0)
            {
                await destinationStream.WriteAsync(buffer.AsMemory(0, bytesRead));
                copiedBytes += bytesRead;
                progress.Report(new InstallCopyProgress(
                    "Copying " + relativePath,
                    copiedBytes,
                    totalBytes));
            }

            File.SetLastWriteTimeUtc(destinationPath, sourceFile.LastWriteTimeUtc);
        }
    }

    private static string? SelectFolder(string title, string initialPath)
    {
        using var dialog = new Forms.FolderBrowserDialog
        {
            Description = title,
            UseDescriptionForTitle = true,
            ShowNewFolderButton = true,
            SelectedPath = Directory.Exists(initialPath)
                ? initialPath
                : Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
        };

        return dialog.ShowDialog() == Forms.DialogResult.OK
            ? dialog.SelectedPath
            : null;
    }

    private static IReadOnlyList<string> GetDefaultIndexDirectories()
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
            .ToArray();
    }

    private static string NormalizeDirectory(string directory, string label)
    {
        if (string.IsNullOrWhiteSpace(directory))
        {
            throw new InvalidOperationException("Choose a " + label + ".");
        }

        return Path.GetFullPath(Environment.ExpandEnvironmentVariables(directory.Trim()));
    }

    private static void EnsureSafeInstallDirectory(string installDirectory)
    {
        var root = Path.GetPathRoot(installDirectory);
        if (string.Equals(installDirectory.TrimEnd(Path.DirectorySeparatorChar), root?.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Choose a folder, not a drive root, for installation.");
        }

        var windowsDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        if (installDirectory.Equals(windowsDirectory, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("The Windows directory cannot be used as the installation folder.");
        }
    }

    private static void VerifyWritableDirectory(string directory)
    {
        Directory.CreateDirectory(directory);
        var probeFile = Path.Combine(directory, ".filemanager-write-test");
        File.WriteAllText(probeFile, "ok");
        File.Delete(probeFile);
    }

    private static string FormatBytes(long bytes)
    {
        string[] units = { "B", "KB", "MB", "GB" };
        var value = (double)bytes;
        var unitIndex = 0;

        while (value >= 1024 && unitIndex < units.Length - 1)
        {
            value /= 1024;
            unitIndex++;
        }

        return value.ToString(unitIndex == 0 ? "0" : "0.0") + " " + units[unitIndex];
    }

    private void SetInstallingState(bool isInstalling)
    {
        InstallButton.IsEnabled = !isInstalling;
        CloseButton.IsEnabled = !isInstalling;
        InstallPathTextBox.IsEnabled = !isInstalling;
        DatabasePathTextBox.IsEnabled = !isInstalling;
        IndexFolderList.IsEnabled = !isInstalling;
        RunInitialIndexCheckBox.IsEnabled = !isInstalling;
        SmartCategorizationCheckBox.IsEnabled = !isInstalling;
        DesktopShortcutCheckBox.IsEnabled = !isInstalling;
        LaunchAfterInstallCheckBox.IsEnabled = !isInstalling;
        if (!isInstalling)
        {
            InstallButton.Content = "Install";
        }
    }

    private readonly record struct InstallCopyProgress(string Message, long BytesCopied, long TotalBytes);
}
