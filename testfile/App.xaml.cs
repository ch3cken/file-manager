using System.Net;
using System.Net.Sockets;
using System.IO;
using System.Windows;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Forms = System.Windows.Forms;

namespace FileManager.Desktop;

public partial class App : System.Windows.Application
{
    private WebApplication? _backend;

    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        try
        {
            var url = await StartBackendAsync(e.Args);
            var mainWindow = new MainWindow(url);
            MainWindow = mainWindow;
            mainWindow.Show();
        }
        catch (Exception exception)
        {
            System.Windows.MessageBox.Show(
                exception.Message,
                "FileManager startup failed",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    protected override async void OnExit(ExitEventArgs e)
    {
        if (_backend is not null)
        {
            await _backend.StopAsync(TimeSpan.FromSeconds(3));
            await _backend.DisposeAsync();
        }

        base.OnExit(e);
    }

    private async Task<string> StartBackendAsync(string[] args)
    {
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions
        {
            Args = args,
            ContentRootPath = AppContext.BaseDirectory
        });
        builder.Logging.ClearProviders();
        builder.Services.ConfigureHttpJsonOptions(options =>
        {
            options.SerializerOptions.PropertyNamingPolicy = System.Text.Json.JsonNamingPolicy.CamelCase;
            options.SerializerOptions.DictionaryKeyPolicy = System.Text.Json.JsonNamingPolicy.CamelCase;
        });
        builder.Services.AddSingleton<FileManagerRuntime>();

        var app = builder.Build();
        var port = FindAvailablePort(5077, 80);
        var url = "http://127.0.0.1:" + port;
        app.Urls.Add(url);

        MapEndpoints(app);

        var runtime = app.Services.GetRequiredService<FileManagerRuntime>();
        await runtime.InitializeAsync(app.Lifetime.ApplicationStopping);
        await app.StartAsync();

        _backend = app;
        return url;
    }

    private static void MapEndpoints(WebApplication app)
    {
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
        app.MapPost("/api/dialog/folder", (FolderDialogRequest request) =>
            Results.Ok(ShowFolderDialog(request)));
        app.MapPost("/api/index/rebuild", async (FileManagerRuntime manager, CancellationToken cancellationToken) =>
            Results.Ok(await manager.RebuildIndexAsync(cancellationToken)));
    }

    private static FolderDialogResponse ShowFolderDialog(FolderDialogRequest request)
    {
        string? selectedPath = null;

        System.Windows.Application.Current.Dispatcher.Invoke(() =>
        {
            using var dialog = new Forms.FolderBrowserDialog
            {
                Description = string.IsNullOrWhiteSpace(request.Title) ? "Select folder" : request.Title,
                UseDescriptionForTitle = true,
                ShowNewFolderButton = true,
                SelectedPath = Directory.Exists(request.InitialPath)
                    ? request.InitialPath
                    : Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
            };

            if (dialog.ShowDialog() == Forms.DialogResult.OK)
            {
                selectedPath = dialog.SelectedPath;
            }
        });

        return new FolderDialogResponse(!string.IsNullOrWhiteSpace(selectedPath), selectedPath);
    }

    private static int FindAvailablePort(int preferredPort, int range)
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

    private static bool CanBind(int port)
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
}
