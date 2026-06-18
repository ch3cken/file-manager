using System.Text.RegularExpressions;
using Microsoft.ML.OnnxRuntime;
using Microsoft.ML.OnnxRuntime.Tensors;
using File2Manager.Core.Models;

namespace File2Manager.Core.Services;

public sealed partial class SemanticEmbeddingService : IDisposable
{
    public const string CurrentModelName = "all-MiniLM-L6-v2-quantized";

    private const int MaxWordPieceInputChars = 100;
    private const int MaxSequenceTokens = 256;
    private const int MaxContentCharacters = 12_000;

    private readonly string? _assetDirectory;
    private readonly object _loadLock = new();
    private readonly SemaphoreSlim _runLock = new(Math.Clamp(Environment.ProcessorCount / 2, 1, 4));
    private InferenceSession? _session;
    private Dictionary<string, int>? _vocabulary;
    private bool _loadAttempted;
    private bool _disposed;

    public SemanticEmbeddingService(string? assetDirectory = null)
    {
        _assetDirectory = string.IsNullOrWhiteSpace(assetDirectory)
            ? null
            : Path.GetFullPath(assetDirectory);
    }

    public bool IsAvailable => TryEnsureLoaded();

    public float[]? EmbedRecord(FileIndexRecord record)
    {
        return EmbedText(BuildRecordEmbeddingText(record));
    }

    public float[]? EmbedQuery(string query)
    {
        return EmbedText(query);
    }

    public byte[] Serialize(float[] vector)
    {
        var bytes = new byte[vector.Length * sizeof(float)];
        Buffer.BlockCopy(vector, 0, bytes, 0, bytes.Length);
        return bytes;
    }

    public float[] Deserialize(byte[] bytes)
    {
        if (bytes.Length == 0 || bytes.Length % sizeof(float) != 0)
        {
            return Array.Empty<float>();
        }

        var vector = new float[bytes.Length / sizeof(float)];
        Buffer.BlockCopy(bytes, 0, vector, 0, bytes.Length);
        return vector;
    }

    public double CosineSimilarity(float[] left, float[] right)
    {
        if (left.Length == 0 || right.Length == 0 || left.Length != right.Length)
        {
            return 0;
        }

        var dotProduct = 0.0;
        var leftMagnitude = 0.0;
        var rightMagnitude = 0.0;

        for (var index = 0; index < left.Length; index++)
        {
            dotProduct += left[index] * right[index];
            leftMagnitude += left[index] * left[index];
            rightMagnitude += right[index] * right[index];
        }

        return leftMagnitude == 0 || rightMagnitude == 0
            ? 0
            : dotProduct / (Math.Sqrt(leftMagnitude) * Math.Sqrt(rightMagnitude));
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _session?.Dispose();
        _runLock.Dispose();
        _disposed = true;
    }

    private float[]? EmbedText(string text)
    {
        if (string.IsNullOrWhiteSpace(text) || !TryEnsureLoaded() || _session is null || _vocabulary is null)
        {
            return null;
        }

        var ids = Tokenize(text, _vocabulary);
        if (ids.Count == 0)
        {
            return null;
        }

        var sequenceLength = ids.Count;
        var inputIds = new DenseTensor<long>(new[] { 1, sequenceLength });
        var attentionMask = new DenseTensor<long>(new[] { 1, sequenceLength });
        var tokenTypeIds = new DenseTensor<long>(new[] { 1, sequenceLength });

        for (var index = 0; index < sequenceLength; index++)
        {
            inputIds[0, index] = ids[index];
            attentionMask[0, index] = 1;
            tokenTypeIds[0, index] = 0;
        }

        _runLock.Wait();
        try
        {
            using var results = _session.Run(new[]
            {
                NamedOnnxValue.CreateFromTensor("input_ids", inputIds),
                NamedOnnxValue.CreateFromTensor("attention_mask", attentionMask),
                NamedOnnxValue.CreateFromTensor("token_type_ids", tokenTypeIds)
            });

            var output = results.First(result => result.Name == "last_hidden_state").AsTensor<float>();
            return MeanPoolAndNormalize(output, attentionMask, sequenceLength);
        }
        catch
        {
            return null;
        }
        finally
        {
            _runLock.Release();
        }
    }

    private bool TryEnsureLoaded()
    {
        if (_session is not null && _vocabulary is not null)
        {
            return true;
        }

        lock (_loadLock)
        {
            if (_session is not null && _vocabulary is not null)
            {
                return true;
            }

            if (_loadAttempted)
            {
                return false;
            }

            _loadAttempted = true;

            try
            {
                var assetsDirectory = _assetDirectory ?? Path.Combine(AppContext.BaseDirectory, "Embeddings");
                var modelPath = Path.Combine(assetsDirectory, "all-MiniLM-L6-v2-quantized.onnx");
                var vocabularyPath = Path.Combine(assetsDirectory, "vocab.txt");

                if (!File.Exists(modelPath) || !File.Exists(vocabularyPath))
                {
                    return false;
                }

                _vocabulary = LoadVocabulary(vocabularyPath);
                _session = new InferenceSession(modelPath, new SessionOptions
                {
                    IntraOpNumThreads = Math.Clamp(Environment.ProcessorCount / 2, 1, 4)
                });
                return true;
            }
            catch
            {
                _session?.Dispose();
                _session = null;
                _vocabulary = null;
                return false;
            }
        }
    }

    private static Dictionary<string, int> LoadVocabulary(string vocabularyPath)
    {
        var vocabulary = new Dictionary<string, int>(StringComparer.Ordinal);
        var index = 0;

        foreach (var token in File.ReadLines(vocabularyPath))
        {
            if (!vocabulary.ContainsKey(token))
            {
                vocabulary[token] = index;
            }

            index++;
        }

        return vocabulary;
    }

    private static IReadOnlyList<long> Tokenize(string text, IReadOnlyDictionary<string, int> vocabulary)
    {
        var tokenIds = new List<long>(MaxSequenceTokens) { vocabulary["[CLS]"] };
        var unk = vocabulary["[UNK]"];
        var sep = vocabulary["[SEP]"];

        foreach (var token in BasicTokenRegex().Matches(text.ToLowerInvariant()).Select(match => match.Value))
        {
            foreach (var id in WordPieceTokenize(token, vocabulary, unk))
            {
                if (tokenIds.Count >= MaxSequenceTokens - 1)
                {
                    tokenIds.Add(sep);
                    return tokenIds;
                }

                tokenIds.Add(id);
            }
        }

        tokenIds.Add(sep);
        return tokenIds;
    }

    private static IEnumerable<long> WordPieceTokenize(string token, IReadOnlyDictionary<string, int> vocabulary, int unknownTokenId)
    {
        if (token.Length > MaxWordPieceInputChars)
        {
            yield return unknownTokenId;
            yield break;
        }

        var start = 0;
        while (start < token.Length)
        {
            var end = token.Length;
            var currentSubToken = string.Empty;
            var currentTokenId = unknownTokenId;

            while (start < end)
            {
                var candidate = token[start..end];
                if (start > 0)
                {
                    candidate = "##" + candidate;
                }

                if (vocabulary.TryGetValue(candidate, out currentTokenId))
                {
                    currentSubToken = candidate;
                    break;
                }

                end--;
            }

            if (string.IsNullOrEmpty(currentSubToken))
            {
                yield return unknownTokenId;
                yield break;
            }

            yield return currentTokenId;
            start = end;
        }
    }

    private static float[] MeanPoolAndNormalize(Tensor<float> output, Tensor<long> attentionMask, int sequenceLength)
    {
        var dimensions = output.Dimensions.ToArray();
        var hiddenSize = dimensions.Length >= 3 ? dimensions[2] : 0;
        if (hiddenSize <= 0)
        {
            return Array.Empty<float>();
        }

        var pooled = new float[hiddenSize];
        var tokenCount = 0;

        for (var tokenIndex = 0; tokenIndex < sequenceLength; tokenIndex++)
        {
            if (attentionMask[0, tokenIndex] == 0)
            {
                continue;
            }

            tokenCount++;
            for (var dimension = 0; dimension < hiddenSize; dimension++)
            {
                pooled[dimension] += output[0, tokenIndex, dimension];
            }
        }

        if (tokenCount == 0)
        {
            return Array.Empty<float>();
        }

        var magnitude = 0.0;
        for (var dimension = 0; dimension < hiddenSize; dimension++)
        {
            pooled[dimension] /= tokenCount;
            magnitude += pooled[dimension] * pooled[dimension];
        }

        if (magnitude <= 0)
        {
            return pooled;
        }

        var scale = 1.0 / Math.Sqrt(magnitude);
        for (var dimension = 0; dimension < hiddenSize; dimension++)
        {
            pooled[dimension] = (float)(pooled[dimension] * scale);
        }

        return pooled;
    }

    private static string BuildRecordEmbeddingText(FileIndexRecord record)
    {
        var contentText = record.ContentText.Length > MaxContentCharacters
            ? record.ContentText[..MaxContentCharacters]
            : record.ContentText;

        return string.Join("\n", new[]
        {
            record.FileName,
            record.Subject,
            record.DocumentType,
            record.MediaType,
            record.Categories,
            record.CustomKeywords,
            contentText
        }.Where(value => !string.IsNullOrWhiteSpace(value)));
    }

    [GeneratedRegex(@"[\p{L}\p{Nd}]+", RegexOptions.Compiled)]
    private static partial Regex BasicTokenRegex();
}
