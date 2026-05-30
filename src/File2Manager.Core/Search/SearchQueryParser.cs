using System.Text.RegularExpressions;
using File2Manager.Core.Models;

namespace File2Manager.Core.Search;

public sealed partial class SearchQueryParser
{
    private static readonly IReadOnlyDictionary<string, string[]> FileTypeExtensions = new Dictionary<string, string[]>(StringComparer.OrdinalIgnoreCase)
    {
        ["paper"] = new[] { ".pdf" },
        ["thesis"] = new[] { ".pdf", ".docx" },
        ["article"] = new[] { ".pdf", ".docx" },
        ["report"] = new[] { ".pdf", ".docx", ".pptx" },
        ["pdf"] = new[] { ".pdf" },
        ["document"] = new[] { ".docx", ".doc", ".pdf", ".txt", ".md" },
        ["doc"] = new[] { ".docx", ".doc" },
        ["text"] = new[] { ".txt", ".md" },
        ["photo"] = new[] { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp" },
        ["picture"] = new[] { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp" },
        ["image"] = new[] { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp" },
        ["slide"] = new[] { ".ppt", ".pptx", ".pdf" },
        ["slides"] = new[] { ".ppt", ".pptx", ".pdf" },
        ["presentation"] = new[] { ".ppt", ".pptx", ".pdf" },
        ["spreadsheet"] = new[] { ".xls", ".xlsx", ".csv" },
        ["excel"] = new[] { ".xls", ".xlsx", ".csv" },
        ["code"] = new[] { ".cs", ".cpp", ".c", ".h", ".java", ".js", ".ts", ".py", ".html", ".css", ".json", ".xml" },
        ["archive"] = new[] { ".zip", ".7z", ".rar" }
    };

    public SearchFilters Parse(string query)
    {
        var cleanQuery = query.Trim();
        var loweredQuery = cleanQuery.ToLowerInvariant();
        var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var pair in FileTypeExtensions)
        {
            if (WordRegex(pair.Key).IsMatch(loweredQuery))
            {
                foreach (var extension in pair.Value)
                {
                    extensions.Add(extension);
                }
            }
        }

        var (modifiedFromUtc, modifiedToUtc) = ParseTimeCondition(loweredQuery);
        var semanticTerms = TokenUtilities.Tokenize(cleanQuery);

        return new SearchFilters
        {
            CleanQuery = cleanQuery,
            Extensions = extensions.ToArray(),
            ModifiedFromUtc = modifiedFromUtc,
            ModifiedToUtc = modifiedToUtc,
            SemanticTerms = semanticTerms
        };
    }

    private static (DateTimeOffset? ModifiedFromUtc, DateTimeOffset? ModifiedToUtc) ParseTimeCondition(string loweredQuery)
    {
        var now = DateTimeOffset.Now;
        var today = new DateTimeOffset(now.Year, now.Month, now.Day, 0, 0, 0, now.Offset);

        if (loweredQuery.Contains("yesterday", StringComparison.OrdinalIgnoreCase))
        {
            return (today.AddDays(-1).ToUniversalTime(), today.ToUniversalTime());
        }

        if (loweredQuery.Contains("today", StringComparison.OrdinalIgnoreCase))
        {
            return (today.ToUniversalTime(), today.AddDays(1).ToUniversalTime());
        }

        if (loweredQuery.Contains("this week", StringComparison.OrdinalIgnoreCase))
        {
            var daysSinceMonday = ((int)today.DayOfWeek + 6) % 7;
            var weekStart = today.AddDays(-daysSinceMonday);
            return (weekStart.ToUniversalTime(), weekStart.AddDays(7).ToUniversalTime());
        }

        if (loweredQuery.Contains("last week", StringComparison.OrdinalIgnoreCase))
        {
            return (now.AddDays(-7).ToUniversalTime(), now.ToUniversalTime());
        }

        if (loweredQuery.Contains("this month", StringComparison.OrdinalIgnoreCase))
        {
            var monthStart = new DateTimeOffset(now.Year, now.Month, 1, 0, 0, 0, now.Offset);
            return (monthStart.ToUniversalTime(), monthStart.AddMonths(1).ToUniversalTime());
        }

        if (loweredQuery.Contains("last month", StringComparison.OrdinalIgnoreCase))
        {
            var monthStart = new DateTimeOffset(now.Year, now.Month, 1, 0, 0, 0, now.Offset);
            return (monthStart.AddMonths(-1).ToUniversalTime(), monthStart.ToUniversalTime());
        }

        var agoMatch = AgoRegex().Match(loweredQuery);
        if (agoMatch.Success)
        {
            var amount = int.Parse(agoMatch.Groups["amount"].Value);
            var unit = agoMatch.Groups["unit"].Value;
            var from = unit.StartsWith("month", StringComparison.OrdinalIgnoreCase)
                ? now.AddMonths(-amount)
                : unit.StartsWith("week", StringComparison.OrdinalIgnoreCase)
                    ? now.AddDays(-amount * 7)
                    : now.AddDays(-amount);

            return (from.ToUniversalTime(), now.ToUniversalTime());
        }

        if (loweredQuery.Contains("recent", StringComparison.OrdinalIgnoreCase))
        {
            return (now.AddDays(-14).ToUniversalTime(), now.ToUniversalTime());
        }

        return (null, null);
    }

    [GeneratedRegex(@"(?<amount>\d+)\s*(?<unit>day|days|week|weeks|month|months)\s+ago", RegexOptions.Compiled)]
    private static partial Regex AgoRegex();

    private static Regex WordRegex(string word)
    {
        return new Regex(@"\b" + Regex.Escape(word) + @"\b", RegexOptions.Compiled | RegexOptions.IgnoreCase);
    }
}
