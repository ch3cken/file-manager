namespace File2Manager.Core.Models;

public sealed class FileIndexRecord
{
    public long Id { get; set; }

    public string FullPath { get; set; } = string.Empty;

    public string FileName { get; set; } = string.Empty;

    public string DirectoryPath { get; set; } = string.Empty;

    public string Extension { get; set; } = string.Empty;

    public DateTimeOffset CreatedUtc { get; set; }

    public DateTimeOffset ModifiedUtc { get; set; }

    public long SizeBytes { get; set; }

    public string ContentText { get; set; } = string.Empty;

    public string Subject { get; set; } = string.Empty;

    public string DocumentType { get; set; } = string.Empty;

    public string MediaType { get; set; } = string.Empty;

    public string Categories { get; set; } = string.Empty;

    public string CustomKeywords { get; set; } = string.Empty;

    public bool IsException { get; set; }

    public string ExceptionReason { get; set; } = string.Empty;

    public bool IsSmartIndexed { get; set; }

    public DateTimeOffset IndexedUtc { get; set; } = DateTimeOffset.UtcNow;
}
