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

        var semanticTerms = TokenUtilities.Tokenize(cleanQuery);

        return new SearchFilters
        {
            CleanQuery = cleanQuery,
            Extensions = extensions.ToArray(),
            SemanticTerms = semanticTerms
        };
    }

    private static Regex WordRegex(string word)
    {
        return new Regex(@"\b" + Regex.Escape(word) + @"\b", RegexOptions.Compiled | RegexOptions.IgnoreCase);
    }
}
