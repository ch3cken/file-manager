using File2Manager.Core.Models;
using System.Threading.Channels;

namespace File2Manager.Core.Services;

public sealed class FileIndexingService : IDisposable
{
    private const int InitialIndexBatchSize = 512;

    private readonly DatabaseService _databaseService;
    private readonly DocumentTextExtractor _textExtractor;
    private readonly CategorizationService _categorizationService;
    private readonly List<FileSystemWatcher> _watchers = new();
    private readonly SemaphoreSlim _singleFileIndexLock = new(1, 1);
    private AppConfig? _activeConfig;
    private bool _disposed;

    public FileIndexingService(
        DatabaseService databaseService,
        DocumentTextExtractor textExtractor,
        CategorizationService categorizationService)
    {
        _databaseService = databaseService;
        _textExtractor = textExtractor;
        _categorizationService = categorizationService;
    }

    public async Task BuildIndexAsync(
        AppConfig config,
        IProgress<IndexingProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        config.Normalize();
        _activeConfig = config;
        await _databaseService.InitializeAsync(config, cancellationToken);

        var filesSeen = 0;
        var filesIndexed = 0;
        var exceptions = 0;
        var scope = IndexingScope.FromConfig(config);
        var workerCount = GetInitialIndexingWorkerCount();
        var roots = config.GetEffectiveQuickSearchDirectories()
            .Where(Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        var pathChannel = Channel.CreateBounded<string>(new BoundedChannelOptions(workerCount * 256)
        {
            FullMode = BoundedChannelFullMode.Wait,
            SingleReader = false,
            SingleWriter = true
        });
        var recordChannel = Channel.CreateBounded<FileIndexRecord>(new BoundedChannelOptions(InitialIndexBatchSize * 2)
        {
            FullMode = BoundedChannelFullMode.Wait,
            SingleReader = true,
            SingleWriter = false
        });

        progress?.Report(new IndexingProgress
        {
            Message = "Starting fast index with " + workerCount + " workers.",
            FilesSeen = filesSeen,
            FilesIndexed = filesIndexed,
            Exceptions = exceptions
        });

        var producer = Task.Run(async () =>
        {
            try
            {
                foreach (var root in roots)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    progress?.Report(new IndexingProgress
                    {
                        Message = "Scanning " + root,
                        FilesSeen = Volatile.Read(ref filesSeen),
                        FilesIndexed = Volatile.Read(ref filesIndexed),
                        Exceptions = Volatile.Read(ref exceptions)
                    });

                    foreach (var filePath in EnumerateFiles(root))
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        var seen = Interlocked.Increment(ref filesSeen);
                        await pathChannel.Writer.WriteAsync(filePath, cancellationToken);

                        if (seen % 500 == 0)
                        {
                            progress?.Report(new IndexingProgress
                            {
                                Message = "Discovered " + seen + " files. Indexed " + Volatile.Read(ref filesIndexed) + ".",
                                FilesSeen = seen,
                                FilesIndexed = Volatile.Read(ref filesIndexed),
                                Exceptions = Volatile.Read(ref exceptions)
                            });
                        }
                    }
                }
            }
            finally
            {
                pathChannel.Writer.TryComplete();
            }
        }, cancellationToken);

        var workers = Enumerable.Range(0, workerCount)
            .Select(_ => Task.Run(async () =>
            {
                await foreach (var filePath in pathChannel.Reader.ReadAllAsync(cancellationToken))
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    try
                    {
                        var record = CreateIndexRecord(filePath, scope);
                        if (record is null)
                        {
                            continue;
                        }

                        if (record.IsException)
                        {
                            Interlocked.Increment(ref exceptions);
                        }

                        await recordChannel.Writer.WriteAsync(record, cancellationToken);
                    }
                    catch (Exception exception) when (IsRecoverableIndexingException(exception))
                    {
                        Interlocked.Increment(ref exceptions);
                    }
                }
            }, cancellationToken))
            .ToArray();

        var writer = Task.Run(async () =>
        {
            var batch = new List<FileIndexRecord>(InitialIndexBatchSize);

            async Task FlushBatchAsync()
            {
                if (batch.Count == 0)
                {
                    return;
                }

                var batchToWrite = batch.ToArray();
                batch.Clear();
                await _databaseService.UpsertFilesAsync(batchToWrite, cancellationToken);
                var indexed = Interlocked.Add(ref filesIndexed, batchToWrite.Length);

                progress?.Report(new IndexingProgress
                {
                    Message = "Indexed " + indexed + " of " + Volatile.Read(ref filesSeen) + " discovered files.",
                    FilesSeen = Volatile.Read(ref filesSeen),
                    FilesIndexed = indexed,
                    Exceptions = Volatile.Read(ref exceptions)
                });
            }

            await foreach (var record in recordChannel.Reader.ReadAllAsync(cancellationToken))
            {
                batch.Add(record);

                if (batch.Count >= InitialIndexBatchSize)
                {
                    await FlushBatchAsync();
                }
            }

            await FlushBatchAsync();
        }, cancellationToken);

        await producer;
        try
        {
            await Task.WhenAll(workers);
            recordChannel.Writer.TryComplete();
        }
        catch (Exception exception)
        {
            recordChannel.Writer.TryComplete(exception);
            throw;
        }

        await writer;

        var deleted = await _databaseService.DeleteUnavailableFilesAsync(cancellationToken);
        progress?.Report(new IndexingProgress
        {
            Message = "Index complete. Removed " + deleted + " stale entries. Exceptions: " + exceptions + ".",
            FilesSeen = Volatile.Read(ref filesSeen),
            FilesIndexed = Volatile.Read(ref filesIndexed),
            Exceptions = Volatile.Read(ref exceptions),
            IsComplete = true
        });
    }

    public void StartWatchers(AppConfig config, Action<IndexingProgress>? progressCallback = null)
    {
        StopWatchers();
        config.Normalize();
        _activeConfig = config;

        var roots = config.GetEffectiveQuickSearchDirectories()
            .Concat(config.CategorizationDirectories)
            .Where(Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        foreach (var root in roots)
        {
            var watcher = new FileSystemWatcher(root)
            {
                IncludeSubdirectories = true,
                EnableRaisingEvents = true,
                NotifyFilter = NotifyFilters.FileName |
                               NotifyFilters.DirectoryName |
                               NotifyFilters.LastWrite |
                               NotifyFilters.CreationTime |
                               NotifyFilters.Size
            };

            watcher.Created += (_, args) => QueueFileUpdate(args.FullPath, progressCallback);
            watcher.Changed += (_, args) => QueueFileUpdate(args.FullPath, progressCallback);
            watcher.Renamed += (_, args) =>
            {
                _ = _databaseService.DeleteFileAsync(args.OldFullPath);
                QueueFileUpdate(args.FullPath, progressCallback);
            };
            watcher.Deleted += (_, args) => _ = _databaseService.DeleteFileAsync(args.FullPath);

            _watchers.Add(watcher);
        }
    }

    public void StopWatchers()
    {
        foreach (var watcher in _watchers)
        {
            watcher.EnableRaisingEvents = false;
            watcher.Dispose();
        }

        _watchers.Clear();
    }

    public async Task<FileIndexRecord?> IndexFileAsync(string filePath, AppConfig config, CancellationToken cancellationToken = default)
    {
        await _singleFileIndexLock.WaitAsync(cancellationToken);
        try
        {
            if (!File.Exists(filePath))
            {
                await _databaseService.DeleteFileAsync(filePath, cancellationToken);
                return null;
            }

            var record = CreateIndexRecord(filePath, IndexingScope.FromConfig(config));
            if (record is null)
            {
                return null;
            }

            await _databaseService.UpsertFileAsync(record, cancellationToken);
            return record;
        }
        finally
        {
            _singleFileIndexLock.Release();
        }
    }

    private FileIndexRecord? CreateIndexRecord(string filePath, IndexingScope scope)
    {
        if (string.IsNullOrWhiteSpace(filePath) || Directory.Exists(filePath))
        {
            return null;
        }

        if (!File.Exists(filePath))
        {
            return null;
        }

        FileInfo fileInfo;
        try
        {
            fileInfo = new FileInfo(filePath);
        }
        catch
        {
            return null;
        }

        FileAttributes attributes;
        long sizeBytes;
        DateTime createdUtc;
        DateTime modifiedUtc;

        try
        {
            attributes = fileInfo.Attributes;
            sizeBytes = fileInfo.Length;
            createdUtc = fileInfo.CreationTimeUtc;
            modifiedUtc = fileInfo.LastWriteTimeUtc;
        }
        catch (Exception exception) when (IsRecoverableIndexingException(exception))
        {
            return CreateExceptionRecord(fileInfo, exception.Message);
        }

        if ((attributes & FileAttributes.System) == FileAttributes.System ||
            (attributes & FileAttributes.Temporary) == FileAttributes.Temporary)
        {
            return null;
        }

        var extension = fileInfo.Extension.ToLowerInvariant();
        var isSmartScope = scope.SmartSearchExtensions.Contains(extension) &&
                           IsUnderAnyDirectory(fileInfo.FullName, scope.SmartSearchDirectories);
        var isCategorizationScope = IsUnderAnyDirectory(fileInfo.FullName, scope.CategorizationDirectories);
        var shouldExtractText = _textExtractor.CanExtract(extension) && (isSmartScope || isCategorizationScope);
        var contentText = string.Empty;
        var isException = false;
        var exceptionReason = string.Empty;

        if (shouldExtractText)
        {
            try
            {
                contentText = _textExtractor.ExtractText(fileInfo.FullName);
            }
            catch (Exception exception) when (IsRecoverableIndexingException(exception))
            {
                isException = true;
                exceptionReason = NormalizeExceptionMessage(exception);
            }
        }
        else if ((isSmartScope || isCategorizationScope) && !string.IsNullOrWhiteSpace(extension) && !_textExtractor.CanExtract(extension))
        {
            exceptionReason = "Content extraction is not supported for " + extension + ". Metadata was indexed.";
        }

        var categorization = _categorizationService.Categorize(fileInfo, contentText);
        return new FileIndexRecord
        {
            FullPath = fileInfo.FullName,
            FileName = fileInfo.Name,
            DirectoryPath = fileInfo.DirectoryName ?? string.Empty,
            Extension = extension,
            CreatedUtc = createdUtc,
            ModifiedUtc = modifiedUtc,
            SizeBytes = sizeBytes,
            ContentText = contentText,
            Subject = categorization.Subject,
            DocumentType = categorization.DocumentType,
            MediaType = categorization.MediaType,
            Categories = string.Join("; ", categorization.Categories),
            IsException = isException,
            ExceptionReason = exceptionReason,
            IsSmartIndexed = isSmartScope,
            IndexedUtc = DateTimeOffset.UtcNow
        };
    }

    private void QueueFileUpdate(string filePath, Action<IndexingProgress>? progressCallback)
    {
        var config = _activeConfig;
        if (config is null)
        {
            return;
        }

        _ = Task.Run(async () =>
        {
            try
            {
                await Task.Delay(500);
                var record = await IndexFileAsync(filePath, config);
                if (record is not null)
                {
                    progressCallback?.Invoke(new IndexingProgress
                    {
                        Message = "Updated " + record.FileName,
                        FilesSeen = 1,
                        FilesIndexed = 1,
                        Exceptions = record.IsException ? 1 : 0,
                        IsComplete = true
                    });
                }
            }
            catch
            {
                progressCallback?.Invoke(new IndexingProgress
                {
                    Message = "A file change could not be indexed.",
                    Exceptions = 1,
                    IsComplete = true
                });
            }
        });
    }

    private static IEnumerable<string> EnumerateFiles(string root)
    {
        var options = new EnumerationOptions
        {
            RecurseSubdirectories = true,
            IgnoreInaccessible = true,
            ReturnSpecialDirectories = false,
            AttributesToSkip = FileAttributes.System | FileAttributes.Temporary
        };

        try
        {
            return Directory.EnumerateFiles(root, "*", options);
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    private static bool IsUnderAnyDirectory(string filePath, IEnumerable<string> directories)
    {
        var normalizedFilePath = Path.GetFullPath(filePath);

        foreach (var directory in directories)
        {
            if (normalizedFilePath.StartsWith(directory, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    private static int GetInitialIndexingWorkerCount()
    {
        return Math.Clamp(Environment.ProcessorCount * 2, 4, 32);
    }

    private static FileIndexRecord CreateExceptionRecord(FileInfo fileInfo, string reason)
    {
        return new FileIndexRecord
        {
            FullPath = fileInfo.FullName,
            FileName = fileInfo.Name,
            DirectoryPath = fileInfo.DirectoryName ?? string.Empty,
            Extension = fileInfo.Extension.ToLowerInvariant(),
            CreatedUtc = DateTimeOffset.UtcNow,
            ModifiedUtc = DateTimeOffset.UtcNow,
            SizeBytes = 0,
            Subject = string.Empty,
            DocumentType = "Unreadable",
            MediaType = "Other",
            Categories = "Unreadable; Metadata Only",
            IsException = true,
            ExceptionReason = NormalizeExceptionMessage(reason),
            IsSmartIndexed = false,
            IndexedUtc = DateTimeOffset.UtcNow
        };
    }

    private static bool IsRecoverableIndexingException(Exception exception)
    {
        return exception is not OperationCanceledException &&
               exception is not Microsoft.Data.Sqlite.SqliteException &&
               exception is not OutOfMemoryException;
    }

    private static string NormalizeExceptionMessage(Exception exception)
    {
        return NormalizeExceptionMessage(exception.Message);
    }

    private static string NormalizeExceptionMessage(string message)
    {
        return string.IsNullOrWhiteSpace(message)
            ? "File content could not be read. Metadata was indexed."
            : message.Trim();
    }

    private sealed class IndexingScope
    {
        private IndexingScope(
            HashSet<string> smartSearchExtensions,
            string[] smartSearchDirectories,
            string[] categorizationDirectories)
        {
            SmartSearchExtensions = smartSearchExtensions;
            SmartSearchDirectories = smartSearchDirectories;
            CategorizationDirectories = categorizationDirectories;
        }

        public HashSet<string> SmartSearchExtensions { get; }

        public string[] SmartSearchDirectories { get; }

        public string[] CategorizationDirectories { get; }

        public static IndexingScope FromConfig(AppConfig config)
        {
            config.Normalize();
            return new IndexingScope(
                config.SmartSearchExtensions.ToHashSet(StringComparer.OrdinalIgnoreCase),
                NormalizeDirectories(config.SmartSearchDirectories),
                NormalizeDirectories(config.CategorizationDirectories));
        }

        private static string[] NormalizeDirectories(IEnumerable<string> directories)
        {
            return directories
                .Where(Directory.Exists)
                .Select(directory => Path.GetFullPath(directory))
                .Select(directory => directory.EndsWith(Path.DirectorySeparatorChar)
                    ? directory
                    : directory + Path.DirectorySeparatorChar)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToArray();
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        StopWatchers();
        _singleFileIndexLock.Dispose();
        _disposed = true;
    }
}
