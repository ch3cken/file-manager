using File2Manager.Core.Models;

namespace File2Manager.Core.Services;

public sealed class CategorizationService
{
    private readonly LocalTagGenerator _tagGenerator = new();

    private static readonly HashSet<string> ImageExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".tiff"
    };

    private static readonly HashSet<string> DocumentExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".pdf", ".doc", ".docx", ".txt", ".md", ".rtf"
    };

    private static readonly HashSet<string> SpreadsheetExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".xls", ".xlsx", ".csv"
    };

    private static readonly HashSet<string> PresentationExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".ppt", ".pptx"
    };

    public CategorizationResult Categorize(FileInfo fileInfo, string contentText)
    {
        var extension = fileInfo.Extension.ToLowerInvariant();
        var categories = new List<string>();
        var generatedTags = _tagGenerator.GenerateTags(fileInfo, contentText, maxTags: 18);

        var mediaType = ResolveMediaType(extension);
        var documentType = ResolveDocumentType(extension);
        var subject = generatedTags.FirstOrDefault(tag => tag.Length <= 40) ?? string.Empty;

        categories.Add(mediaType);
        categories.Add(documentType);

        if (fileInfo.LastWriteTime >= DateTime.Now.AddDays(-7))
        {
            categories.Add("Recent");
        }

        categories.AddRange(generatedTags);

        return new CategorizationResult
        {
            Subject = subject,
            DocumentType = documentType,
            MediaType = mediaType,
            Categories = categories.Distinct(StringComparer.OrdinalIgnoreCase).ToArray()
        };
    }

    private static string ResolveMediaType(string extension)
    {
        if (ImageExtensions.Contains(extension))
        {
            return "Image";
        }

        if (DocumentExtensions.Contains(extension))
        {
            return "Document";
        }

        if (SpreadsheetExtensions.Contains(extension))
        {
            return "Spreadsheet";
        }

        if (PresentationExtensions.Contains(extension))
        {
            return "Presentation";
        }

        if (string.Equals(extension, ".zip", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(extension, ".7z", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(extension, ".rar", StringComparison.OrdinalIgnoreCase))
        {
            return "Archive";
        }

        return "Other";
    }

    private static string ResolveDocumentType(string extension)
    {
        if (PresentationExtensions.Contains(extension))
        {
            return "Slides";
        }

        if (SpreadsheetExtensions.Contains(extension))
        {
            return "Spreadsheet";
        }

        if (ImageExtensions.Contains(extension))
        {
            return "Image";
        }

        if (DocumentExtensions.Contains(extension))
        {
            return "Document";
        }

        return "Unknown";
    }
}
