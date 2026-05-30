using File2Manager.Core.Models;
using File2Manager.Core.Search;
using File2Manager.Core.Services;
using Microsoft.Data.Sqlite;

var testRoot = Path.Combine(Path.GetTempPath(), "file2manager-smoke-" + Guid.NewGuid().ToString("N"));
var documentsRoot = Path.Combine(testRoot, "documents");
var databaseRoot = Path.Combine(testRoot, "database");
FileIndexingService? indexingService = null;
FileStream? lockedStream = null;

try
{
    Directory.CreateDirectory(documentsRoot);

    var machineLearningPaper = Path.Combine(documentsRoot, "machine-learning-paper.txt");
    var operatingSystemsAssignment = Path.Combine(documentsRoot, "operating-systems-assignment.txt");
    var corruptedDocx = Path.Combine(documentsRoot, "corrupted-project-report.docx");
    var lockedFile = Path.Combine(documentsRoot, "locked-notes.txt");
    var unsupportedBinary = Path.Combine(documentsRoot, "raw-camera-dump.bin");
    var requirementsNote = Path.Combine(documentsRoot, "requirements-review-note.txt");

    await File.WriteAllTextAsync(machineLearningPaper, "A machine learning paper about embeddings, vector search, semantic classification, retrieval augmented generation, chunking, reranking, and citations.");
    await File.WriteAllTextAsync(operatingSystemsAssignment, "Operating systems assignment covering process scheduling, threads, and virtual memory.");
    await File.WriteAllTextAsync(corruptedDocx, "this is not a valid docx package");
    await File.WriteAllTextAsync(lockedFile, "locked file content should fall back to metadata-only indexing");
    await File.WriteAllBytesAsync(unsupportedBinary, new byte[] { 0, 1, 2, 3, 4, 5 });
    await File.WriteAllTextAsync(requirementsNote, "Requirements review note for a kitchen inventory checklist and stakeholder comments.");
    for (var index = 0; index < 620; index++)
    {
        await File.WriteAllTextAsync(
            Path.Combine(documentsRoot, $"bulk-generated-{index:0000}.txt"),
            $"Bulk indexing fixture {index} for fast initial indexing, batched sqlite writes, and generated tags.");
    }
    File.SetLastWriteTime(operatingSystemsAssignment, DateTime.Now.Date.AddDays(-1).AddHours(12));
    lockedStream = File.Open(lockedFile, FileMode.Open, FileAccess.ReadWrite, FileShare.None);

    var config = new AppConfig
    {
        InstallationPath = testRoot,
        DatabasePath = databaseRoot,
        QuickSearchDirectories = new List<string> { documentsRoot },
        SmartSearchDirectories = new List<string> { documentsRoot },
        SmartSearchExtensions = new List<string> { ".txt", ".docx", ".bin" },
        CategorizationDirectories = new List<string> { documentsRoot },
        Theme = "Dark",
        HotkeyGesture = "Alt+Space",
        IsConfigured = true
    };
    config.Normalize();

    var databaseService = new DatabaseService();
    await databaseService.InitializeAsync(config);
    indexingService = new FileIndexingService(databaseService, new DocumentTextExtractor(), new CategorizationService());
    await indexingService.BuildIndexAsync(config);

    var indexedCount = await databaseService.CountFilesAsync();
    Expect(indexedCount >= 626, "expected bulk fixture files to be indexed");
    var allRecords = await databaseService.GetAllFilesAsync();
    Expect(allRecords.Any(record => record.FileName == "corrupted-project-report.docx" && record.IsException), "corrupted DOCX was not stored as an exception-state metadata entry");
    Expect(allRecords.Any(record => record.FileName == "locked-notes.txt" && record.IsException), "locked file was not stored as an exception-state metadata entry");
    Expect(allRecords.Any(record => record.FileName == "raw-camera-dump.bin"), "unsupported binary file was not indexed by metadata");
    Expect(allRecords.Any(record =>
        record.FileName == "machine-learning-paper.txt" &&
        record.Categories.Contains("Retrieval Augmented Generation", StringComparison.OrdinalIgnoreCase)),
        "content-derived dynamic tag was not generated");
    Expect(allRecords.All(record =>
        !record.Categories.Contains("Software Engineering", StringComparison.OrdinalIgnoreCase) &&
        !record.Subject.Contains("Software Engineering", StringComparison.OrdinalIgnoreCase)),
        "static topical tag should not be generated");
    Expect(allRecords.Any(record =>
        record.FileName == "requirements-review-note.txt" &&
        record.Categories.Contains("Requirements Review", StringComparison.OrdinalIgnoreCase)),
        "filename/content-derived tag was not generated for requirements note");

    var searchService = new SearchService(databaseService, new SearchQueryParser());
    var quickResults = await searchService.QuickSearchAsync("machine", 10);
    Expect(quickResults.Any(result => result.FileName == "machine-learning-paper.txt"), "quick search did not find the machine learning paper");

    var bulkResults = await searchService.QuickSearchAsync("bulk-generated-0619", 10);
    Expect(bulkResults.Any(result => result.FileName == "bulk-generated-0619.txt"), "batched initial indexing missed a generated bulk file");

    var smartResults = await searchService.SmartSearchAsync("operating systems assignment yesterday", 10);
    Expect(smartResults.Any(result => result.FileName == "operating-systems-assignment.txt"), "smart search did not find the OS assignment");

    var ragResults = await searchService.SmartSearchAsync("retrieval augmented generation citations", 10);
    Expect(ragResults.Any(result => result.FileName == "machine-learning-paper.txt"), "smart search did not use generated RAG-related tags/content");

    await databaseService.SetCustomKeywordsAsync(machineLearningPaper, "cs350 reference");
    var keywordResults = await searchService.QuickSearchAsync("cs350", 10);
    Expect(keywordResults.Any(result => result.FileName == "machine-learning-paper.txt"), "custom keyword search did not find the tagged file");

    await RunOptionalDownloadsProbeAsync(databaseService, indexingService);

    Console.WriteLine("Smoke tests passed: indexing, quick search, smart search, and custom keywords.");
}
finally
{
    lockedStream?.Dispose();
    indexingService?.Dispose();
    SqliteConnection.ClearAllPools();

    if (Directory.Exists(testRoot))
    {
        Directory.Delete(testRoot, recursive: true);
    }
}

static void Expect(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

static async Task RunOptionalDownloadsProbeAsync(DatabaseService databaseService, FileIndexingService indexingService)
{
    if (!string.Equals(Environment.GetEnvironmentVariable("FILE2MANAGER_REAL_SCAN"), "1", StringComparison.Ordinal))
    {
        return;
    }

    var downloadsRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
    if (!Directory.Exists(downloadsRoot))
    {
        Console.WriteLine("Real-folder probe skipped: Downloads folder was not found.");
        return;
    }

    var probeDatabaseRoot = Path.Combine(Path.GetTempPath(), "file2manager-downloads-probe-" + Guid.NewGuid().ToString("N"));
    try
    {
        var probeConfig = new AppConfig
        {
            InstallationPath = probeDatabaseRoot,
            DatabasePath = probeDatabaseRoot,
            QuickSearchDirectories = new List<string> { downloadsRoot },
            SmartSearchDirectories = new List<string> { downloadsRoot },
            SmartSearchExtensions = new List<string> { ".txt", ".md", ".csv", ".json", ".xml", ".html", ".pdf", ".docx" },
            CategorizationDirectories = new List<string> { downloadsRoot },
            IsConfigured = true
        };
        probeConfig.Normalize();

        await databaseService.InitializeAsync(probeConfig);
        var files = Directory
            .EnumerateFiles(downloadsRoot, "*", new EnumerationOptions
            {
                RecurseSubdirectories = true,
                IgnoreInaccessible = true,
                AttributesToSkip = FileAttributes.System | FileAttributes.Temporary
            })
            .Take(150)
            .ToArray();

        var indexed = 0;
        var exceptions = 0;
        foreach (var file in files)
        {
            var record = await indexingService.IndexFileAsync(file, probeConfig);
            if (record is null)
            {
                continue;
            }

            indexed++;
            if (record.IsException)
            {
                exceptions++;
            }
        }

        var searchService = new SearchService(databaseService, new SearchQueryParser());
        _ = await searchService.QuickSearchAsync("download", 10);
        _ = await searchService.SmartSearchAsync("recent document", 10);

        Console.WriteLine($"Real-folder probe passed: sampled {files.Length} Downloads files, indexed {indexed}, exception-state entries {exceptions}.");
    }
    finally
    {
        SqliteConnection.ClearAllPools();
        if (Directory.Exists(probeDatabaseRoot))
        {
            Directory.Delete(probeDatabaseRoot, recursive: true);
        }
    }
}
