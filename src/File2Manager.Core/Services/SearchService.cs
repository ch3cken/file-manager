using File2Manager.Core.Models;
using File2Manager.Core.Search;

namespace File2Manager.Core.Services;

public sealed class SearchService
{
    private readonly DatabaseService _databaseService;
    private readonly SearchQueryParser _queryParser;

    public SearchService(DatabaseService databaseService, SearchQueryParser queryParser)
    {
        _databaseService = databaseService;
        _queryParser = queryParser;
    }

    public SearchFilters Parse(string query)
    {
        return _queryParser.Parse(query);
    }

    public async Task<IReadOnlyList<SearchResultItem>> QuickSearchAsync(string query, int limit = 50, CancellationToken cancellationToken = default)
    {
        var filters = _queryParser.Parse(query);
        var records = await _databaseService.SearchQuickAsync(filters.CleanQuery, filters, limit, cancellationToken);

        return records
            .Select(record => ToSearchResult(record, 1, record.Categories))
            .ToArray();
    }

    public async Task<IReadOnlyList<SearchResultItem>> SmartSearchAsync(string query, int limit = 50, CancellationToken cancellationToken = default)
    {
        var filters = _queryParser.Parse(query);
        var candidates = await _databaseService.GetSmartCandidatesAsync(filters, limit: null, cancellationToken);
        var queryTokens = filters.SemanticTerms.Count == 0
            ? TokenUtilities.Tokenize(filters.CleanQuery)
            : filters.SemanticTerms;
        var queryVector = TokenUtilities.TermFrequency(queryTokens);

        var ranked = candidates
            .Select(record => new
            {
                Record = record,
                Score = ScoreRecord(record, queryTokens, queryVector)
            })
            .Where(item => item.Score > 0 || string.IsNullOrWhiteSpace(filters.CleanQuery))
            .OrderByDescending(item => item.Score)
            .ThenByDescending(item => item.Record.ModifiedUtc)
            .Take(limit)
            .Select(item => ToSearchResult(item.Record, item.Score, item.Record.Categories))
            .ToArray();

        return ranked;
    }

    private static double ScoreRecord(FileIndexRecord record, IReadOnlyList<string> queryTokens, IReadOnlyDictionary<string, double> queryVector)
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

        return cosine + exactBoost;
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
