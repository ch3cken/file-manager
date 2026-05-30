using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using File2Manager.Core.Models;
using File2Manager.Core.Services;

namespace File2Manager.App;

public partial class OverlayWindow : Window
{
    private const string SearchBlockedDuringIndexMessage = "Initial indexing is running. Search is disabled until indexing completes.";
    private readonly SearchService _searchService;
    private readonly Func<bool> _isSearchBlocked;
    private readonly DispatcherTimer _searchDebounceTimer;
    private CancellationTokenSource? _searchCancellationTokenSource;

    public OverlayWindow(SearchService searchService, Func<bool> isSearchBlocked)
    {
        _searchService = searchService;
        _isSearchBlocked = isSearchBlocked;
        InitializeComponent();

        _searchDebounceTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(180)
        };
        _searchDebounceTimer.Tick += async (_, _) =>
        {
            _searchDebounceTimer.Stop();
            await RunSearchAsync();
        };
    }

    public void ActivateSearch()
    {
        OverlaySearchTextBox.Clear();
        OverlayQuickResultsList.ItemsSource = null;
        OverlaySmartResultsList.ItemsSource = null;
        OverlayStatusText.Text = string.Empty;
        UpdateSearchAvailability();
        Show();
        Activate();
        if (!IsSearchBlocked())
        {
            OverlaySearchTextBox.Focus();
        }
    }

    public void UpdateSearchAvailability()
    {
        var isBlocked = IsSearchBlocked();
        OverlaySearchTextBox.IsReadOnly = isBlocked;
        OverlaySearchTextBox.IsHitTestVisible = !isBlocked;
        OverlayQuickResultsList.IsHitTestVisible = !isBlocked;
        OverlaySmartResultsList.IsHitTestVisible = !isBlocked;
        OverlayQuickResultsList.Opacity = isBlocked ? 0.72 : 1;
        OverlaySmartResultsList.Opacity = isBlocked ? 0.72 : 1;

        if (isBlocked)
        {
            _searchDebounceTimer.Stop();
            _searchCancellationTokenSource?.Cancel();
            OverlayQuickResultsList.ItemsSource = null;
            OverlaySmartResultsList.ItemsSource = null;
            OverlayStatusText.Text = SearchBlockedDuringIndexMessage;
        }
    }

    private void OnSearchTextChanged(object sender, TextChangedEventArgs e)
    {
        if (IsSearchBlocked())
        {
            UpdateSearchAvailability();
            return;
        }

        _searchDebounceTimer.Stop();
        _searchDebounceTimer.Start();
    }

    private async Task RunSearchAsync()
    {
        if (IsSearchBlocked())
        {
            _searchCancellationTokenSource?.Cancel();
            UpdateSearchAvailability();
            return;
        }

        var query = OverlaySearchTextBox.Text.Trim();
        _searchCancellationTokenSource?.Cancel();
        _searchCancellationTokenSource = new CancellationTokenSource();
        var cancellationToken = _searchCancellationTokenSource.Token;

        try
        {
            OverlayStatusText.Text = string.IsNullOrWhiteSpace(query) ? "Recent indexed files" : "Searching...";
            QuickColumn.Width = new GridLength(3, GridUnitType.Star);
            SmartColumn.Width = new GridLength(2, GridUnitType.Star);

            var quickTask = _searchService.QuickSearchAsync(query, 25, cancellationToken);
            var quickResults = await quickTask;
            OverlayQuickResultsList.ItemsSource = quickResults;

            var smartResults = await _searchService.SmartSearchAsync(query, 25, cancellationToken);
            OverlaySmartResultsList.ItemsSource = smartResults;

            if (quickResults.Count == 0 && smartResults.Count > 0)
            {
                QuickColumn.Width = new GridLength(2, GridUnitType.Star);
                SmartColumn.Width = new GridLength(3, GridUnitType.Star);
            }

            OverlayStatusText.Text = quickResults.Count == 0 && smartResults.Count == 0
                ? "No results found"
                : quickResults.Count + " quick results, " + smartResults.Count + " smart results";
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            OverlayStatusText.Text = exception.Message;
        }
    }

    private void OnSearchKeyDown(object sender, KeyEventArgs e)
    {
        if (IsSearchBlocked())
        {
            UpdateSearchAvailability();
            e.Handled = true;
        }
        else if (e.Key == Key.Enter)
        {
            OpenSelectedResult();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            Hide();
            e.Handled = true;
        }
    }

    private void OnWindowKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            Hide();
            e.Handled = true;
        }
    }

    private void OnDeactivated(object sender, EventArgs e)
    {
        Hide();
    }

    private void OnResultOpen(object sender, MouseButtonEventArgs e)
    {
        OpenSelectedResult(sender as ListView);
    }

    private void OpenSelectedResult(ListView? listView = null)
    {
        if (IsSearchBlocked())
        {
            UpdateSearchAvailability();
            return;
        }

        var selectedItem = listView?.SelectedItem ??
                           OverlayQuickResultsList.SelectedItem ??
                           OverlaySmartResultsList.SelectedItem;

        if (selectedItem is not SearchResultItem result || !File.Exists(result.FullPath))
        {
            return;
        }

        Process.Start(new ProcessStartInfo(result.FullPath)
        {
            UseShellExecute = true
        });
        Hide();
    }

    private bool IsSearchBlocked()
    {
        return _isSearchBlocked();
    }
}
