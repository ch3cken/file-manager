using File2Manager.Core.Models;
using File2Manager.Core.Search;

namespace File2Manager.Core.Services;

public sealed class SearchService
{
    private const int MaxQuickSearchResults = 20;

    private readonly DatabaseService _databaseService;
    private readonly SearchQueryParser _queryParser;
    private readonly SemanticEmbeddingService _embeddingService;

    public SearchService(
        DatabaseService databaseService,
        SearchQueryParser queryParser,
        SemanticEmbeddingService? embeddingService = null)
    {
        _databaseService = databaseService;
        _queryParser = queryParser;
        _embeddingService = embeddingService ?? new SemanticEmbeddingService();
    }

    public SearchFilters Parse(string query)
    {
        return _queryParser.Parse(query);
    }

    public async Task<IReadOnlyList<SearchResultItem>> QuickSearchAsync(string query, int limit = 50, CancellationToken cancellationToken = default)
    {
        var boundedLimit = Math.Clamp(limit, 1, MaxQuickSearchResults);
        var filters = _queryParser.Parse(query);
        return await QuickSearchAsync(filters, boundedLimit, cancellationToken);
    }

    public async Task<IReadOnlyList<SearchResultItem>> QuickSearchAsync(SearchFilters filters, int limit = 50, CancellationToken cancellationToken = default)
    {
        var boundedLimit = Math.Clamp(limit, 1, MaxQuickSearchResults);
        var records = await _databaseService.SearchQuickAsync(filters.CleanQuery, filters, boundedLimit, cancellationToken);

        return records
            .Select(record => ToSearchResult(record, 1, record.Categories))
            .ToArray();
    }

    public async Task<IReadOnlyList<SearchResultItem>> SmartSearchAsync(string query, int limit = 50, CancellationToken cancellationToken = default)
    {
        var filters = _queryParser.Parse(query);
        return await SmartSearchAsync(filters, limit, cancellationToken);
    }

    public async Task<IReadOnlyList<SearchResultItem>> SmartSearchAsync(SearchFilters filters, int limit = 50, CancellationToken cancellationToken = default)
    {
        var candidates = await _databaseService.GetSmartCandidatesAsync(filters, limit: null, cancellationToken);
        var queryTokens = filters.SemanticTerms.Count == 0
            ? TokenUtilities.Tokenize(filters.CleanQuery)
            : filters.SemanticTerms;
        var queryVector = TokenUtilities.TermFrequency(queryTokens);
        var queryEmbedding = _embeddingService.EmbedQuery(filters.CleanQuery);

        var ranked = candidates
            .Select(record => new
            {
                Record = record,
                Score = ScoreRecord(record, queryTokens, queryVector, queryEmbedding)
            })
            .Where(item => item.Score > 0 || string.IsNullOrWhiteSpace(filters.CleanQuery))
            .OrderByDescending(item => item.Score)
            .ThenByDescending(item => item.Record.ModifiedUtc)
            .Take(limit)
            .Select(item => ToSearchResult(item.Record, item.Score, item.Record.Categories))
            .ToArray();

        return ranked;
    }

    private double ScoreRecord(
        FileIndexRecord record,
        IReadOnlyList<string> queryTokens,
        IReadOnlyDictionary<string, double> queryVector,
        float[]? queryEmbedding)
    {
        if (queryTokens.Count == 0)
        {
            return 0.1;
        }

        var weightedCorpus = string.Join(' ', new[]
        {
            record.FileName,
            record.FileName,
            record.Subject,
            record.Subject,
            record.DocumentType,
            record.MediaType,
            record.Categories,
            record.Categories,
            record.CustomKeywords,
            record.CustomKeywords,
            record.CustomKeywords,
            record.ContentText
        });
        var recordTokens = TokenUtilities.Tokenize(weightedCorpus);
        var recordVector = TokenUtilities.TermFrequency(recordTokens);
        var cosine = TokenUtilities.CosineSimilarity(queryVector, recordVector);

        var exactBoost = queryTokens.Sum(token =>
        {
            var boost = 0.0;
            if (record.FileName.Contains(token, StringComparison.OrdinalIgnoreCase))
            {
                boost += 0.25;
            }

            if (record.CustomKeywords.Contains(token, StringComparison.OrdinalIgnoreCase))
            {
                boost += 0.35;
            }

            if (record.Categories.Contains(token, StringComparison.OrdinalIgnoreCase) ||
                record.Subject.Contains(token, StringComparison.OrdinalIgnoreCase) ||
                record.DocumentType.Contains(token, StringComparison.OrdinalIgnoreCase))
            {
                boost += 0.25;
            }

            return boost;
        });

        var lexicalScore = cosine + exactBoost;
        var semanticScore = ScoreEmbedding(record, queryEmbedding);

        if (semanticScore <= 0)
        {
            return lexicalScore;
        }

        return (semanticScore * 1.25) + (lexicalScore * 0.65);
    }

    private double ScoreEmbedding(FileIndexRecord record, float[]? queryEmbedding)
    {
        if (queryEmbedding is not { Length: > 0 } ||
            record.EmbeddingVector.Length == 0 ||
            !string.Equals(record.EmbeddingModel, SemanticEmbeddingService.CurrentModelName, StringComparison.Ordinal))
        {
            return 0;
        }

        var recordEmbedding = _embeddingService.Deserialize(record.EmbeddingVector);
        var similarity = _embeddingService.CosineSimilarity(queryEmbedding, recordEmbedding);
        return Math.Max(0, similarity);
    }

    private static SearchResultItem ToSearchResult(FileIndexRecord record, double score, string summary)
    {
        return new SearchResultItem
        {
            FileName = record.FileName,
            FullPath = record.FullPath,
            DirectoryPath = record.DirectoryPath,
            Extension = record.Extension,
            ModifiedUtc = record.ModifiedUtc,
            Score = Math.Round(score, 3),
            Summary = summary,
            Tags = string.Join(", ", new[]
            {
                record.Subject,
                record.DocumentType,
                record.MediaType,
                record.Categories,
                record.CustomKeywords
            }.Where(value => !string.IsNullOrWhiteSpace(value)).Distinct(StringComparer.OrdinalIgnoreCase))
        };
    }
}
