namespace File2Manager.Core.Models;

public sealed class SearchFilters
{
    public DateTimeOffset? ModifiedFromUtc { get; init; }

    public DateTimeOffset? ModifiedToUtc { get; init; }

    public IReadOnlyList<string> Extensions { get; init; } = Array.Empty<string>();

    public IReadOnlyList<string> SemanticTerms { get; init; } = Array.Empty<string>();

    public string CleanQuery { get; init; } = string.Empty;

    public bool HasDateFilter => ModifiedFromUtc.HasValue || ModifiedToUtc.HasValue;

    public bool HasExtensionFilter => Extensions.Count > 0;
}
