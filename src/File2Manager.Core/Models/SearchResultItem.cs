namespace File2Manager.Core.Models;

public sealed class SearchResultItem
{
    public string FileName { get; init; } = string.Empty;

    public string FullPath { get; init; } = string.Empty;

    public string DirectoryPath { get; init; } = string.Empty;

    public string Extension { get; init; } = string.Empty;

    public DateTimeOffset ModifiedUtc { get; init; }

    public double Score { get; init; }

    public string Summary { get; init; } = string.Empty;

    public string Tags { get; init; } = string.Empty;

    public string ModifiedLocalText => ModifiedUtc.ToLocalTime().ToString("yyyy-MM-dd HH:mm");
}
