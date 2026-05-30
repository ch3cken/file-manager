using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using File2Manager.App.Services;
using File2Manager.Core.Models;
using File2Manager.Core.Search;
using File2Manager.Core.Services;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

namespace File2Manager.App;

public partial class MainWindow : Window
{
    private const string SearchBlockedDuringIndexMessage = "Initial indexing is running. Search is disabled until indexing completes.";
    private readonly ConfigService _configService;
    private readonly DatabaseService _databaseService = new();
    private readonly DocumentTextExtractor _textExtractor = new();
    private readonly CategorizationService _categorizationService = new();
    private readonly SearchQueryParser _queryParser = new();
    private readonly DispatcherTimer _searchDebounceTimer;
    private readonly SearchService _searchService;
    private FileIndexingService _indexingService;
    private AppConfig _config;
    private CancellationTokenSource? _searchCancellationTokenSource;
    private CancellationTokenSource? _indexingCancellationTokenSource;
    private OverlayWindow? _overlayWindow;
    private HotkeyService? _hotkeyService;
    private bool _isLoaded;
    private bool _isFullIndexingActive;
    private int _indexingRunId;

    public MainWindow()
    {
        _configService = new ConfigService(AppContext.BaseDirectory);
        _config = _configService.Load();
        _searchService = new SearchService(_databaseService, _queryParser);
        _indexingService = new FileIndexingService(_databaseService, _textExtractor, _categorizationService);

        InitializeComponent();

        _searchDebounceTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(220)
        };
        _searchDebounceTimer.Tick += async (_, _) =>
        {
            _searchDebounceTimer.Stop();
            await RunMainSearchAsync();
        };
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_isLoaded)
        {
            return;
        }

        _isLoaded = true;
        ApplyTheme(_config.Theme);
        PopulateControlsFromConfig(_config);

        try
        {
            await _databaseService.InitializeAsync(_config);
            await RefreshIndexCountAsync();

            if (!_config.IsConfigured)
            {
                ShowSetup();
                HeaderStatusText.Text = "Setup required";
                return;
            }

            StartRuntimeServices();
            if (await _databaseService.CountFilesAsync() == 0)
            {
                BeginIndexRebuild();
            }
        }
        catch (Exception exception)
        {
            HeaderStatusText.Text = exception.Message;
        }
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        _searchCancellationTokenSource?.Cancel();
        _indexingCancellationTokenSource?.Cancel();
        _overlayWindow?.Close();
        _hotkeyService?.Dispose();
        _indexingService.Dispose();
    }

    private void ShowSetup()
    {
        SetupOverlay.Visibility = Visibility.Visible;
        SetupInstallationPathTextBox.Text = _config.InstallationPath;
        SetupDatabasePathTextBox.Text = _config.DatabasePath;
        SetupExtensionsTextBox.Text = string.Join(", ", _config.SmartSearchExtensions);
        ReplaceListBoxItems(SetupSmartDirectoriesListBox, _config.SmartSearchDirectories);
        ReplaceListBoxItems(SetupCategorizationDirectoriesListBox, _config.CategorizationDirectories);
        SetupStatusText.Text = "config.json will be saved next to the application.";
    }

    private async void OnFinishSetup(object sender, RoutedEventArgs e)
    {
        try
        {
            var newConfig = new AppConfig
            {
                InstallationPath = AppContext.BaseDirectory,
                DatabasePath = SetupDatabasePathTextBox.Text,
                SmartSearchExtensions = ParseExtensions(SetupExtensionsTextBox.Text),
                SmartSearchDirectories = ReadListBoxItems(SetupSmartDirectoriesListBox),
                CategorizationDirectories = ReadListBoxItems(SetupCategorizationDirectoriesListBox),
                Theme = "Dark",
                HotkeyGesture = _config.HotkeyGesture,
                IsConfigured = true
            };
            newConfig.QuickSearchDirectories = newConfig.SmartSearchDirectories
                .Concat(newConfig.CategorizationDirectories)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
            newConfig.Normalize();

            _configService.VerifyWritableDirectory(newConfig.DatabasePath);
            _configService.Save(newConfig);
            _config = newConfig;

            SetupOverlay.Visibility = Visibility.Collapsed;
            PopulateControlsFromConfig(_config);
            await _databaseService.InitializeAsync(_config);
            StartRuntimeServices();
            BeginIndexRebuild();
            HeaderStatusText.Text = "Setup complete";
        }
        catch (Exception exception)
        {
            SetupStatusText.Text = exception.Message;
        }
    }

    private void StartRuntimeServices()
    {
        RegisterHotkey();
        _indexingService.StartWatchers(_config, progress =>
        {
            Dispatcher.Invoke(() => ApplyIndexProgress(progress));
        });
        HotkeyDisplayText.Text = _config.HotkeyGesture;
    }

    private void RegisterHotkey()
    {
        try
        {
            _hotkeyService?.Dispose();
            var helper = new WindowInteropHelper(this);
            var handle = helper.Handle == IntPtr.Zero ? helper.EnsureHandle() : helper.Handle;
            _hotkeyService = new HotkeyService(handle);
            _hotkeyService.Register(_config.HotkeyGesture, () =>
            {
                Dispatcher.Invoke(ShowOverlay);
            });
        }
        catch (Exception exception)
        {
            HeaderStatusText.Text = exception.Message;
        }
    }

    private void ShowOverlay()
    {
        if (!_config.IsConfigured)
        {
            ShowSetup();
            return;
        }

        _overlayWindow ??= new OverlayWindow(_searchService, IsSearchBlocked)
        {
            Owner = this
        };
        _overlayWindow.ActivateSearch();
    }

    private void OnOpenOverlay(object sender, RoutedEventArgs e)
    {
        ShowOverlay();
    }

    private void OnNavigateSearch(object sender, RoutedEventArgs e)
    {
        ShowPage(SearchPage);
    }

    private void OnNavigateSettings(object sender, RoutedEventArgs e)
    {
        ShowPage(SettingsPage);
    }

    private void OnNavigateKeywords(object sender, RoutedEventArgs e)
    {
        ShowPage(KeywordsPage);
        _ = ReloadKeywordsAsync();
    }

    private void ShowPage(UIElement activePage)
    {
        SearchPage.Visibility = activePage == SearchPage ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = activePage == SettingsPage ? Visibility.Visible : Visibility.Collapsed;
        KeywordsPage.Visibility = activePage == KeywordsPage ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnSearchQueryChanged(object sender, TextChangedEventArgs e)
    {
        if (!_config.IsConfigured)
        {
            return;
        }

        if (IsSearchBlocked())
        {
            ShowSearchBlockedMessage();
            return;
        }

        _searchDebounceTimer.Stop();
        _searchDebounceTimer.Start();
    }

    private async Task RunMainSearchAsync()
    {
        if (IsSearchBlocked())
        {
            _searchCancellationTokenSource?.Cancel();
            ShowSearchBlockedMessage();
            return;
        }

        var query = SearchQueryTextBox.Text.Trim();
        _searchCancellationTokenSource?.Cancel();
        _searchCancellationTokenSource = new CancellationTokenSource();
        var cancellationToken = _searchCancellationTokenSource.Token;

        try
        {
            HeaderStatusText.Text = string.IsNullOrWhiteSpace(query) ? "Showing recent indexed files" : "Searching...";
            var quickTask = _searchService.QuickSearchAsync(query, 75, cancellationToken);
            var smartTask = _searchService.SmartSearchAsync(query, 75, cancellationToken);
            var quickResults = await quickTask;
            QuickResultsList.ItemsSource = quickResults;
            var smartResults = await smartTask;
            SmartResultsList.ItemsSource = smartResults;

            HeaderStatusText.Text = quickResults.Count == 0 && smartResults.Count == 0
                ? "No results found"
                : quickResults.Count + " quick results, " + smartResults.Count + " smart results";
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            HeaderStatusText.Text = exception.Message;
        }
    }

    private void OnSearchBoxKeyDown(object sender, KeyEventArgs e)
    {
        if (IsSearchBlocked())
        {
            ShowSearchBlockedMessage();
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Enter)
        {
            OpenSelectedResult(QuickResultsList.SelectedItem is not null ? QuickResultsList : SmartResultsList);
            e.Handled = true;
        }
    }

    private void OnResultOpen(object sender, MouseButtonEventArgs e)
    {
        OpenSelectedResult(sender as ListView);
    }

    private void OpenSelectedResult(ListView? listView)
    {
        if (IsSearchBlocked())
        {
            ShowSearchBlockedMessage();
            return;
        }

        var selectedItem = listView?.SelectedItem;
        if (selectedItem is not SearchResultItem result || !File.Exists(result.FullPath))
        {
            return;
        }

        Process.Start(new ProcessStartInfo(result.FullPath)
        {
            UseShellExecute = true
        });
    }

    private void OnBrowseDatabasePath(object sender, RoutedEventArgs e)
    {
        var selectedPath = ChooseFolder(DatabasePathTextBox.Text);
        if (selectedPath is not null)
        {
            DatabasePathTextBox.Text = selectedPath;
        }
    }

    private void OnBrowseSetupDatabasePath(object sender, RoutedEventArgs e)
    {
        var selectedPath = ChooseFolder(SetupDatabasePathTextBox.Text);
        if (selectedPath is not null)
        {
            SetupDatabasePathTextBox.Text = selectedPath;
        }
    }

    private void OnAddQuickDirectory(object sender, RoutedEventArgs e)
    {
        AddDirectoryToList(QuickDirectoriesListBox);
    }

    private void OnRemoveQuickDirectory(object sender, RoutedEventArgs e)
    {
        RemoveSelectedDirectory(QuickDirectoriesListBox);
    }

    private void OnAddSmartDirectory(object sender, RoutedEventArgs e)
    {
        AddDirectoryToList(SmartDirectoriesListBox);
    }

    private void OnRemoveSmartDirectory(object sender, RoutedEventArgs e)
    {
        RemoveSelectedDirectory(SmartDirectoriesListBox);
    }

    private void OnAddCategorizationDirectory(object sender, RoutedEventArgs e)
    {
        AddDirectoryToList(CategorizationDirectoriesListBox);
    }

    private void OnRemoveCategorizationDirectory(object sender, RoutedEventArgs e)
    {
        RemoveSelectedDirectory(CategorizationDirectoriesListBox);
    }

    private void OnAddSetupSmartDirectory(object sender, RoutedEventArgs e)
    {
        AddDirectoryToList(SetupSmartDirectoriesListBox);
    }

    private void OnRemoveSetupSmartDirectory(object sender, RoutedEventArgs e)
    {
        RemoveSelectedDirectory(SetupSmartDirectoriesListBox);
    }

    private void OnAddSetupCategorizationDirectory(object sender, RoutedEventArgs e)
    {
        AddDirectoryToList(SetupCategorizationDirectoriesListBox);
    }

    private void OnRemoveSetupCategorizationDirectory(object sender, RoutedEventArgs e)
    {
        RemoveSelectedDirectory(SetupCategorizationDirectoriesListBox);
    }

    private async void OnSaveSettings(object sender, RoutedEventArgs e)
    {
        try
        {
            var oldConfig = _config;
            var newConfig = ReadConfigFromControls();
            newConfig.Normalize();
            _configService.VerifyWritableDirectory(newConfig.DatabasePath);
            _configService.MoveDatabaseIfNeeded(oldConfig, newConfig);
            _configService.Save(newConfig);
            _config = newConfig;

            PopulateControlsFromConfig(_config);
            ApplyTheme(_config.Theme);
            await _databaseService.InitializeAsync(_config);
            StartRuntimeServices();

            HeaderStatusText.Text = "Settings saved";
            if (RequiresRebuild(oldConfig, _config))
            {
                BeginIndexRebuild();
            }
        }
        catch (Exception exception)
        {
            HeaderStatusText.Text = exception.Message;
        }
    }

    private AppConfig ReadConfigFromControls()
    {
        return new AppConfig
        {
            InstallationPath = InstallationPathTextBox.Text,
            DatabasePath = DatabasePathTextBox.Text,
            QuickSearchDirectories = ReadListBoxItems(QuickDirectoriesListBox),
            SmartSearchDirectories = ReadListBoxItems(SmartDirectoriesListBox),
            SmartSearchExtensions = ParseExtensions(SmartExtensionsTextBox.Text),
            CategorizationDirectories = ReadListBoxItems(CategorizationDirectoriesListBox),
            Theme = "Dark",
            HotkeyGesture = string.IsNullOrWhiteSpace(HotkeyTextBox.Text) ? "Alt+Space" : HotkeyTextBox.Text.Trim(),
            IndexAllFixedDrivesForQuickSearch = IndexAllDrivesCheckBox.IsChecked == true,
            IsConfigured = true
        };
    }

    private void PopulateControlsFromConfig(AppConfig config)
    {
        InstallationPathTextBox.Text = config.InstallationPath;
        DatabasePathTextBox.Text = config.DatabasePath;
        HotkeyTextBox.Text = config.HotkeyGesture;
        SmartExtensionsTextBox.Text = string.Join(", ", config.SmartSearchExtensions);
        IndexAllDrivesCheckBox.IsChecked = config.IndexAllFixedDrivesForQuickSearch;
        ReplaceListBoxItems(QuickDirectoriesListBox, config.QuickSearchDirectories);
        ReplaceListBoxItems(SmartDirectoriesListBox, config.SmartSearchDirectories);
        ReplaceListBoxItems(CategorizationDirectoriesListBox, config.CategorizationDirectories);
        HotkeyDisplayText.Text = config.HotkeyGesture;
    }

    private void BeginIndexRebuild()
    {
        if (!_config.IsConfigured)
        {
            return;
        }

        var runId = Interlocked.Increment(ref _indexingRunId);
        _indexingCancellationTokenSource?.Cancel();
        _indexingCancellationTokenSource = new CancellationTokenSource();
        var cancellationToken = _indexingCancellationTokenSource.Token;
        var progress = new Progress<IndexingProgress>(progressUpdate =>
        {
            if (IsCurrentIndexingRun(runId))
            {
                ApplyIndexProgress(progressUpdate);
            }
        });

        SetFullIndexingActive(true);
        IndexProgressBar.IsIndeterminate = true;
        HeaderStatusText.Text = "Indexing...";

        _ = Task.Run(async () =>
        {
            try
            {
                await _indexingService.BuildIndexAsync(_config, progress, cancellationToken);
                await Dispatcher.InvokeAsync(async () =>
                {
                    if (!IsCurrentIndexingRun(runId))
                    {
                        return;
                    }

                    SetFullIndexingActive(false);
                    await RefreshIndexCountAsync();
                    await RunMainSearchAsync();
                });
            }
            catch (OperationCanceledException)
            {
                await Dispatcher.InvokeAsync(() =>
                {
                    if (!IsCurrentIndexingRun(runId))
                    {
                        return;
                    }

                    SetFullIndexingActive(false);
                    IndexProgressBar.IsIndeterminate = false;
                    IndexStatusText.Text = "Indexing cancelled.";
                    HeaderStatusText.Text = "Indexing cancelled";
                });
            }
            catch (Exception exception)
            {
                await Dispatcher.InvokeAsync(() =>
                {
                    if (!IsCurrentIndexingRun(runId))
                    {
                        return;
                    }

                    SetFullIndexingActive(false);
                    IndexProgressBar.IsIndeterminate = false;
                    IndexStatusText.Text = exception.Message;
                    HeaderStatusText.Text = exception.Message;
                });
            }
        });
    }

    private void OnRebuildIndex(object sender, RoutedEventArgs e)
    {
        BeginIndexRebuild();
    }

    private async void OnRefreshIndexCount(object sender, RoutedEventArgs e)
    {
        await RefreshIndexCountAsync();
    }

    private void ApplyIndexProgress(IndexingProgress progress)
    {
        IndexStatusText.Text = progress.Message;
        HeaderStatusText.Text = progress.Message;

        if (progress.IsComplete)
        {
            IndexProgressBar.IsIndeterminate = false;
            IndexProgressBar.Value = 100;
        }
    }

    private bool IsSearchBlocked()
    {
        return _isFullIndexingActive;
    }

    private bool IsCurrentIndexingRun(int runId)
    {
        return runId == Volatile.Read(ref _indexingRunId);
    }

    private void SetFullIndexingActive(bool isActive)
    {
        _isFullIndexingActive = isActive;
        SearchQueryTextBox.IsReadOnly = isActive;
        SearchQueryTextBox.IsHitTestVisible = !isActive;
        QuickResultsList.IsHitTestVisible = !isActive;
        SmartResultsList.IsHitTestVisible = !isActive;
        QuickResultsList.Opacity = isActive ? 0.72 : 1;
        SmartResultsList.Opacity = isActive ? 0.72 : 1;

        if (isActive)
        {
            _searchDebounceTimer.Stop();
            _searchCancellationTokenSource?.Cancel();
            ShowSearchBlockedMessage();
        }

        _overlayWindow?.UpdateSearchAvailability();
    }

    private void ShowSearchBlockedMessage()
    {
        HeaderStatusText.Text = SearchBlockedDuringIndexMessage;
        IndexStatusText.Text = SearchBlockedDuringIndexMessage;
    }

    private async Task RefreshIndexCountAsync()
    {
        var count = await _databaseService.CountFilesAsync();
        IndexStatusText.Text = "Indexed files: " + count;
    }

    private async void OnBrowseKeywordFile(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true
        };

        if (dialog.ShowDialog(this) == true)
        {
            KeywordFilePathTextBox.Text = dialog.FileName;
            await _indexingService.IndexFileAsync(dialog.FileName, _config);
        }
    }

    private async void OnSaveKeywords(object sender, RoutedEventArgs e)
    {
        var filePath = KeywordFilePathTextBox.Text.Trim();
        if (!File.Exists(filePath))
        {
            HeaderStatusText.Text = "Select an indexed file first.";
            return;
        }

        try
        {
            await _indexingService.IndexFileAsync(filePath, _config);
            await _databaseService.SetCustomKeywordsAsync(filePath, KeywordsTextBox.Text);
            HeaderStatusText.Text = "Keywords saved";
            await ReloadKeywordsAsync();
            await RunMainSearchAsync();
        }
        catch (Exception exception)
        {
            HeaderStatusText.Text = exception.Message;
        }
    }

    private async void OnDeleteKeywords(object sender, RoutedEventArgs e)
    {
        KeywordsTextBox.Clear();
        await _databaseService.SetCustomKeywordsAsync(KeywordFilePathTextBox.Text.Trim(), string.Empty);
        HeaderStatusText.Text = "Keywords deleted";
        await ReloadKeywordsAsync();
    }

    private async void OnReloadKeywords(object sender, RoutedEventArgs e)
    {
        await ReloadKeywordsAsync();
    }

    private async Task ReloadKeywordsAsync()
    {
        var records = await _databaseService.GetFilesWithCustomKeywordsAsync();
        KeywordFilesList.ItemsSource = records.Select(record => new SearchResultItem
        {
            FileName = record.FileName,
            FullPath = record.FullPath,
            DirectoryPath = record.DirectoryPath,
            Extension = record.Extension,
            ModifiedUtc = record.ModifiedUtc,
            Score = 1,
            Tags = record.CustomKeywords
        }).ToArray();
    }

    private void OnKeywordSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (KeywordFilesList.SelectedItem is not SearchResultItem result)
        {
            return;
        }

        KeywordFilePathTextBox.Text = result.FullPath;
        KeywordsTextBox.Text = result.Tags;
    }

    private static string? ChooseFolder(string selectedPath)
    {
        using var dialog = new Forms.FolderBrowserDialog
        {
            SelectedPath = Directory.Exists(selectedPath) ? selectedPath : Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
            UseDescriptionForTitle = true,
            Description = "Select folder"
        };

        return dialog.ShowDialog() == Forms.DialogResult.OK ? dialog.SelectedPath : null;
    }

    private static void AddDirectoryToList(ListBox listBox)
    {
        var selectedPath = ChooseFolder(string.Empty);
        if (selectedPath is null)
        {
            return;
        }

        var existing = listBox.Items.Cast<string>().Any(item => string.Equals(item, selectedPath, StringComparison.OrdinalIgnoreCase));
        if (!existing)
        {
            listBox.Items.Add(selectedPath);
        }
    }

    private static void RemoveSelectedDirectory(ListBox listBox)
    {
        if (listBox.SelectedItem is string selectedItem)
        {
            listBox.Items.Remove(selectedItem);
        }
    }

    private static List<string> ReadListBoxItems(ListBox listBox)
    {
        return listBox.Items.Cast<string>().ToList();
    }

    private static void ReplaceListBoxItems(ListBox listBox, IEnumerable<string> items)
    {
        listBox.Items.Clear();
        foreach (var item in items.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            listBox.Items.Add(item);
        }
    }

    private static List<string> ParseExtensions(string value)
    {
        return value
            .Split(new[] { ',', ';', ' ', '\r', '\n', '\t' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(extension => extension.StartsWith('.') ? extension.ToLowerInvariant() : "." + extension.ToLowerInvariant())
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static bool RequiresRebuild(AppConfig oldConfig, AppConfig newConfig)
    {
        return oldConfig.IndexAllFixedDrivesForQuickSearch != newConfig.IndexAllFixedDrivesForQuickSearch ||
               !oldConfig.DatabasePath.Equals(newConfig.DatabasePath, StringComparison.OrdinalIgnoreCase) ||
               !oldConfig.QuickSearchDirectories.SequenceEqual(newConfig.QuickSearchDirectories, StringComparer.OrdinalIgnoreCase) ||
               !oldConfig.SmartSearchDirectories.SequenceEqual(newConfig.SmartSearchDirectories, StringComparer.OrdinalIgnoreCase) ||
               !oldConfig.SmartSearchExtensions.SequenceEqual(newConfig.SmartSearchExtensions, StringComparer.OrdinalIgnoreCase) ||
               !oldConfig.CategorizationDirectories.SequenceEqual(newConfig.CategorizationDirectories, StringComparer.OrdinalIgnoreCase);
    }

    private static void ApplyTheme(string theme)
    {
        SetBrush("AppBackgroundBrush", Color.FromRgb(16, 19, 24));
        SetBrush("PanelBackgroundBrush", Color.FromRgb(23, 27, 34));
        SetBrush("InsetBackgroundBrush", Color.FromRgb(13, 16, 21));
        SetBrush("PrimaryTextBrush", Color.FromRgb(243, 246, 250));
        SetBrush("SecondaryTextBrush", Color.FromRgb(167, 176, 190));
        SetBrush("AccentBrush", Color.FromRgb(55, 183, 165));
        SetBrush("BorderBrush", Color.FromRgb(45, 52, 64));
        SetBrush("InputBackgroundBrush", Color.FromRgb(15, 19, 25));
        SetBrush("ButtonBackgroundBrush", Color.FromRgb(38, 49, 59));
        SetBrush("ButtonHoverBrush", Color.FromRgb(51, 66, 82));
        SetBrush("ButtonPressedBrush", Color.FromRgb(30, 126, 114));
        SetBrush("ButtonTextBrush", Color.FromRgb(249, 251, 252));
        SetBrush("DisabledTextBrush", Color.FromRgb(112, 123, 138));
        SetBrush("HeaderBackgroundBrush", Color.FromRgb(32, 38, 48));
        SetBrush("RowHoverBrush", Color.FromRgb(38, 49, 59));
        SetBrush("SelectionBrush", Color.FromRgb(30, 126, 114));
        SetBrush("SelectionTextBrush", Color.FromRgb(255, 255, 255));
    }

    private static void SetBrush(string resourceKey, Color color)
    {
        Application.Current.Resources[resourceKey] = new SolidColorBrush(color);
    }
}
