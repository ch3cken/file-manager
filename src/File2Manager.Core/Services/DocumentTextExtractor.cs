using System.Text;
using DocumentFormat.OpenXml.Packaging;
using UglyToad.PdfPig;

namespace File2Manager.Core.Services;

public sealed class DocumentTextExtractor
{
    private const int MaxCharacters = 250_000;
    private const long MaxExtractableBytes = 50L * 1024L * 1024L;

    private static readonly HashSet<string> PlainTextExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".txt", ".md", ".csv", ".json", ".xml", ".html", ".htm", ".cs", ".css", ".js", ".ts", ".sql", ".log"
    };

    public bool CanExtract(string extension)
    {
        return PlainTextExtensions.Contains(extension) ||
               string.Equals(extension, ".pdf", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(extension, ".docx", StringComparison.OrdinalIgnoreCase);
    }

    public string ExtractText(string filePath)
    {
        var fileInfo = new FileInfo(filePath);
        if (!fileInfo.Exists || fileInfo.Length > MaxExtractableBytes)
        {
            return string.Empty;
        }

        var extension = fileInfo.Extension.ToLowerInvariant();

        if (PlainTextExtensions.Contains(extension))
        {
            return Limit(ReadPlainText(filePath));
        }

        if (string.Equals(extension, ".pdf", StringComparison.OrdinalIgnoreCase))
        {
            return Limit(ReadPdf(filePath));
        }

        if (string.Equals(extension, ".docx", StringComparison.OrdinalIgnoreCase))
        {
            return Limit(ReadDocx(filePath));
        }

        return string.Empty;
    }

    private static string ReadPlainText(string filePath)
    {
        using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        using var reader = new StreamReader(stream, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
        return reader.ReadToEnd();
    }

    private static string ReadPdf(string filePath)
    {
        var builder = new StringBuilder();
        using var document = PdfDocument.Open(filePath);

        foreach (var page in document.GetPages())
        {
            builder.AppendLine(page.Text);
            if (builder.Length >= MaxCharacters)
            {
                break;
            }
        }

        return builder.ToString();
    }

    private static string ReadDocx(string filePath)
    {
        using var document = WordprocessingDocument.Open(filePath, false);
        return document.MainDocumentPart?.Document?.Body?.InnerText ?? string.Empty;
    }

    private static string Limit(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return string.Empty;
        }

        return value.Length <= MaxCharacters ? value : value[..MaxCharacters];
    }
}
