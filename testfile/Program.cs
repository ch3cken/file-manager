using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using File2Manager.Core.Models;
using File2Manager.Core.Search;
using File2Manager.Core.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Services.ConfigureHttpJsonOptions(options =>
{
    options.SerializerOptions.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
    options.SerializerOptions.DictionaryKeyPolicy = JsonNamingPolicy.CamelCase;
});
builder.Services.AddSingleton<FileManagerRuntime>();

var app = builder.Build();
if (!HasExplicitUrl(args))
{
    app.Urls.Add("http://127.0.0.1:" + FindAvailablePort(5077, 40));
}

var runtime = app.Services.GetRequiredService<FileManagerRuntime>();
await runtime.InitializeAsync(app.Lifetime.ApplicationStopping);

app.MapGet("/", (IWebHostEnvironment environment) =>
    Results.File(Path.Combine(environment.ContentRootPath, "index.html"), "text/html; charset=utf-8"));
app.MapGet("/index.html", (IWebHostEnvironment environment) =>
    Results.File(Path.Combine(environment.ContentRootPath, "index.html"), "text/html; charset=utf-8"));
app.MapGet("/styles.css", (IWebHostEnvironment environment) =>
    Results.File(Path.Combine(environment.ContentRootPath, "styles.css"), "text/css; charset=utf-8"));
app.MapGet("/app.js", (IWebHostEnvironment environment) =>
    Results.File(Path.Combine(environment.ContentRootPath, "app.js"), "application/javascript; charset=utf-8"));

app.MapGet("/api/health", () => Results.Ok(new { ok = true }));
app.MapGet("/api/config", (FileManagerRuntime manager) => Results.Ok(manager.GetConfig()));
app.MapGet("/api/status", async (FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.GetStatusAsync(cancellationToken)));
app.MapGet("/api/files", async (string? query, int? limit, FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.GetFilesAsync(query, limit ?? 200, cancellationToken)));
app.MapPost("/api/search", async (SearchRequest request, FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.SearchAsync(request.Query ?? string.Empty, request.Mode ?? "hybrid", request.ModifiedDate ?? "any", cancellationToken)));
app.MapPost("/api/setup", async (SetupRequest request, FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.SaveSetupAsync(request, cancellationToken)));
app.MapPost("/api/settings", async (SettingsRequest request, FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.SaveSettingsAsync(request, cancellationToken)));
app.MapPost("/api/keywords", async (KeywordsRequest request, FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.SaveKeywordsAsync(request, cancellationToken)));
app.MapPost("/api/files/open", async (OpenFileRequest request, FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.OpenFileAsync(request, cancellationToken)));
app.MapPost("/api/index/rebuild", async (FileManagerRuntime manager, CancellationToken cancellationToken) =>
    Results.Ok(await manager.RebuildIndexAsync(cancellationToken)));

if (args.Any(arg => string.Equals(arg, "--open", StringComparison.OrdinalIgnoreCase)))
{
    app.Lifetime.ApplicationStarted.Register(() =>
    {
        var url = app.Urls.FirstOrDefault() ?? "http://127.0.0.1:5077";
        _ = Task.Run(() => OpenBrowser(url));
    });
}

app.Run();

static bool HasExplicitUrl(string[] args)
{
    if (!string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("ASPNETCORE_URLS")))
    {
        return true;
    }

    for (var index = 0; index < args.Length; index++)
    {
        var arg = args[index];
        if (arg.Equals("--urls", StringComparison.OrdinalIgnoreCase) ||
            arg.Equals("-urls", StringComparison.OrdinalIgnoreCase) ||
            arg.StartsWith("--urls=", StringComparison.OrdinalIgnoreCase) ||
            arg.StartsWith("urls=", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
    }

    return false;
}

static int FindAvailablePort(int preferredPort, int range)
{
    for (var port = preferredPort; port < preferredPort + range; port++)
    {
        if (CanBind(port))
        {
            return port;
        }
    }

    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var selectedPort = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return selectedPort;
}

static bool CanBind(int port)
{
    try
    {
        var listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
        listener.Stop();
        return true;
    }
    catch (SocketException)
    {
        return false;
    }
}

static void OpenBrowser(string url)
{
    try
    {
        Process.Start(new ProcessStartInfo(url)
        {
            UseShellExecute = true
        });
    }
    catch
    {
        // Opening the browser is a convenience only; the server remains usable.
    }
}

public sealed class FileManagerRuntime : IDisposable
{
    private const int QuickSearchResultLimit = 20;
    private const int SmartSearchResultLimit = 75;

    private readonly object _stateLock = new();
    private readonly ConfigService _configService;
    private readonly DatabaseService _databaseService = new();
    private readonly DocumentTextExtractor _textExtractor = new();
    private readonly CategorizationService _categorizationService = new();
    private readonly SearchQueryParser _queryParser = new();
    private readonly SemanticEmbeddingService _embeddingService = new();
    private readonly SearchService _searchService;
    private FileIndexingService _indexingService;
    private AppConfig _config;
    private CancellationTokenSource? _indexingCancellationTokenSource;
    private IndexingProgress _lastProgress = new() { Message = "Idle" };
    private bool _isIndexing;
    private bool _disposed;

    public FileManagerRuntime(IWebHostEnvironment environment)
    {
        _configService = new ConfigService(environment.ContentRootPath);
        _config = _configService.Load();
        if (!_config.IsConfigured)
        {
            _config.QuickSearchDirectories = new List<string> { environment.ContentRootPath };
            _config.SmartSearchDirectories = new List<string> { environment.ContentRootPath };
            _config.CategorizationDirectories = new List<string> { environment.ContentRootPath };
            _config.DatabasePath = Path.Combine(environment.ContentRootPath, "data");
            _config.Normalize();
        }
        _searchService = new SearchService(_databaseService, _queryParser, _embeddingService);
        _indexingService = new FileIndexingService(_databaseService, _textExtractor, _categorizationService, _embeddingService);
    }

    public async Task InitializeAsync(CancellationToken cancellationToken)
    {
        await _databaseService.InitializeAsync(_config, cancellationToken);

        if (_config.IsConfigured)
        {
            _indexingService.StartWatchers(_config, UpdateProgress);
            if (await _databaseService.CountFilesAsync(cancellationToken) == 0)
            {
                _ = BeginIndexRebuildAsync(cancellationToken);
            }
        }
    }

    public ConfigResponse GetConfig()
    {
        lock (_stateLock)
        {
            return ConfigResponse.FromConfig(_config);
        }
    }

    public async Task<StatusResponse> GetStatusAsync(CancellationToken cancellationToken)
    {
        var count = 0;
        try
        {
            count = await _databaseService.CountFilesAsync(cancellationToken);
        }
        catch
        {
            count = 0;
        }

        lock (_stateLock)
        {
            return new StatusResponse(
                _config.IsConfigured,
                _isIndexing,
                count,
                _lastProgress.FilesSeen,
                _lastProgress.FilesIndexed,
                _lastProgress.Exceptions,
                _lastProgress.Message);
        }
    }

    public async Task<SearchResponse> SearchAsync(string query, string mode, string modifiedDate, CancellationToken cancellationToken)
    {
        var normalizedMode = NormalizeSearchMode(mode);
        var filters = ApplyModifiedDateFilter(_searchService.Parse(query), modifiedDate);
        lock (_stateLock)
        {
            if (_isIndexing)
            {
                return new SearchResponse(
                    Array.Empty<ResultDto>(),
                    Array.Empty<ResultDto>(),
                    BuildParsedQueryDetails(filters, normalizedMode),
                    new TimingDto("blocked", "blocked"),
                    "Search is disabled while indexing.");
            }
        }

        var runQuick = normalizedMode == "hybrid" || normalizedMode == "quick";
        var runSmart = normalizedMode == "hybrid" || normalizedMode == "smart";
        IReadOnlyList<SearchResultItem> quickResults = Array.Empty<SearchResultItem>();
        IReadOnlyList<SearchResultItem> smartResults = Array.Empty<SearchResultItem>();
        var quickTiming = "off";
        var smartTiming = "off";

        if (runQuick)
        {
            var quickWatch = Stopwatch.StartNew();
            quickResults = await _searchService.QuickSearchAsync(filters, QuickSearchResultLimit, cancellationToken);
            quickWatch.Stop();
            quickTiming = FormatElapsed(quickWatch.Elapsed);
        }

        if (runSmart)
        {
            var smartWatch = Stopwatch.StartNew();
            smartResults = await _searchService.SmartSearchAsync(filters, SmartSearchResultLimit, cancellationToken);
            smartWatch.Stop();
            smartTiming = FormatElapsed(smartWatch.Elapsed);
        }

        return new SearchResponse(
            quickResults.Select(result => ResultDto.FromSearchResult(result, includeConfidence: false)).ToArray(),
            smartResults.Select(result => ResultDto.FromSearchResult(result, includeConfidence: true)).ToArray(),
            BuildParsedQueryDetails(filters, normalizedMode),
            new TimingDto(quickTiming, smartTiming),
            !GetConfig().IsConfigured ? "Setup is required before indexing can start." : string.Empty);
    }

    public async Task<IReadOnlyList<KeywordFileDto>> GetFilesAsync(string? query, int limit, CancellationToken cancellationToken)
    {
        var boundedLimit = Math.Clamp(limit, 1, 500);
        var records = await _databaseService.GetAllFilesAsync(cancellationToken);
        var normalizedQuery = (query ?? string.Empty).Trim();

        if (!string.IsNullOrWhiteSpace(normalizedQuery))
        {
            records = records
                .Where(record =>
                    record.FileName.Contains(normalizedQuery, StringComparison.OrdinalIgnoreCase) ||
                    record.FullPath.Contains(normalizedQuery, StringComparison.OrdinalIgnoreCase) ||
                    record.CustomKeywords.Contains(normalizedQuery, StringComparison.OrdinalIgnoreCase) ||
                    record.Categories.Contains(normalizedQuery, StringComparison.OrdinalIgnoreCase))
                .ToList();
        }

        return records
            .Take(boundedLimit)
            .Select(KeywordFileDto.FromRecord)
            .ToArray();
    }

    public async Task<SaveResponse> SaveSetupAsync(SetupRequest request, CancellationToken cancellationToken)
    {
        var oldConfig = GetConfigSnapshot();
        var newConfig = BuildConfigFromSetupRequest(request, oldConfig);
        newConfig.IsConfigured = true;

        await ApplyConfigAsync(oldConfig, newConfig, rebuildIndex: true, cancellationToken);
        return new SaveResponse(true, "Setup saved. Index rebuild started.");
    }

    public async Task<SaveResponse> SaveSettingsAsync(SettingsRequest request, CancellationToken cancellationToken)
    {
        var oldConfig = GetConfigSnapshot();
        var newConfig = BuildConfigFromSettingsRequest(request, oldConfig);
        var rebuildIndex = RequiresRebuild(oldConfig, newConfig);

        await ApplyConfigAsync(oldConfig, newConfig, rebuildIndex, cancellationToken);
        return new SaveResponse(true, rebuildIndex ? "Settings saved. Index rebuild started." : "Settings saved.");
    }

    public async Task<SaveResponse> SaveKeywordsAsync(KeywordsRequest request, CancellationToken cancellationToken)
    {
        var fullPath = string.IsNullOrWhiteSpace(request.FullPath) ? request.FileId : request.FullPath;
        if (string.IsNullOrWhiteSpace(fullPath))
        {
            throw new InvalidOperationException("A file path is required.");
        }

        var normalizedPath = Path.GetFullPath(Environment.ExpandEnvironmentVariables(fullPath));
        if (File.Exists(normalizedPath))
        {
            await _indexingService.IndexFileAsync(normalizedPath, _config, cancellationToken);
        }

        var keywords = string.Join(", ", request.Keywords
            .Where(keyword => !string.IsNullOrWhiteSpace(keyword))
            .Select(keyword => keyword.Trim())
            .Distinct(StringComparer.OrdinalIgnoreCase));

        await _databaseService.SetCustomKeywordsAsync(normalizedPath, keywords, cancellationToken);
        return new SaveResponse(true, "Keywords saved.");
    }

    public Task<SaveResponse> OpenFileAsync(OpenFileRequest request, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(request.Path))
        {
            throw new InvalidOperationException("A file path is required.");
        }

        var path = Path.GetFullPath(Environment.ExpandEnvironmentVariables(request.Path));
        if (!File.Exists(path) && !Directory.Exists(path))
        {
            throw new FileNotFoundException("The requested file no longer exists.", path);
        }

        Process.Start(new ProcessStartInfo(path)
        {
            UseShellExecute = true
        });
        return Task.FromResult(new SaveResponse(true, "Open request sent."));
    }

    public Task<SaveResponse> RebuildIndexAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!_config.IsConfigured)
        {
            return Task.FromResult(new SaveResponse(false, "Complete setup before rebuilding the index."));
        }

        _ = BeginIndexRebuildAsync(CancellationToken.None);
        return Task.FromResult(new SaveResponse(true, "Index rebuild started."));
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _indexingCancellationTokenSource?.Cancel();
        _indexingCancellationTokenSource?.Dispose();
        _indexingService.Dispose();
    }

    private async Task ApplyConfigAsync(AppConfig oldConfig, AppConfig newConfig, bool rebuildIndex, CancellationToken cancellationToken)
    {
        newConfig.Normalize();
        _configService.VerifyWritableDirectory(newConfig.DatabasePath);
        _configService.MoveDatabaseIfNeeded(oldConfig, newConfig);
        _configService.Save(newConfig);

        lock (_stateLock)
        {
            _config = newConfig;
        }

        await _databaseService.InitializeAsync(newConfig, cancellationToken);
        _indexingService.StartWatchers(newConfig, UpdateProgress);

        if (rebuildIndex)
        {
            _ = BeginIndexRebuildAsync(CancellationToken.None);
        }
    }

    private AppConfig GetConfigSnapshot()
    {
        lock (_stateLock)
        {
            return new AppConfig
            {
                InstallationPath = _config.InstallationPath,
                DatabasePath = _config.DatabasePath,
                QuickSearchDirectories = _config.QuickSearchDirectories.ToList(),
                SmartSearchDirectories = _config.SmartSearchDirectories.ToList(),
                SmartSearchExtensions = _config.SmartSearchExtensions.ToList(),
                CategorizationDirectories = _config.CategorizationDirectories.ToList(),
                Theme = _config.Theme,
                HotkeyGesture = _config.HotkeyGesture,
                IndexAllFixedDrivesForQuickSearch = _config.IndexAllFixedDrivesForQuickSearch,
                IsConfigured = _config.IsConfigured
            };
        }
    }

    private AppConfig BuildConfigFromSetupRequest(SetupRequest request, AppConfig fallback)
    {
        var smartFolders = CoalesceList(request.SmartFolders, fallback.SmartSearchDirectories);
        var categorizationFolders = CoalesceList(request.CategoryFolders, fallback.CategorizationDirectories);
        var extensions = CoalesceList(request.EnabledExtensions, CoalesceList(request.Extensions, fallback.SmartSearchExtensions));

        return new AppConfig
        {
            InstallationPath = NonEmpty(request.AppPath, fallback.InstallationPath),
            DatabasePath = NonEmpty(request.DbPath, fallback.DatabasePath),
            QuickSearchDirectories = smartFolders.Concat(categorizationFolders).Distinct(StringComparer.OrdinalIgnoreCase).ToList(),
            SmartSearchDirectories = smartFolders,
            SmartSearchExtensions = extensions,
            CategorizationDirectories = categorizationFolders,
            Theme = NonEmpty(request.Theme, fallback.Theme),
            HotkeyGesture = NonEmpty(request.Hotkey, fallback.HotkeyGesture),
            IndexAllFixedDrivesForQuickSearch = request.IndexAllFixedDrivesForQuickSearch ?? fallback.IndexAllFixedDrivesForQuickSearch,
            IsConfigured = true
        };
    }

    private AppConfig BuildConfigFromSettingsRequest(SettingsRequest request, AppConfig fallback)
    {
        var smartFolders = CoalesceList(request.SmartFolders, fallback.SmartSearchDirectories);
        var categorizationFolders = CoalesceList(request.CategoryFolders, fallback.CategorizationDirectories);
        var quickFolders = CoalesceList(request.QuickFolders, fallback.QuickSearchDirectories);
        var extensions = CoalesceList(request.EnabledExtensions, CoalesceList(request.Extensions, fallback.SmartSearchExtensions));

        return new AppConfig
        {
            InstallationPath = NonEmpty(request.AppPath, fallback.InstallationPath),
            DatabasePath = NonEmpty(request.DbPath, fallback.DatabasePath),
            QuickSearchDirectories = quickFolders.Count > 0
                ? quickFolders
                : smartFolders.Concat(categorizationFolders).Distinct(StringComparer.OrdinalIgnoreCase).ToList(),
            SmartSearchDirectories = smartFolders,
            SmartSearchExtensions = extensions,
            CategorizationDirectories = categorizationFolders,
            Theme = NonEmpty(request.Theme, fallback.Theme),
            HotkeyGesture = NonEmpty(request.Hotkey, fallback.HotkeyGesture),
            IndexAllFixedDrivesForQuickSearch = request.IndexAllFixedDrivesForQuickSearch ?? fallback.IndexAllFixedDrivesForQuickSearch,
            IsConfigured = request.IsConfigured ?? fallback.IsConfigured
        };
    }

    private async Task BeginIndexRebuildAsync(CancellationToken applicationStopping)
    {
        _indexingCancellationTokenSource?.Cancel();
        _indexingCancellationTokenSource?.Dispose();
        _indexingCancellationTokenSource = CancellationTokenSource.CreateLinkedTokenSource(applicationStopping);
        var cancellationToken = _indexingCancellationTokenSource.Token;
        var config = GetConfigSnapshot();

        lock (_stateLock)
        {
            _isIndexing = true;
            _lastProgress = new IndexingProgress { Message = "Indexing started." };
        }

        var progress = new Progress<IndexingProgress>(UpdateProgress);

        try
        {
            await _indexingService.BuildIndexAsync(config, progress, cancellationToken);
            _indexingService.StartWatchers(config, UpdateProgress);
        }
        catch (OperationCanceledException)
        {
            UpdateProgress(new IndexingProgress { Message = "Indexing cancelled." });
        }
        catch (Exception exception)
        {
            UpdateProgress(new IndexingProgress { Message = exception.Message });
        }
        finally
        {
            lock (_stateLock)
            {
                _isIndexing = false;
            }
        }
    }

    private void UpdateProgress(IndexingProgress progress)
    {
        lock (_stateLock)
        {
            _lastProgress = progress;
            if (progress.IsComplete)
            {
                _isIndexing = false;
            }
        }
    }

    private static IReadOnlyList<string> BuildParsedQueryDetails(SearchFilters filters, string mode)
    {
        var type = filters.HasExtensionFilter
            ? "Type: " + string.Join(", ", filters.Extensions)
            : "Type: any";
        var time = filters.HasDateFilter
            ? "Modified: " + FormatDateRange(filters.ModifiedFromUtc, filters.ModifiedToUtc)
            : "Modified: any";

        return new[] { type, time, "Mode: " + SearchModeLabel(mode) };
    }

    private static SearchFilters ApplyModifiedDateFilter(SearchFilters filters, string? modifiedDate)
    {
        var (from, to) = ResolveModifiedDateRange(modifiedDate);
        return new SearchFilters
        {
            CleanQuery = filters.CleanQuery,
            Extensions = filters.Extensions,
            SemanticTerms = filters.SemanticTerms,
            ModifiedFromUtc = from,
            ModifiedToUtc = to
        };
    }

    private static (DateTimeOffset? FromUtc, DateTimeOffset? ToUtc) ResolveModifiedDateRange(string? modifiedDate)
    {
        var normalized = (modifiedDate ?? "any").Trim().ToLowerInvariant();
        var now = DateTimeOffset.Now;
        var today = new DateTimeOffset(now.Year, now.Month, now.Day, 0, 0, 0, now.Offset);

        return normalized switch
        {
            "today" => (today.ToUniversalTime(), today.AddDays(1).ToUniversalTime()),
            "yesterday" => (today.AddDays(-1).ToUniversalTime(), today.ToUniversalTime()),
            "this-week" => (today.AddDays(-(((int)today.DayOfWeek + 6) % 7)).ToUniversalTime(),
                today.AddDays(-(((int)today.DayOfWeek + 6) % 7)).AddDays(7).ToUniversalTime()),
            "last-7-days" => (now.AddDays(-7).ToUniversalTime(), now.ToUniversalTime()),
            "this-month" => (new DateTimeOffset(now.Year, now.Month, 1, 0, 0, 0, now.Offset).ToUniversalTime(),
                new DateTimeOffset(now.Year, now.Month, 1, 0, 0, 0, now.Offset).AddMonths(1).ToUniversalTime()),
            "last-30-days" => (now.AddDays(-30).ToUniversalTime(), now.ToUniversalTime()),
            _ => (null, null)
        };
    }

    private static string NormalizeSearchMode(string? mode)
    {
        var normalized = (mode ?? "hybrid").Trim().ToLowerInvariant();
        return normalized == "quick" || normalized == "smart" ? normalized : "hybrid";
    }

    private static string SearchModeLabel(string mode)
    {
        return mode switch
        {
            "quick" => "quick only",
            "smart" => "smart only",
            _ => "hybrid"
        };
    }

    private static string FormatDateRange(DateTimeOffset? from, DateTimeOffset? to)
    {
        if (from.HasValue && to.HasValue)
        {
            return from.Value.ToLocalTime().ToString("yyyy-MM-dd") + " to " + to.Value.ToLocalTime().ToString("yyyy-MM-dd");
        }

        if (from.HasValue)
        {
            return "after " + from.Value.ToLocalTime().ToString("yyyy-MM-dd");
        }

        if (to.HasValue)
        {
            return "before " + to.Value.ToLocalTime().ToString("yyyy-MM-dd");
        }

        return "any";
    }

    private static string FormatElapsed(TimeSpan elapsed)
    {
        return elapsed.TotalSeconds < 1
            ? Math.Max(1, (int)Math.Round(elapsed.TotalMilliseconds)) + "ms"
            : elapsed.TotalSeconds.ToString("0.00") + "s";
    }

    private static List<string> CoalesceList(IEnumerable<string>? primary, IEnumerable<string> fallback)
    {
        var list = primary?
            .Where(item => !string.IsNullOrWhiteSpace(item))
            .Select(item => item.Trim())
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        return list is { Count: > 0 } ? list : fallback.ToList();
    }

    private static string NonEmpty(string? value, string fallback)
    {
        return string.IsNullOrWhiteSpace(value) ? fallback : value.Trim();
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
}

public sealed record SearchRequest(string? Query, string? Mode, string? ModifiedDate);

public sealed record SetupRequest(
    string? AppPath,
    string? DbPath,
    List<string>? SmartFolders,
    List<string>? CategoryFolders,
    List<string>? Extensions,
    List<string>? EnabledExtensions,
    string? Theme,
    string? Hotkey,
    bool? IndexAllFixedDrivesForQuickSearch);

public sealed record SettingsRequest(
    string? AppPath,
    string? DbPath,
    List<string>? QuickFolders,
    List<string>? SmartFolders,
    List<string>? CategoryFolders,
    List<string>? Extensions,
    List<string>? EnabledExtensions,
    string? Theme,
    string? Hotkey,
    bool? IndexAllFixedDrivesForQuickSearch,
    bool? IsConfigured);

public sealed record KeywordsRequest(string? FileId, string? FullPath, List<string> Keywords);

public sealed record OpenFileRequest(string? Path);

public sealed record FolderDialogRequest(string? InitialPath, string? Title);

public sealed record FolderDialogResponse(bool Ok, string? Path);

public sealed record SaveResponse(bool Ok, string Message);

public sealed record TimingDto(string Quick, string Smart);

public sealed record SearchResponse(
    IReadOnlyList<ResultDto> QuickResults,
    IReadOnlyList<ResultDto> SmartResults,
    IReadOnlyList<string> ParsedQuery,
    TimingDto Timings,
    string Message);

public sealed record StatusResponse(
    bool IsConfigured,
    bool IsIndexing,
    int IndexedCount,
    int FilesSeen,
    int FilesIndexed,
    int Exceptions,
    string Message);

public sealed record ConfigResponse(
    bool IsConfigured,
    string AppPath,
    string DbPath,
    IReadOnlyList<string> QuickFolders,
    IReadOnlyList<string> SmartFolders,
    IReadOnlyList<string> CategoryFolders,
    IReadOnlyList<string> Extensions,
    string Theme,
    string Hotkey,
    bool IndexAllFixedDrivesForQuickSearch)
{
    public static ConfigResponse FromConfig(AppConfig config)
    {
        return new ConfigResponse(
            config.IsConfigured,
            config.InstallationPath,
            config.DatabasePath,
            config.QuickSearchDirectories,
            config.SmartSearchDirectories,
            config.CategorizationDirectories,
            config.SmartSearchExtensions,
            config.Theme,
            config.HotkeyGesture,
            config.IndexAllFixedDrivesForQuickSearch);
    }
}

public sealed record ResultDto(
    string Id,
    string Name,
    string Path,
    string Modified,
    string Extension,
    string Confidence,
    IReadOnlyList<string> Tags)
{
    public static ResultDto FromSearchResult(SearchResultItem result, bool includeConfidence)
    {
        return new ResultDto(
            result.FullPath,
            result.FileName,
            result.FullPath,
            result.ModifiedLocalText,
            result.Extension,
            includeConfidence ? "score " + result.Score.ToString("0.###") : string.Empty,
            SplitTags(result.Tags));
    }

    private static IReadOnlyList<string> SplitTags(string tags)
    {
        return tags
            .Split(new[] { ',', ';' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(6)
            .ToArray();
    }
}

public sealed record KeywordFileDto(
    string Id,
    string Name,
    string Path,
    string Modified,
    IReadOnlyList<string> Keywords,
    IReadOnlyList<string> GeneratedTags)
{
    public static KeywordFileDto FromRecord(FileIndexRecord record)
    {
        var customKeywords = SplitTags(record.CustomKeywords, 24);
        var customLookup = new HashSet<string>(customKeywords, StringComparer.OrdinalIgnoreCase);
        var generatedSource = string.Join("; ", new[]
        {
            record.Subject,
            record.DocumentType,
            record.MediaType,
            record.Categories
        }.Where(value => !string.IsNullOrWhiteSpace(value)));

        return new KeywordFileDto(
            record.FullPath,
            record.FileName,
            record.FullPath,
            record.ModifiedUtc.ToLocalTime().ToString("yyyy-MM-dd HH:mm"),
            customKeywords,
            SplitTags(generatedSource, 12)
                .Where(tag => !customLookup.Contains(tag))
                .ToArray());
    }

    private static IReadOnlyList<string> SplitTags(string tags, int maxTags)
    {
        if (string.IsNullOrWhiteSpace(tags))
        {
            return Array.Empty<string>();
        }

        return tags
            .Split(new[] { ',', ';' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(maxTags)
            .ToArray();
    }
}
