using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using Microsoft.Web.WebView2.Core;
using Drawing = System.Drawing;
using Forms = System.Windows.Forms;

namespace FileManager.Desktop;

public partial class MainWindow : Window
{
    private const int OverlayHotkeyId = 0x350;
    private const int WmHotkey = 0x0312;
    private const uint ModAlt = 0x0001;
    private const uint ModNoRepeat = 0x4000;
    private const uint VkSpace = 0x20;

    private readonly string _url;
    private HwndSource? _windowSource;
    private Forms.NotifyIcon? _trayIcon;
    private bool _hotkeyRegistered;
    private bool _isPageReady;
    private bool _openOverlayWhenReady;
    private bool _isNativeOverlayMode;
    private bool _allowClose;
    private WindowPlacementSnapshot? _normalPlacement;

    public MainWindow(string url)
    {
        _url = url;
        InitializeComponent();
        InitializeTrayIcon();
        Loaded += OnLoaded;
        SourceInitialized += OnSourceInitialized;
        Closing += OnClosing;
        Closed += OnClosed;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            var userDataFolder = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "FileManager",
                "WebView2");
            Directory.CreateDirectory(userDataFolder);

            var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: userDataFolder);
            await Browser.EnsureCoreWebView2Async(environment);
            Browser.DefaultBackgroundColor = Drawing.Color.Transparent;
            Browser.CoreWebView2.Settings.AreDefaultContextMenusEnabled = true;
            Browser.CoreWebView2.Settings.AreDevToolsEnabled = false;
            Browser.CoreWebView2.WebMessageReceived += OnWebMessageReceived;
            Browser.CoreWebView2.NavigationCompleted += async (_, _) =>
            {
                LoadingOverlay.Visibility = Visibility.Collapsed;
                _isPageReady = true;
                if (_openOverlayWhenReady)
                {
                    _openOverlayWhenReady = false;
                    await OpenOverlayFromHotkeyAsync();
                }
            };
            Browser.Source = new Uri(_url);
        }
        catch (Exception exception)
        {
            LoadingOverlay.Visibility = Visibility.Visible;
            System.Windows.MessageBox.Show(
                exception.Message,
                "FileManager window failed",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            _allowClose = true;
            Close();
        }
    }

    private void InitializeTrayIcon()
    {
        var contextMenu = new Forms.ContextMenuStrip();
        contextMenu.Items.Add("Open FileManager", null, (_, _) =>
            Dispatcher.Invoke(() => _ = RestoreMainWindowAsync()));
        contextMenu.Items.Add("Show Overlay", null, (_, _) =>
            Dispatcher.Invoke(() => _ = OpenOverlayFromHotkeyAsync()));
        contextMenu.Items.Add(new Forms.ToolStripSeparator());
        contextMenu.Items.Add("Exit", null, (_, _) =>
            Dispatcher.Invoke(ExitApplication));

        _trayIcon = new Forms.NotifyIcon
        {
            Icon = Drawing.SystemIcons.Application,
            Text = "FileManager",
            ContextMenuStrip = contextMenu,
            Visible = true
        };
        _trayIcon.DoubleClick += (_, _) =>
            Dispatcher.Invoke(() => _ = RestoreMainWindowAsync());
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
        var handle = new WindowInteropHelper(this).Handle;
        _windowSource = HwndSource.FromHwnd(handle);
        _windowSource?.AddHook(WndProc);

        _hotkeyRegistered = RegisterHotKey(handle, OverlayHotkeyId, ModAlt | ModNoRepeat, VkSpace);
        if (!_hotkeyRegistered)
        {
            _hotkeyRegistered = RegisterHotKey(handle, OverlayHotkeyId, ModAlt, VkSpace);
        }
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        if (_hotkeyRegistered)
        {
            UnregisterHotKey(new WindowInteropHelper(this).Handle, OverlayHotkeyId);
            _hotkeyRegistered = false;
        }

        _windowSource?.RemoveHook(WndProc);
        _windowSource = null;
        _trayIcon?.Dispose();
        _trayIcon = null;
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (_allowClose)
        {
            return;
        }

        e.Cancel = true;
        Hide();
    }

    private void OnWebMessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        if (e.WebMessageAsJson.Contains("nativeOverlayClosed", StringComparison.OrdinalIgnoreCase))
        {
            _ = HideNativeOverlayAsync();
        }
    }

    private IntPtr WndProc(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == WmHotkey && wParam.ToInt32() == OverlayHotkeyId)
        {
            handled = true;
            _ = OpenOverlayFromHotkeyAsync();
        }

        return IntPtr.Zero;
    }

    private async Task OpenOverlayFromHotkeyAsync()
    {
        EnterNativeOverlayMode();
        BringShellToFront();

        if (!_isPageReady || Browser.CoreWebView2 is null)
        {
            _openOverlayWhenReady = true;
            return;
        }

        Browser.Focus();
        await Browser.CoreWebView2.ExecuteScriptAsync(
            "if (typeof openFloatingOverlay === 'function') { openFloatingOverlay({ nativeOverlay: true }); }");
    }

    private async Task RestoreMainWindowAsync()
    {
        RestoreNormalWindowMode();
        await ResetWebOverlayStateAsync();
        Show();
        BringShellToFront();
    }

    private async Task HideNativeOverlayAsync()
    {
        await ResetWebOverlayStateAsync();
        Hide();
    }

    private async Task ResetWebOverlayStateAsync()
    {
        if (_isPageReady && Browser.CoreWebView2 is not null)
        {
            try
            {
                await Browser.CoreWebView2.ExecuteScriptAsync("""
                    document.body.classList.remove('native-overlay-mode');
                    const backdrop = document.getElementById('overlayBackdrop');
                    if (backdrop) {
                      backdrop.classList.remove('is-open');
                      backdrop.setAttribute('aria-hidden', 'true');
                    }
                    """);
            }
            catch
            {
                // The normal window should still be recoverable even if the page is navigating.
            }
        }
    }

    private void BringShellToFront()
    {
        if (WindowState == WindowState.Minimized)
        {
            WindowState = WindowState.Normal;
        }

        Show();
        Activate();

        var handle = new WindowInteropHelper(this).Handle;
        if (handle != IntPtr.Zero)
        {
            SetForegroundWindow(handle);
        }

        Topmost = true;
        Topmost = false;
        Focus();
    }

    private void EnterNativeOverlayMode()
    {
        if (!_isNativeOverlayMode)
        {
            _normalPlacement = WindowPlacementSnapshot.Capture(this);
        }

        _isNativeOverlayMode = true;
        WindowState = WindowState.Normal;
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.NoResize;
        ShowInTaskbar = false;
        Topmost = true;

        var workArea = SystemParameters.WorkArea;
        Width = Math.Min(820, Math.Max(420, workArea.Width - 48));
        Height = Math.Min(640, Math.Max(420, workArea.Height - 48));
        Left = workArea.Left + (workArea.Width - Width) / 2;
        Top = workArea.Top + (workArea.Height - Height) / 3;
    }

    private void RestoreNormalWindowMode()
    {
        if (!_isNativeOverlayMode)
        {
            ShowInTaskbar = true;
            Topmost = false;
            return;
        }

        _isNativeOverlayMode = false;
        WindowStyle = _normalPlacement?.WindowStyle ?? WindowStyle.SingleBorderWindow;
        ResizeMode = _normalPlacement?.ResizeMode ?? ResizeMode.CanResize;
        ShowInTaskbar = true;
        Topmost = false;

        if (_normalPlacement is not null)
        {
            WindowState = WindowState.Normal;
            Left = _normalPlacement.Left;
            Top = _normalPlacement.Top;
            Width = _normalPlacement.Width;
            Height = _normalPlacement.Height;
            WindowState = _normalPlacement.WindowState == WindowState.Minimized
                ? WindowState.Normal
                : _normalPlacement.WindowState;
        }
    }

    private void ExitApplication()
    {
        _allowClose = true;
        _trayIcon?.Dispose();
        _trayIcon = null;
        Close();
        System.Windows.Application.Current.Shutdown();
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    private sealed record WindowPlacementSnapshot(
        double Left,
        double Top,
        double Width,
        double Height,
        WindowState WindowState,
        WindowStyle WindowStyle,
        ResizeMode ResizeMode)
    {
        public static WindowPlacementSnapshot Capture(Window window)
        {
            var bounds = window.WindowState == WindowState.Normal
                ? new Rect(window.Left, window.Top, window.Width, window.Height)
                : window.RestoreBounds;

            return new WindowPlacementSnapshot(
                bounds.Left,
                bounds.Top,
                bounds.Width,
                bounds.Height,
                window.WindowState,
                window.WindowStyle,
                window.ResizeMode);
        }
    }
}
