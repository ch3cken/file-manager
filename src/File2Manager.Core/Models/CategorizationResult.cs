namespace File2Manager.Core.Models;

public sealed class CategorizationResult
{
    public string Subject { get; init; } = "General";

    public string DocumentType { get; init; } = "Unknown";

    public string MediaType { get; init; } = "Other";

    public IReadOnlyList<string> Categories { get; init; } = Array.Empty<string>();
}
