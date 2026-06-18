using File2Manager.Core.Models;
using Microsoft.Data.Sqlite;

namespace File2Manager.Core.Services;

public sealed class DatabaseService
{
    private const int SqliteBusyTimeoutSeconds = 30;
    private const int MaxQuickTermLength = 64;
    private const int FileNameRank = 0;
    private const int CustomKeywordRank = 3;
    private const int CategoryRank = 4;
    private const int PathRank = 8;

    private static readonly char[] QuickTokenSeparators =
    {
        '\\', '/', ' ', '\t', '\r', '\n', '-', '_', ',', ';'
    };

    private const string MetadataSelectColumns = """
        id, full_path, file_name, directory_path, extension, created_utc, modified_utc, size_bytes,
        '' AS content_text, subject, document_type, media_type, categories, custom_keywords, is_exception,
        exception_reason, is_smart_indexed, '' AS embedding_model, NULL AS embedding_vector, indexed_utc
        """;

    private const string UpsertFileCommandText = """
        INSERT INTO files (
            full_path, file_name, directory_path, extension, created_utc, modified_utc, size_bytes,
            content_text, subject, document_type, media_type, categories, custom_keywords, is_exception,
            exception_reason, is_smart_indexed, embedding_model, embedding_vector, indexed_utc
        )
        VALUES (
            $full_path, $file_name, $directory_path, $extension, $created_utc, $modified_utc, $size_bytes,
            $content_text, $subject, $document_type, $media_type, $categories,
            COALESCE((SELECT custom_keywords FROM files WHERE full_path = $full_path), $custom_keywords),
            $is_exception, $exception_reason, $is_smart_indexed, $embedding_model, $embedding_vector, $indexed_utc
        )
        ON CONFLICT(full_path) DO UPDATE SET
            file_name = excluded.file_name,
            directory_path = excluded.directory_path,
            extension = excluded.extension,
            created_utc = excluded.created_utc,
            modified_utc = excluded.modified_utc,
            size_bytes = excluded.size_bytes,
            content_text = excluded.content_text,
            subject = excluded.subject,
            document_type = excluded.document_type,
            media_type = excluded.media_type,
            categories = excluded.categories,
            is_exception = excluded.is_exception,
            exception_reason = excluded.exception_reason,
            is_smart_indexed = excluded.is_smart_indexed,
            embedding_model = excluded.embedding_model,
            embedding_vector = excluded.embedding_vector,
            indexed_utc = excluded.indexed_utc;
        """;

    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private string _connectionString = string.Empty;

    public static string GetDatabaseFilePath(AppConfig config)
    {
        var databasePath = Path.GetFullPath(Environment.ExpandEnvironmentVariables(config.DatabasePath));
        var extension = Path.GetExtension(databasePath);
        return string.Equals(extension, ".db", StringComparison.OrdinalIgnoreCase)
            ? databasePath
            : Path.Combine(databasePath, "file2manager.db");
    }

    public async Task InitializeAsync(
        AppConfig config,
        CancellationToken cancellationToken = default,
        bool backfillQuickTerms = true)
    {
        var databaseFilePath = GetDatabaseFilePath(config);
        Directory.CreateDirectory(Path.GetDirectoryName(databaseFilePath)!);
        _connectionString = new SqliteConnectionStringBuilder
        {
            DataSource = databaseFilePath,
            Mode = SqliteOpenMode.ReadWriteCreate,
            DefaultTimeout = SqliteBusyTimeoutSeconds
        }.ToString();

        await _writeLock.WaitAsync(cancellationToken);
        try
        {
            await using var connection = CreateConnection();
            await connection.OpenAsync(cancellationToken);
            await ExecuteNonQueryAsync(connection, """
            PRAGMA journal_mode = WAL;
            PRAGMA synchronous = NORMAL;
            PRAGMA temp_store = MEMORY;
            PRAGMA cache_size = -20000;
            PRAGMA busy_timeout = 30000;
            CREATE TABLE IF NOT EXISTS files (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                full_path TEXT NOT NULL UNIQUE,
                file_name TEXT NOT NULL,
                directory_path TEXT NOT NULL,
                extension TEXT NOT NULL,
                created_utc TEXT NOT NULL,
                modified_utc TEXT NOT NULL,
                size_bytes INTEGER NOT NULL,
                content_text TEXT NOT NULL DEFAULT '',
                subject TEXT NOT NULL DEFAULT '',
                document_type TEXT NOT NULL DEFAULT '',
                media_type TEXT NOT NULL DEFAULT '',
                categories TEXT NOT NULL DEFAULT '',
                custom_keywords TEXT NOT NULL DEFAULT '',
                is_exception INTEGER NOT NULL DEFAULT 0,
                exception_reason TEXT NOT NULL DEFAULT '',
                is_smart_indexed INTEGER NOT NULL DEFAULT 0,
                embedding_model TEXT NOT NULL DEFAULT '',
                embedding_vector BLOB,
                indexed_utc TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_files_file_name ON files(file_name);
            CREATE INDEX IF NOT EXISTS idx_files_extension ON files(extension);
            CREATE INDEX IF NOT EXISTS idx_files_modified ON files(modified_utc);
            CREATE INDEX IF NOT EXISTS idx_files_smart ON files(is_smart_indexed);
            CREATE TABLE IF NOT EXISTS file_quick_tokens (
                file_id INTEGER NOT NULL,
                token TEXT NOT NULL,
                source_rank INTEGER NOT NULL,
                PRIMARY KEY (file_id, token, source_rank),
                FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE
            );
            CREATE INDEX IF NOT EXISTS idx_file_quick_tokens_token ON file_quick_tokens(token, file_id, source_rank);
            """, cancellationToken);

            await AddColumnIfMissingAsync(connection, "files", "embedding_model", "TEXT NOT NULL DEFAULT ''", cancellationToken);
            await AddColumnIfMissingAsync(connection, "files", "embedding_vector", "BLOB", cancellationToken);

            if (backfillQuickTerms)
            {
                await BackfillQuickTokensIfNeededAsync(connection, cancellationToken);
            }
        }
        finally
        {
            _writeLock.Release();
        }
    }

    public async Task UpsertFileAsync(FileIndexRecord record, CancellationToken cancellationToken = default)
    {
        await _writeLock.WaitAsync(cancellationToken);
        try
        {
            await using var connection = CreateConnection();
            await connection.OpenAsync(cancellationToken);
            using var transaction = connection.BeginTransaction();

            await using var command = connection.CreateCommand();
            command.Transaction = transaction;
            command.CommandText = UpsertFileCommandText;
            AddRecordParameters(command, record);
            await command.ExecuteNonQueryAsync(cancellationToken);
            await RefreshQuickTokensAsync(connection, transaction, record.FullPath, cancellationToken);
            transaction.Commit();
        }
        finally
        {
            _writeLock.Release();
        }
    }

    public async Task UpsertFilesAsync(IReadOnlyCollection<FileIndexRecord> records, CancellationToken cancellationToken = default)
    {
        if (records.Count == 0)
        {
            return;
        }

        await _writeLock.WaitAsync(cancellationToken);
        try
        {
            await using var connection = CreateConnection();
            await connection.OpenAsync(cancellationToken);
            using var transaction = connection.BeginTransaction();
            await using var command = connection.CreateCommand();
            command.Transaction = transaction;
            command.CommandText = UpsertFileCommandText;

            foreach (var record in records)
            {
                cancellationToken.ThrowIfCancellationRequested();
                command.Parameters.Clear();
                AddRecordParameters(command, record);
                await command.ExecuteNonQueryAsync(cancellationToken);
                await RefreshQuickTokensAsync(connection, transaction, record.FullPath, cancellationToken);
            }

            transaction.Commit();
        }
        finally
        {
            _writeLock.Release();
        }
    }

    public async Task DeleteFileAsync(string fullPath, CancellationToken cancellationToken = default)
    {
        await _writeLock.WaitAsync(cancellationToken);
        try
        {
            await using var connection = CreateConnection();
            await connection.OpenAsync(cancellationToken);
            using var transaction = connection.BeginTransaction();

            var fileId = await GetFileIdAsync(connection, transaction, fullPath, cancellationToken);
            if (fileId.HasValue)
            {
                await DeleteQuickTokensAsync(connection, transaction, fileId.Value, cancellationToken);
            }

            await using var command = connection.CreateCommand();
            command.Transaction = transaction;
            command.CommandText = "DELETE FROM files WHERE full_path = $full_path;";
            command.Parameters.AddWithValue("$full_path", fullPath);
            await command.ExecuteNonQueryAsync(cancellationToken);
            transaction.Commit();
        }
        finally
        {
            _writeLock.Release();
        }
    }

    public async Task<int> DeleteUnavailableFilesAsync(CancellationToken cancellationToken = default)
    {
        var records = await GetAllFilesAsync(cancellationToken);
        var deleted = 0;

        foreach (var record in records.Where(record => !File.Exists(record.FullPath)))
        {
            cancellationToken.ThrowIfCancellationRequested();
            await DeleteFileAsync(record.FullPath, cancellationToken);
            deleted++;
        }

        return deleted;
    }

    public async Task<List<FileIndexRecord>> SearchQuickAsync(string query, SearchFilters filters, int limit, CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);

        await using var command = connection.CreateCommand();
        var where = new List<string>();
        var normalizedQuery = query.Trim();
        var queryTerms = ExtractQuickTerms(normalizedQuery);

        AppendFilterSql(where, command, filters);
        command.Parameters.AddWithValue("$limit", limit);

        if (queryTerms.Count == 0)
        {
            command.CommandText = $"""
                SELECT {MetadataSelectColumns}
                FROM files
                {(where.Count == 0 ? string.Empty : "WHERE " + string.Join(" AND ", where))}
                ORDER BY modified_utc DESC
                LIMIT $limit;
                """;
            return await ReadRecordsAsync(command, cancellationToken);
        }

        var tokenQueries = new List<string>(queryTerms.Count);
        for (var index = 0; index < queryTerms.Count; index++)
        {
            var startParameterName = "$quick_token_start" + index;
            var endParameterName = "$quick_token_end" + index;
            command.Parameters.AddWithValue(startParameterName, queryTerms[index]);
            command.Parameters.AddWithValue(endParameterName, queryTerms[index] + "\uffff");
            tokenQueries.Add($"""
                SELECT file_id, {index} AS query_index, MIN(source_rank) AS source_rank
                FROM file_quick_tokens
                WHERE token >= {startParameterName} AND token < {endParameterName}
                GROUP BY file_id
                """);
        }
        command.Parameters.AddWithValue("$quick_term_count", queryTerms.Count);

        command.CommandText = $"""
            WITH matched_terms AS (
                {string.Join("\n                UNION ALL\n                ", tokenQueries)}
            ),
            ranked AS (
                SELECT
                    file_id,
                    COUNT(*) AS matched_count,
                    MIN(source_rank) AS best_rank,
                    SUM(CASE WHEN source_rank = {FileNameRank} THEN 1 ELSE 0 END) AS filename_hits,
                    SUM(16 - source_rank) AS rank_score
                FROM matched_terms
                GROUP BY file_id
                HAVING COUNT(*) = $quick_term_count
            )
            SELECT {MetadataSelectColumns}
            FROM files
            JOIN ranked ON ranked.file_id = files.id
            {(where.Count == 0 ? string.Empty : "WHERE " + string.Join(" AND ", where))}
            ORDER BY ranked.filename_hits DESC, ranked.best_rank ASC, ranked.rank_score DESC, modified_utc DESC
            LIMIT $limit;
            """;

        return await ReadRecordsAsync(command, cancellationToken);
    }

    public async Task<List<FileIndexRecord>> GetSmartCandidatesAsync(SearchFilters filters, int? limit = null, CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);

        await using var command = connection.CreateCommand();
        var where = new List<string> { "is_smart_indexed = 1" };
        AppendFilterSql(where, command, filters);
        command.CommandText = $"""
            SELECT * FROM files
            WHERE {string.Join(" AND ", where)}
            ORDER BY modified_utc DESC
            {(limit is > 0 ? "LIMIT $limit" : string.Empty)};
            """;
        if (limit is > 0)
        {
            command.Parameters.AddWithValue("$limit", limit.Value);
        }

        return await ReadRecordsAsync(command, cancellationToken);
    }

    public async Task<List<FileIndexRecord>> GetAllFilesAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);
        await using var command = connection.CreateCommand();
        command.CommandText = "SELECT * FROM files ORDER BY modified_utc DESC;";
        return await ReadRecordsAsync(command, cancellationToken);
    }

    public async Task<List<FileIndexRecord>> GetFilesWithCustomKeywordsAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);
        await using var command = connection.CreateCommand();
        command.CommandText = "SELECT * FROM files WHERE custom_keywords <> '' ORDER BY modified_utc DESC;";
        return await ReadRecordsAsync(command, cancellationToken);
    }

    public async Task SetCustomKeywordsAsync(string fullPath, string keywords, CancellationToken cancellationToken = default)
    {
        await _writeLock.WaitAsync(cancellationToken);
        try
        {
            await using var connection = CreateConnection();
            await connection.OpenAsync(cancellationToken);
            using var transaction = connection.BeginTransaction();

            await using var command = connection.CreateCommand();
            command.Transaction = transaction;
            command.CommandText = """
            UPDATE files
            SET custom_keywords = $custom_keywords,
                categories = TRIM(categories || '; ' || $custom_keywords, '; ')
            WHERE full_path = $full_path;
            """;
            command.Parameters.AddWithValue("$full_path", fullPath);
            command.Parameters.AddWithValue("$custom_keywords", keywords.Trim());
            await command.ExecuteNonQueryAsync(cancellationToken);
            await RefreshQuickTokensAsync(connection, transaction, fullPath, cancellationToken);
            transaction.Commit();
        }
        finally
        {
            _writeLock.Release();
        }
    }

    public async Task<int> CountFilesAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);
        await using var command = connection.CreateCommand();
        command.CommandText = "SELECT COUNT(*) FROM files;";
        var result = await command.ExecuteScalarAsync(cancellationToken);
        return Convert.ToInt32(result);
    }

    private static async Task BackfillQuickTokensIfNeededAsync(SqliteConnection connection, CancellationToken cancellationToken)
    {
        var fileCount = await ExecuteScalarLongAsync(connection, null, "SELECT COUNT(*) FROM files;", cancellationToken);
        if (fileCount == 0)
        {
            return;
        }

        var indexedCount = await ExecuteScalarLongAsync(connection, null, "SELECT COUNT(DISTINCT file_id) FROM file_quick_tokens;", cancellationToken);
        if (indexedCount >= fileCount)
        {
            return;
        }

        using var transaction = connection.BeginTransaction();
        await using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = $"""
            SELECT {MetadataSelectColumns}
            FROM files;
            """;

        var records = await ReadRecordsAsync(command, cancellationToken);
        foreach (var record in records)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await RefreshQuickTokensForRecordAsync(connection, transaction, record, cancellationToken);
        }

        transaction.Commit();
    }

    private static async Task RefreshQuickTokensAsync(
        SqliteConnection connection,
        SqliteTransaction transaction,
        string fullPath,
        CancellationToken cancellationToken)
    {
        var record = await ReadRecordByPathAsync(connection, transaction, fullPath, cancellationToken);
        if (record is null)
        {
            return;
        }

        await RefreshQuickTokensForRecordAsync(connection, transaction, record, cancellationToken);
    }

    private static async Task RefreshQuickTokensForRecordAsync(
        SqliteConnection connection,
        SqliteTransaction transaction,
        FileIndexRecord record,
        CancellationToken cancellationToken)
    {
        await DeleteQuickTokensAsync(connection, transaction, record.Id, cancellationToken);
        var tokens = BuildQuickTokens(record);
        if (tokens.Count == 0)
        {
            return;
        }

        await using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = """
            INSERT OR IGNORE INTO file_quick_tokens (file_id, token, source_rank)
            VALUES ($file_id, $token, $source_rank);
            """;

        foreach (var token in tokens)
        {
            cancellationToken.ThrowIfCancellationRequested();
            command.Parameters.Clear();
            command.Parameters.AddWithValue("$file_id", record.Id);
            command.Parameters.AddWithValue("$token", token.Token);
            command.Parameters.AddWithValue("$source_rank", token.SourceRank);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }
    }

    private static async Task DeleteQuickTokensAsync(
        SqliteConnection connection,
        SqliteTransaction transaction,
        long fileId,
        CancellationToken cancellationToken)
    {
        await using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = "DELETE FROM file_quick_tokens WHERE file_id = $file_id;";
        command.Parameters.AddWithValue("$file_id", fileId);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<long?> GetFileIdAsync(
        SqliteConnection connection,
        SqliteTransaction transaction,
        string fullPath,
        CancellationToken cancellationToken)
    {
        await using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = "SELECT id FROM files WHERE full_path = $full_path;";
        command.Parameters.AddWithValue("$full_path", fullPath);
        var result = await command.ExecuteScalarAsync(cancellationToken);
        return result is null || result == DBNull.Value ? null : Convert.ToInt64(result);
    }

    private static async Task<FileIndexRecord?> ReadRecordByPathAsync(
        SqliteConnection connection,
        SqliteTransaction transaction,
        string fullPath,
        CancellationToken cancellationToken)
    {
        await using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = $"""
            SELECT {MetadataSelectColumns}
            FROM files
            WHERE full_path = $full_path;
            """;
        command.Parameters.AddWithValue("$full_path", fullPath);

        await using var reader = await command.ExecuteReaderAsync(cancellationToken);
        return await reader.ReadAsync(cancellationToken) ? ReadRecord(reader) : null;
    }

    private static async Task<long> ExecuteScalarLongAsync(
        SqliteConnection connection,
        SqliteTransaction? transaction,
        string commandText,
        CancellationToken cancellationToken)
    {
        await using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = commandText;
        var result = await command.ExecuteScalarAsync(cancellationToken);
        return result is null || result == DBNull.Value ? 0 : Convert.ToInt64(result);
    }

    private static async Task AddColumnIfMissingAsync(
        SqliteConnection connection,
        string tableName,
        string columnName,
        string columnDefinition,
        CancellationToken cancellationToken)
    {
        await using var tableInfoCommand = connection.CreateCommand();
        tableInfoCommand.CommandText = "PRAGMA table_info(" + tableName + ");";

        await using var reader = await tableInfoCommand.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            if (string.Equals(reader.GetString(reader.GetOrdinal("name")), columnName, StringComparison.OrdinalIgnoreCase))
            {
                return;
            }
        }

        await using var alterCommand = connection.CreateCommand();
        alterCommand.CommandText = "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
        await alterCommand.ExecuteNonQueryAsync(cancellationToken);
    }

    private static IReadOnlyList<QuickToken> BuildQuickTokens(FileIndexRecord record)
    {
        var tokens = new HashSet<QuickToken>();
        AddQuickTokens(tokens, record.FileName, FileNameRank);
        AddQuickTokens(tokens, record.Extension.TrimStart('.'), FileNameRank);
        AddQuickTokens(tokens, record.CustomKeywords, CustomKeywordRank);
        AddQuickTokens(tokens, record.Subject, CategoryRank);
        AddQuickTokens(tokens, record.DocumentType, CategoryRank);
        AddQuickTokens(tokens, record.MediaType, CategoryRank);
        AddQuickTokens(tokens, record.Categories, CategoryRank);
        AddQuickTokens(tokens, record.DirectoryPath, PathRank);
        return tokens.ToArray();
    }

    private static IReadOnlyList<string> ExtractQuickTerms(string text)
    {
        return SplitQuickTokens(text)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
    }

    private static void AddQuickTokens(HashSet<QuickToken> tokens, string text, int sourceRank)
    {
        foreach (var token in SplitQuickTokens(text))
        {
            tokens.Add(new QuickToken(token, sourceRank));
        }
    }

    private static IEnumerable<string> SplitQuickTokens(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return Array.Empty<string>();
        }

        return text
            .Split(QuickTokenSeparators, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(token => token.ToLowerInvariant())
            .Where(token => token.Length > 0)
            .Select(token => token.Length > MaxQuickTermLength ? token[..MaxQuickTermLength] : token);
    }

    private SqliteConnection CreateConnection()
    {
        if (string.IsNullOrWhiteSpace(_connectionString))
        {
            throw new InvalidOperationException("The database has not been initialized.");
        }

        return new SqliteConnection(_connectionString);
    }

    private static async Task ExecuteNonQueryAsync(SqliteConnection connection, string commandText, CancellationToken cancellationToken)
    {
        await using var command = connection.CreateCommand();
        command.CommandText = commandText;
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static void AppendFilterSql(List<string> where, SqliteCommand command, SearchFilters filters)
    {
        if (filters.ModifiedFromUtc.HasValue)
        {
            where.Add("modified_utc >= $modified_from");
            command.Parameters.AddWithValue("$modified_from", filters.ModifiedFromUtc.Value.UtcDateTime.ToString("O"));
        }

        if (filters.ModifiedToUtc.HasValue)
        {
            where.Add("modified_utc < $modified_to");
            command.Parameters.AddWithValue("$modified_to", filters.ModifiedToUtc.Value.UtcDateTime.ToString("O"));
        }

        if (filters.Extensions.Count > 0)
        {
            var placeholders = new List<string>();
            for (var index = 0; index < filters.Extensions.Count; index++)
            {
                var parameterName = "$extension" + index;
                placeholders.Add(parameterName);
                command.Parameters.AddWithValue(parameterName, filters.Extensions[index]);
            }

            where.Add("extension IN (" + string.Join(", ", placeholders) + ")");
        }
    }

    private static async Task<List<FileIndexRecord>> ReadRecordsAsync(SqliteCommand command, CancellationToken cancellationToken)
    {
        var records = new List<FileIndexRecord>();
        await using var reader = await command.ExecuteReaderAsync(cancellationToken);

        while (await reader.ReadAsync(cancellationToken))
        {
            records.Add(ReadRecord(reader));
        }

        return records;
    }

    private static FileIndexRecord ReadRecord(SqliteDataReader reader)
    {
        return new FileIndexRecord
        {
            Id = reader.GetInt64(reader.GetOrdinal("id")),
            FullPath = reader.GetString(reader.GetOrdinal("full_path")),
            FileName = reader.GetString(reader.GetOrdinal("file_name")),
            DirectoryPath = reader.GetString(reader.GetOrdinal("directory_path")),
            Extension = reader.GetString(reader.GetOrdinal("extension")),
            CreatedUtc = DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("created_utc"))),
            ModifiedUtc = DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("modified_utc"))),
            SizeBytes = reader.GetInt64(reader.GetOrdinal("size_bytes")),
            ContentText = reader.GetString(reader.GetOrdinal("content_text")),
            Subject = reader.GetString(reader.GetOrdinal("subject")),
            DocumentType = reader.GetString(reader.GetOrdinal("document_type")),
            MediaType = reader.GetString(reader.GetOrdinal("media_type")),
            Categories = reader.GetString(reader.GetOrdinal("categories")),
            CustomKeywords = reader.GetString(reader.GetOrdinal("custom_keywords")),
            IsException = reader.GetInt32(reader.GetOrdinal("is_exception")) == 1,
            ExceptionReason = reader.GetString(reader.GetOrdinal("exception_reason")),
            IsSmartIndexed = reader.GetInt32(reader.GetOrdinal("is_smart_indexed")) == 1,
            EmbeddingModel = GetOptionalString(reader, "embedding_model"),
            EmbeddingVector = GetOptionalBytes(reader, "embedding_vector"),
            IndexedUtc = DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("indexed_utc")))
        };
    }

    private static void AddRecordParameters(SqliteCommand command, FileIndexRecord record)
    {
        command.Parameters.AddWithValue("$full_path", record.FullPath);
        command.Parameters.AddWithValue("$file_name", record.FileName);
        command.Parameters.AddWithValue("$directory_path", record.DirectoryPath);
        command.Parameters.AddWithValue("$extension", record.Extension);
        command.Parameters.AddWithValue("$created_utc", record.CreatedUtc.UtcDateTime.ToString("O"));
        command.Parameters.AddWithValue("$modified_utc", record.ModifiedUtc.UtcDateTime.ToString("O"));
        command.Parameters.AddWithValue("$size_bytes", record.SizeBytes);
        command.Parameters.AddWithValue("$content_text", record.ContentText);
        command.Parameters.AddWithValue("$subject", record.Subject);
        command.Parameters.AddWithValue("$document_type", record.DocumentType);
        command.Parameters.AddWithValue("$media_type", record.MediaType);
        command.Parameters.AddWithValue("$categories", record.Categories);
        command.Parameters.AddWithValue("$custom_keywords", record.CustomKeywords);
        command.Parameters.AddWithValue("$is_exception", record.IsException ? 1 : 0);
        command.Parameters.AddWithValue("$exception_reason", record.ExceptionReason);
        command.Parameters.AddWithValue("$is_smart_indexed", record.IsSmartIndexed ? 1 : 0);
        command.Parameters.AddWithValue("$embedding_model", record.EmbeddingModel);
        command.Parameters.Add("$embedding_vector", SqliteType.Blob).Value = record.EmbeddingVector.Length == 0
            ? DBNull.Value
            : record.EmbeddingVector;
        command.Parameters.AddWithValue("$indexed_utc", record.IndexedUtc.UtcDateTime.ToString("O"));
    }

    private static string GetOptionalString(SqliteDataReader reader, string columnName)
    {
        var ordinal = TryGetOrdinal(reader, columnName);
        return ordinal >= 0 && !reader.IsDBNull(ordinal)
            ? reader.GetString(ordinal)
            : string.Empty;
    }

    private static byte[] GetOptionalBytes(SqliteDataReader reader, string columnName)
    {
        var ordinal = TryGetOrdinal(reader, columnName);
        return ordinal >= 0 && !reader.IsDBNull(ordinal)
            ? (byte[])reader.GetValue(ordinal)
            : Array.Empty<byte>();
    }

    private static int TryGetOrdinal(SqliteDataReader reader, string columnName)
    {
        for (var index = 0; index < reader.FieldCount; index++)
        {
            if (string.Equals(reader.GetName(index), columnName, StringComparison.OrdinalIgnoreCase))
            {
                return index;
            }
        }

        return -1;
    }

    private readonly record struct QuickToken(string Token, int SourceRank);
}
