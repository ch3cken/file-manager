using File2Manager.Core.Models;
using Microsoft.Data.Sqlite;

namespace File2Manager.Core.Services;

public sealed class DatabaseService
{
    private const string UpsertFileCommandText = """
        INSERT INTO files (
            full_path, file_name, directory_path, extension, created_utc, modified_utc, size_bytes,
            content_text, subject, document_type, media_type, categories, custom_keywords, is_exception,
            exception_reason, is_smart_indexed, indexed_utc
        )
        VALUES (
            $full_path, $file_name, $directory_path, $extension, $created_utc, $modified_utc, $size_bytes,
            $content_text, $subject, $document_type, $media_type, $categories,
            COALESCE((SELECT custom_keywords FROM files WHERE full_path = $full_path), $custom_keywords),
            $is_exception, $exception_reason, $is_smart_indexed, $indexed_utc
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
            indexed_utc = excluded.indexed_utc;
        """;

    private string _connectionString = string.Empty;

    public static string GetDatabaseFilePath(AppConfig config)
    {
        var databasePath = Path.GetFullPath(Environment.ExpandEnvironmentVariables(config.DatabasePath));
        var extension = Path.GetExtension(databasePath);
        return string.Equals(extension, ".db", StringComparison.OrdinalIgnoreCase)
            ? databasePath
            : Path.Combine(databasePath, "file2manager.db");
    }

    public async Task InitializeAsync(AppConfig config, CancellationToken cancellationToken = default)
    {
        var databaseFilePath = GetDatabaseFilePath(config);
        Directory.CreateDirectory(Path.GetDirectoryName(databaseFilePath)!);
        _connectionString = new SqliteConnectionStringBuilder
        {
            DataSource = databaseFilePath,
            Mode = SqliteOpenMode.ReadWriteCreate
        }.ToString();

        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);
        await ExecuteNonQueryAsync(connection, """
            PRAGMA journal_mode = WAL;
            PRAGMA synchronous = NORMAL;
            PRAGMA temp_store = MEMORY;
            PRAGMA cache_size = -20000;
            PRAGMA busy_timeout = 5000;
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
                indexed_utc TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_files_file_name ON files(file_name);
            CREATE INDEX IF NOT EXISTS idx_files_extension ON files(extension);
            CREATE INDEX IF NOT EXISTS idx_files_modified ON files(modified_utc);
            CREATE INDEX IF NOT EXISTS idx_files_smart ON files(is_smart_indexed);
            """, cancellationToken);
    }

    public async Task UpsertFileAsync(FileIndexRecord record, CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);

        await using var command = connection.CreateCommand();
        command.CommandText = UpsertFileCommandText;
        AddRecordParameters(command, record);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task UpsertFilesAsync(IReadOnlyCollection<FileIndexRecord> records, CancellationToken cancellationToken = default)
    {
        if (records.Count == 0)
        {
            return;
        }

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
        }

        transaction.Commit();
    }

    public async Task DeleteFileAsync(string fullPath, CancellationToken cancellationToken = default)
    {
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);
        await using var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM files WHERE full_path = $full_path;";
        command.Parameters.AddWithValue("$full_path", fullPath);
        await command.ExecuteNonQueryAsync(cancellationToken);
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

        if (!string.IsNullOrWhiteSpace(normalizedQuery))
        {
            where.Add("(file_name LIKE $query OR full_path LIKE $query OR custom_keywords LIKE $query OR categories LIKE $query)");
            command.Parameters.AddWithValue("$query", "%" + EscapeLike(normalizedQuery) + "%");
        }

        AppendFilterSql(where, command, filters);
        command.CommandText = $"""
            SELECT * FROM files
            {(where.Count == 0 ? string.Empty : "WHERE " + string.Join(" AND ", where))}
            ORDER BY modified_utc DESC
            LIMIT $limit;
            """;
        command.Parameters.AddWithValue("$limit", limit);

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
        await using var connection = CreateConnection();
        await connection.OpenAsync(cancellationToken);
        await using var command = connection.CreateCommand();
        command.CommandText = """
            UPDATE files
            SET custom_keywords = $custom_keywords,
                categories = TRIM(categories || '; ' || $custom_keywords, '; ')
            WHERE full_path = $full_path;
            """;
        command.Parameters.AddWithValue("$full_path", fullPath);
        command.Parameters.AddWithValue("$custom_keywords", keywords.Trim());
        await command.ExecuteNonQueryAsync(cancellationToken);
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
        command.Parameters.AddWithValue("$indexed_utc", record.IndexedUtc.UtcDateTime.ToString("O"));
    }

    private static string EscapeLike(string value)
    {
        return value.Replace("[", "[[]").Replace("%", "[%]").Replace("_", "[_]");
    }
}
