using System.Text.RegularExpressions;

namespace File2Manager.Core.Search;

public static partial class TokenUtilities
{
    private static readonly HashSet<string> StopWords = new(StringComparer.OrdinalIgnoreCase)
    {
        "a", "an", "and", "are", "as", "at", "be", "by", "for", "from", "i", "in", "is",
        "it", "last", "me", "my", "of", "on", "or", "paper", "show", "that", "the", "this",
        "to", "was", "week", "with", "yesterday", "today", "ago", "downloaded", "file", "files"
    };

    public static IReadOnlyList<string> Tokenize(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return Array.Empty<string>();
        }

        return SplitRegex()
            .Split(text.ToLowerInvariant())
            .Select(token => token.Trim())
            .Where(token => token.Length > 1 && !StopWords.Contains(token))
            .Select(Stem)
            .Where(token => token.Length > 1)
            .ToArray();
    }

    public static Dictionary<string, double> TermFrequency(IEnumerable<string> tokens)
    {
        var frequency = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);

        foreach (var token in tokens)
        {
            frequency[token] = frequency.GetValueOrDefault(token) + 1;
        }

        return frequency;
    }

    public static double CosineSimilarity(IReadOnlyDictionary<string, double> left, IReadOnlyDictionary<string, double> right)
    {
        if (left.Count == 0 || right.Count == 0)
        {
            return 0;
        }

        var dotProduct = 0.0;
        foreach (var pair in left)
        {
            if (right.TryGetValue(pair.Key, out var rightValue))
            {
                dotProduct += pair.Value * rightValue;
            }
        }

        var leftMagnitude = Math.Sqrt(left.Values.Sum(value => value * value));
        var rightMagnitude = Math.Sqrt(right.Values.Sum(value => value * value));

        return leftMagnitude == 0 || rightMagnitude == 0
            ? 0
            : dotProduct / (leftMagnitude * rightMagnitude);
    }

    private static string Stem(string token)
    {
        if (token.Length > 5 && token.EndsWith("ing", StringComparison.OrdinalIgnoreCase))
        {
            return token[..^3];
        }

        if (token.Length > 4 && token.EndsWith("ed", StringComparison.OrdinalIgnoreCase))
        {
            return token[..^2];
        }

        if (token.Length > 3 && token.EndsWith("s", StringComparison.OrdinalIgnoreCase))
        {
            return token[..^1];
        }

        return token;
    }

    [GeneratedRegex(@"[^\p{L}\p{Nd}]+", RegexOptions.Compiled)]
    private static partial Regex SplitRegex();
}
