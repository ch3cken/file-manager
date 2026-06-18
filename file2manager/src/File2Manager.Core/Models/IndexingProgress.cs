namespace File2Manager.Core.Models;

public sealed class IndexingProgress
{
    public string Message { get; init; } = string.Empty;

    public int FilesSeen { get; init; }

    public int FilesIndexed { get; init; }

    public int Exceptions { get; init; }

    public bool IsComplete { get; init; }
}
