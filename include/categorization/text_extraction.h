#pragma once

/* ==========================================================================
 *  SRS Feature Map - Local Text Analysis
 * --------------------------------------------------------------------------
 *  Implements the content-analysis subfeature of Categorization:
 *    REQ-4.2.2.1-C [Automatic Categorization] -> analyze document title/body
 *    REQ-4.2.3.5   [Text Analysis]            -> PDF, DOCX, TXT/plain text
 *    REQ-4.2.3.13  [Exception Handling]       -> unreadable/locked/corrupt files
 *    REQ-2.4.2     [Privacy]                  -> extraction is local-only
 * --------------------------------------------------------------------------
 *  Extraction is best-effort. On failure, exceptionReason lets rules.h mark
 *  an exception or continue with metadata-only categorization.
 * ========================================================================== */
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "file_metadata.h"
#include "text_utils.h"
#include "types.h"

#include <pugixml.hpp>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#include <zip.h>

namespace categorization {
    /// REQ-4.2.3.5 / REQ-4.2.3.13: broad document formats that should get document-aware categorization, even when text extraction is unavailable.
    inline bool isSupportedDocumentExtension(const std::string& extension) {
        return anyOf(normalizeExtension(extension), ".pdf .doc .docx .odt .ott .rtf .txt .md .markdown .tex .pages .epub .mobi .azw .azw3 .djvu");
    }

    /// REQ-4.2.3.5: text-like formats that can be safely read directly as local text for content analysis.
    inline bool isLocallyTextExtractableExtension(const std::string& extension) {
        return anyOf(normalizeExtension(extension), R"(.txt .md .markdown .tex .rtf .csv .tsv .log .json .jsonl .yaml .yml .toml .ini .cfg .conf .sql .xml .html .htm .css .scss .sass .less .c .cc .cpp .cxx .h .hh .hpp .hxx .py .ipynb .js .jsx .ts .tsx .java .kt .kts .cs .go .rs .rb .php .swift .scala .r .sh .bash .zsh .ps1 .bat .cmd)");
    }

    /// REQ-4.2.3.13: binary-content guard for unsupported or mislabelled files.
    inline bool looksBinary(const std::string& data) {
        if (data.empty())
            return false;
        return static_cast<std::size_t>(std::count(data.begin(), data.end(), '\0')) > std::max<std::size_t>(1, data.size() / 100);
    }

    /// REQ-4.2.3.5: remove NUL/control bytes while preserving meaningful whitespace for rule matching.
    inline std::string sanitizeExtractedText(std::string text) {
        std::string out;
        out.reserve(text.size());
        for (unsigned char c : text)
            if (c != '\0')
                out.push_back((c == '\n' || c == '\r' || c == '\t' || c >= 32) ? static_cast<char>(c) : ' ');
        return out;
    }

    /// REQ-4.2.3.5: cap extraction so large files do not dominate preprocessing.
    inline void appendLimited(std::string& out, std::string_view text, std::size_t maxBytes) {
        if (maxBytes == 0 || out.size() >= maxBytes || text.empty())
            return;
        out.append(text.data(), std::min(maxBytes - out.size(), text.size()));
    }

    /// REQ-4.2.3.5: store sanitized content and derive whether usable text was actually found.
    inline void setExtractedText(TextExtractionResult& result, std::string text) {
        result.text = sanitizeExtractedText(std::move(text));
        result.success = !trim(result.text).empty();
    }

    /// REQ-4.2.3.5: read enough DOCX XML to produce maxTextBytes of visible text without loading unusually large archives into memory.
    inline std::size_t docxXmlReadLimit(std::size_t maxTextBytes) {
        constexpr std::size_t MinXmlBytes = 64 * 1024, MaxXmlBytes = 16 * 1024 * 1024;
        if (maxTextBytes == 0)
            return 0;
        return maxTextBytes > MaxXmlBytes / 16 ? MaxXmlBytes : std::max(MinXmlBytes, maxTextBytes * 16);
    }

    /// REQ-4.2.3.5: match DOCX XML nodes by local name, ignoring namespace prefixes emitted by Word.
    inline bool docxNodeNameIs(const pugi::xml_node& node, const char* expectedLocalName) {
        const char* name = node.name(), *local = std::strchr(name, ':');
        return std::strcmp(local == nullptr ? name : local + 1, expectedLocalName) == 0;
    }

    /// REQ-4.2.3.5: recursively collect visible DOCX body text while preserving tabs, breaks, and paragraph boundaries.
    inline void appendDocxNodeText(const pugi::xml_node& node, std::string& out, std::size_t maxBytes) {
        if (out.size() >= maxBytes)
            return;
        if (docxNodeNameIs(node, "t")) {
            appendLimited(out, node.child_value(), maxBytes);
            return;
        }
        if (docxNodeNameIs(node, "tab")) {
            appendLimited(out, "\t", maxBytes);
            return;
        }
        if (docxNodeNameIs(node, "br") || docxNodeNameIs(node, "cr")) {
            appendLimited(out, "\n", maxBytes);
            return;
        }
        for (const auto& child : node.children())
            appendDocxNodeText(child, out, maxBytes);
        if (docxNodeNameIs(node, "p"))
            appendLimited(out, "\n", maxBytes);
    }

    /// REQ-4.2.3.5: extract readable text from the main DOCX document XML payload.
    inline std::string extractDocxDocumentText(const std::string& xml, std::size_t maxBytes) {
        pugi::xml_document document;
        if (!document.load_buffer(xml.data(), xml.size()))
            return {};
        std::string out;
        appendDocxNodeText(document.document_element(), out, maxBytes);
        return out;
    }

    /// REQ-4.2.3.5 / REQ-4.2.3.13: extract UTF-8 page text from a PDF and report invalid/locked extraction failures.
    inline TextExtractionResult extractPdfTextFromFile(const std::string& path, std::size_t maxBytes = 64 * 1024) {
        TextExtractionResult result;
        result.attempted = true;
        if (maxBytes == 0)
            return result;
        try {
            std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(path));
            if (!doc) {
                result.exceptionReason = "invalid pdf";
                return result;
            }
            if (doc->is_locked()) {
                result.exceptionReason = "locked pdf";
                return result;
            }
            std::string extracted;
            for (int i = 0; i < doc->pages() && extracted.size() < maxBytes; ++i) {
                std::unique_ptr<poppler::page> page(doc->create_page(i));
                if (!page)
                    continue;
                const poppler::byte_array utf8 = page->text().to_utf8();
                appendLimited(extracted, std::string(utf8.begin(), utf8.end()), maxBytes);
                appendLimited(extracted, "\n", maxBytes);
            }
            setExtractedText(result, std::move(extracted));
            result.bytesRead = result.text.size();
        } catch (...) {
            result.exceptionReason = "pdf extraction failed";
        }
        return result;
    }

    /// REQ-4.2.3.5 / REQ-4.2.3.13: extract the main document text from a DOCX zip archive and mark unreadable/corrupt archives.
    inline TextExtractionResult extractDocxTextFromFile(const std::string& path, std::size_t maxBytes = 64 * 1024) {
        TextExtractionResult result;
        result.attempted = true;
        if (maxBytes == 0)
            return result;

        int zipError = 0;
        std::unique_ptr<zip_t, decltype(&zip_discard)> archive(zip_open(path.c_str(), ZIP_RDONLY, &zipError), zip_discard);
        if (!archive) {
            result.exceptionReason = "unreadable docx";
            return result;
        }

        const char* xmlPath = "word/document.xml";
        zip_stat_t stat{};
        zip_stat_init(&stat);
        if (zip_stat(archive.get(), xmlPath, ZIP_FL_ENC_GUESS, &stat) != 0) {
            result.exceptionReason = "invalid docx";
            return result;
        }

        std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(zip_fopen(archive.get(), xmlPath, ZIP_FL_ENC_GUESS), zip_fclose);
        if (!file) {
            result.exceptionReason = "unreadable docx";
            return result;
        }

        const std::size_t readLimit = docxXmlReadLimit(maxBytes);
        std::string xml;
        if ((stat.valid & ZIP_STAT_SIZE) != 0)
            xml.reserve(static_cast<std::size_t>(std::min<zip_uint64_t>(stat.size, readLimit)));

        char buffer[4096] = {};
        while (xml.size() < readLimit) {
            const zip_int64_t bytesRead = zip_fread(file.get(), buffer, std::min(sizeof(buffer), readLimit - xml.size()));
            if (bytesRead < 0) {
                result.exceptionReason = "docx extraction failed";
                return result;
            }
            if (bytesRead == 0)
                break;
            xml.append(buffer, static_cast<std::size_t>(bytesRead));
        }

        result.bytesRead = xml.size();
        setExtractedText(result, extractDocxDocumentText(xml, maxBytes));
        if (!result.success && result.exceptionReason.empty())
            result.exceptionReason = "docx text not found";
        return result;
    }

    /// REQ-4.2.3.5 / REQ-4.2.3.13: read TXT/plain-text-like files directly with a binary-content guard.
    inline TextExtractionResult extractPlainTextFromFile(const std::string& path, std::size_t maxBytes = 64 * 1024) {
        TextExtractionResult result;
        result.attempted = true;
        if (maxBytes == 0)
            return result;
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            result.exceptionReason = "unreadable file";
            return result;
        }
        std::string raw(maxBytes, '\0');
        input.read(&raw[0], static_cast<std::streamsize>(raw.size()));
        raw.resize(static_cast<std::size_t>(input.gcount()));
        result.bytesRead = raw.size();
        if (looksBinary(raw)) {
            result.exceptionReason = "binary content";
            return result;
        }
        setExtractedText(result, std::move(raw));
        return result;
    }

    /// REQ-4.2.2.1-C / REQ-4.2.3.5: dispatch to the local extractor for the detected document type.
    inline TextExtractionResult extractTextFromFile(const std::string& path, std::size_t maxBytes = 64 * 1024) {
        const std::string ext = extensionFromPath(path);
        if (ext == ".pdf")
            return extractPdfTextFromFile(path, maxBytes);
        if (ext == ".docx")
            return extractDocxTextFromFile(path, maxBytes);
        if (isLocallyTextExtractableExtension(ext))
            return extractPlainTextFromFile(path, maxBytes);
        return {};
    }
}
