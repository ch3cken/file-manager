using System.Text.RegularExpressions;

namespace File2Manager.Core.Services;

public sealed partial class LocalTagGenerator
{
    private const int MaxContentCharacters = 80_000;

    private static readonly HashSet<string> StopWords = new(StringComparer.OrdinalIgnoreCase)
    {
        "about", "after", "again", "against", "all", "also", "and", "any", "are", "because",
        "been", "before", "being", "between", "both", "but", "can", "chapter", "class",
        "copy", "course", "data", "date", "default", "document", "documents", "download", "downloaded", "downloads",
        "draft", "each", "example", "file", "files", "final", "for", "from", "has", "have",
        "having", "here", "home", "into", "its", "last", "lecture", "make", "more", "most",
        "new", "notes", "only", "other", "page", "part", "project", "report", "same",
        "section", "should", "some", "such", "system", "than", "that", "the", "their",
        "there", "these", "this", "through", "time", "use", "used", "user", "using",
        "version", "was", "week", "were", "when", "where", "which", "while", "will",
        "with", "work", "would", "your"
    };

    public IReadOnlyList<string> GenerateTags(FileInfo fileInfo, string contentText, int maxTags = 10)
    {
        var tags = new List<string>();

        if (!string.IsNullOrWhiteSpace(contentText))
        {
            AddDistinct(tags, RankText(Path.GetFileNameWithoutExtension(fileInfo.Name), 9, 5, fileInfo));
            AddDistinct(tags, RankText(contentText[..Math.Min(contentText.Length, MaxContentCharacters)], 1, 12, fileInfo));
            AddDistinct(tags, RankDirectoryCandidates(fileInfo.DirectoryName, fileInfo));
        }
        else
        {
            AddDistinct(tags, RankText(Path.GetFileNameWithoutExtension(fileInfo.Name), 9, 7, fileInfo));
            AddDistinct(tags, RankDirectoryCandidates(fileInfo.DirectoryName, fileInfo));
        }

        return tags
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(maxTags)
            .ToArray();
    }

    private static IReadOnlyList<string> RankText(string text, double weight, int maxTags, FileInfo fileInfo)
    {
        var candidateScores = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        var displayNames = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        AddWeightedCandidates(candidateScores, displayNames, text, weight);

        return candidateScores
            .Where(pair => pair.Value >= 2.5)
            .OrderByDescending(pair => pair.Value)
            .Select(pair => displayNames[pair.Key])
            .Where(tag => IsUsefulTag(tag, fileInfo))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(maxTags)
            .ToArray();
    }

    private static IReadOnlyList<string> RankDirectoryCandidates(string? directoryName, FileInfo fileInfo)
    {
        if (string.IsNullOrWhiteSpace(directoryName))
        {
            return Array.Empty<string>();
        }

        var candidateScores = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        var displayNames = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var directory = new DirectoryInfo(directoryName);
        var depth = 0;

        while (directory is not null && depth < 3)
        {
            AddWeightedCandidates(candidateScores, displayNames, directory.Name, 4 - depth);
            directory = directory.Parent;
            depth++;
        }

        return candidateScores
            .Where(pair => pair.Value >= 2.5)
            .OrderByDescending(pair => pair.Value)
            .Select(pair => displayNames[pair.Key])
            .Where(tag => IsUsefulTag(tag, fileInfo))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(3)
            .ToArray();
    }

    private static void AddDistinct(List<string> tags, IEnumerable<string> values)
    {
        foreach (var value in values)
        {
            if (!tags.Contains(value, StringComparer.OrdinalIgnoreCase))
            {
                tags.Add(value);
            }
        }
    }

    private static void AddWeightedCandidates(
        Dictionary<string, double> candidateScores,
        Dictionary<string, string> displayNames,
        string text,
        double weight)
    {
        var tokens = ExtractTokens(text);
        if (tokens.Count == 0)
        {
            return;
        }

        foreach (var token in tokens)
        {
            AddCandidate(candidateScores, displayNames, token, weight);
        }

        for (var phraseLength = 2; phraseLength <= 3; phraseLength++)
        {
            for (var index = 0; index <= tokens.Count - phraseLength; index++)
            {
                var phraseTokens = tokens.Skip(index).Take(phraseLength).ToArray();
                if (phraseTokens.Any(token => token.Length < 3) ||
                    phraseTokens.All(token => decimal.TryParse(token, out _)))
                {
                    continue;
                }

                var phrase = string.Join(' ', phraseTokens);
                AddCandidate(candidateScores, displayNames, phrase, weight * (phraseLength + 0.75));
            }
        }
    }

    private static IReadOnlyList<string> ExtractTokens(string text)
    {
        return SplitRegex()
            .Split(text.ToLowerInvariant())
            .Select(token => token.Trim())
            .Where(token => token.Length > 2)
            .Where(token => !StopWords.Contains(token))
            .Where(token => !token.All(char.IsDigit))
            .Where(token => token.Length > 2)
            .Where(token => !StopWords.Contains(token))
            .Where(token => !token.Any(char.IsDigit))
            .ToArray();
    }

    private static void AddCandidate(
        Dictionary<string, double> candidateScores,
        Dictionary<string, string> displayNames,
        string candidate,
        double score)
    {
        var normalized = NormalizeCandidate(candidate);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return;
        }

        candidateScores[normalized] = candidateScores.GetValueOrDefault(normalized) + score;
        displayNames[normalized] = ToDisplayTag(normalized);
    }

    private static bool IsUsefulTag(string tag, FileInfo fileInfo)
    {
        if (tag.Length < 3)
        {
            return false;
        }

        var fileName = Path.GetFileNameWithoutExtension(fileInfo.Name);
        return !string.Equals(tag, fileName, StringComparison.OrdinalIgnoreCase);
    }

    private static string NormalizeCandidate(string candidate)
    {
        return SpaceRegex()
            .Replace(candidate.Trim().ToLowerInvariant(), " ")
            .Trim();
    }

    private static string ToDisplayTag(string candidate)
    {
        var smallWords = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "ai", "api", "cpu", "csv", "db", "gpu", "html", "http", "json", "nlp", "pdf",
            "rag", "sql", "ui", "uml", "url", "xml"
        };

        return string.Join(' ', candidate.Split(' ', StringSplitOptions.RemoveEmptyEntries)
            .Select(token => smallWords.Contains(token)
                ? token.ToUpperInvariant()
                : char.ToUpperInvariant(token[0]) + token[1..]));
    }

    [GeneratedRegex(@"[^\p{L}\p{Nd}]+", RegexOptions.Compiled)]
    private static partial Regex SplitRegex();

    [GeneratedRegex(@"\s+", RegexOptions.Compiled)]
    private static partial Regex SpaceRegex();
}
