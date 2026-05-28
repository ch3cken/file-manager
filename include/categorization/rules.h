#pragma once

/* ==========================================================================
 *  SRS Feature Map - Rule-Based Category Inference
 * --------------------------------------------------------------------------
 *  Implements the classifier required by SRS 4.2 Categorization:
 *    REQ-4.2.3.4  [Primary Categorization]              -> extension/path/name
 *    REQ-4.2.3.6  [Categorization Metadata Generation]  -> subject/type/media
 *    REQ-4.2.3.9  [Keyword Reflection]                  -> keyword tags
 *    REQ-4.2.3.10 [Categorization Update]               -> recategorize inputs
 *    REQ-4.2.3.11 [Search Reflection]                   -> searchable tags
 *    REQ-4.2.3.13 [Exception Handling]                  -> metadata_only/status
 * --------------------------------------------------------------------------
 *  The generated tags bridge automatic categorization and Smart Search queries
 *  such as "machine learning paper" or "assignment yesterday".
 * ========================================================================== */
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "collection_utils.h"
#include "file_metadata.h"
#include "text_extraction.h"
#include "text_utils.h"
#include "types.h"

namespace categorization {
    namespace detail {
        /// REQ-4.2.3.3: detects file-like objects with a fileSize member.
        template <typename, typename = void> inline constexpr bool HasFileSize = false;
        template <typename T>
        inline constexpr bool HasFileSize<T, std::void_t<decltype(std::declval<T&>().fileSize)>> = true;

        /// REQ-4.2.3.4 / REQ-4.2.3.6: compact row for rule-based classifiers.
        struct Rule { const char* value; const char* terms; const char* extensions = ""; };
    }

    /* ------------------------------------------------------------------
     * Rule matching primitives - shared by subject, media, and document
     * type inference for REQ-4.2.3.4 and REQ-4.2.3.6.
     * ---------------------------------------------------------------- */

    /// REQ-4.2.3.4: return the first rule whose term list or extension list matches the normalized file evidence.
    template <std::size_t N>
    inline std::string firstMatch(const std::string& text, const detail::Rule (&rules)[N], const char* fallback, const std::string& ext = {}) {
        for (const auto& rule : rules)
            if (containsAny(text, rule.terms) || anyOf(ext, rule.extensions))
                return rule.value;
        return fallback;
    }

    /// REQ-4.2.3.4 / REQ-4.2.3.6: classify broad media family from extension.
    inline std::string mediaType(const std::string& extension) {
        static constexpr detail::Rule rules[] = {
            {"document",    "", ".pdf .doc .docx .odt .ott .rtf .txt .md .markdown .tex .pages .ppt .pptx .odp .key .xls .xlsx .ods .numbers .csv .tsv"},
            {"ebook",       "", ".epub .mobi .azw .azw3 .fb2 .djvu"},
            {"image",       "", ".jpg .jpeg .png .gif .bmp .tif .tiff .svg .webp .heic .heif .ico .psd .ai .eps .raw .cr2 .nef .orf .arw .dng"},
            {"video",       "", ".mp4 .m4v .mov .avi .mkv .webm .wmv .flv .mpeg .mpg .3gp .mts .m2ts"},
            {"audio",       "", ".mp3 .wav .flac .aac .ogg .oga .opus .m4a .wma .aiff .alac .mid .midi"},
            {"disk image",  "", ".iso .dmg .img .vhd .vhdx .vmdk .qcow2"},
            {"archive",     "", ".zip .rar .7z .tar .gz .tgz .bz2 .xz .zst .lz .lzma .cab"},
            {"code",        "", ".c .cc .cpp .cxx .h .hh .hpp .hxx .py .ipynb .js .jsx .ts .tsx .java .kt .kts .cs .go .rs .rb .php .swift .scala .r .m .mm .sh .bash .zsh .ps1 .bat .cmd .sql .html .htm .css .scss .sass .less .vue .svelte .xml .xaml"},
            {"data",        "", ".json .jsonl .yaml .yml .toml .ini .cfg .conf .log .parquet .avro .orc .feather .h5 .hdf5 .mat .npy .npz .pkl"},
            {"database",    "", ".db .sqlite .sqlite3 .mdb .accdb .sqlitedb"},
            {"executable",  "", ".exe .msi .app .apk .ipa .deb .rpm .bin .run .dll .so .dylib .sys"},
            {"font",        "", ".ttf .otf .woff .woff2 .eot"},
            {"model",       "", ".obj .fbx .stl .dae .gltf .glb .blend .3ds .ply .step .stp .iges .igs .dwg .dxf"},
            {"certificate", "", ".pem .crt .cer .der .pfx .p12 .key .pub"},
            {"shortcut",    "", ".lnk .url .webloc .desktop"},
        };
        return firstMatch(" ", rules, "other", extension);
    }

    /// REQ-4.2.3.6: narrow document files into subtypes using filename/path/text evidence and known extensions.
    inline std::string documentType(const std::string& text, const std::string& extension, const std::string& media) {
        if (media != "document")
            return media;
        static constexpr detail::Rule rules[] = {
            {"presentation",    "slide|slides|presentation",                                     ".ppt .pptx .odp .key"},
            {"spreadsheet",     "spreadsheet|dataset",                                           ".xls .xlsx .ods .numbers .csv .tsv"},
            {"syllabus",        "syllabus|course outline|course schedule|class schedule"},
            {"exam",            "exam|midterm|final exam|practice exam|mock exam|assessment"},
            {"quiz",            "quiz"},
            {"solution",        "solution|solutions|answer key|worked solution|marking scheme"},
            {"rubric",          "rubric|grading criteria|evaluation criteria"},
            {"assignment",      "assignment|homework|problem set|pset"},
            {"lecture material","lecture|course|class|recitation"},
            {"textbook",        "textbook|book|chapter|reader"},
            {"thesis",          "thesis|dissertation"},
            {"paper",           "paper|journal|arxiv|proceedings|preprint|publication"},
            {"article",         "article|essay|whitepaper|case study"},
            {"proposal",        "proposal|pitch"},
            {"specification",   "srs|requirement|requirements|specification|design document|architecture document"},
            {"report",          "report|analysis|summary|review|retrospective"},
            {"meeting record",  "meeting|minutes|agenda|action items"},
            {"manual",          "manual|guide|tutorial|documentation|docs|readme|handbook|how to"},
            {"form",            "form|application|questionnaire|survey|checklist"},
            {"legal document",  "contract|agreement|nda|license|policy|terms"},
            {"financial record","receipt|invoice|statement|bill|quote|purchase order|budget"},
            {"career document", "resume|cv|cover letter|portfolio"},
            {"letter",          "letter|memo|notice|announcement"},
            {"note",            "note|notes|memo|draft",                                         ".txt .md .markdown .rtf .tex"},
        };
        return firstMatch(text, rules, "document", extension);
    }

    /// REQ-4.2.3.6: infer subject area from normalized filename, path, and extracted document text.
    inline std::string subjectOf(const std::string& text) {
        static constexpr detail::Rule rules[] = {
            {"machine learning",            "machine learning|deep learning|neural network|artificial intelligence|ai"},
            {"data science",                "data science|data mining|data analysis|analytics|visualization|pandas|numpy|jupyter|dataset"},
            {"natural language processing", "natural language processing|nlp|language model|llm|tokenizer|embedding|transformer"},
            {"computer vision",             "computer vision|image processing|object detection|segmentation|opencv"},
            {"robotics",                    "robotics|robot|ros|slam|autonomous|control system"},
            {"operating systems",           "operating system|operating systems|os"},
            {"distributed systems",         "distributed system|distributed systems|consensus|raft|paxos|replication|fault tolerance"},
            {"cloud computing",             "cloud|aws|azure|gcp|kubernetes|docker|container|serverless|devops"},
            {"web development",             "web development|frontend|backend|full stack|react|vue|svelte|node|express|django|flask"},
            {"mobile development",          "mobile|android|ios|swift|kotlin|flutter|react native"},
            {"software engineering",        "software engineering|srs|requirements|uml|agile"},
            {"database",                    "database|sql|sqlite|transaction"},
            {"algorithms",                  "algorithm|data structure|graph|dynamic programming"},
            {"computer networks",           "network|protocol|tcp|udp|http"},
            {"programming languages",       "compiler|programming language|parser"},
            {"computer graphics",           "computer graphics|rendering|shader|opengl|vulkan|ray tracing|animation"},
            {"security",                    "security|cryptography|malware|privacy"},
            {"mathematics",                 "calculus|algebra|geometry|statistics|probability|math|equation|linear algebra|discrete math|optimization"},
            {"physics",                     "physics|mechanics|electromagnetism|thermodynamics|quantum|relativity"},
            {"chemistry",                   "chemistry|organic chemistry|inorganic chemistry|biochemistry|molecule|reaction"},
            {"biology",                     "biology|genetics|cell|molecular biology|ecology|evolution"},
            {"medicine",                    "medicine|medical|clinical|diagnosis|patient|hospital|prescription"},
            {"psychology",                  "psychology|cognitive|behavior|behaviour|mental health|therapy"},
            {"education",                   "education|pedagogy|teaching|learning|curriculum|classroom"},
            {"engineering",                 "engineering|mechanical|electrical|civil|aerospace|materials|manufacturing"},
            {"finance",                     "finance|invoice|receipt|budget|tax|bank|payment"},
            {"accounting",                  "accounting|ledger|balance sheet|cash flow|bookkeeping|audit"},
            {"economics",                   "economics|microeconomics|macroeconomics|market|inflation|gdp"},
            {"business",                    "business|strategy|startup|entrepreneurship|operations|management"},
            {"marketing",                   "marketing|sales|customer|campaign|seo|brand strategy|growth"},
            {"legal",                       "contract|legal|agreement|policy|license"},
            {"career",                      "resume|cv|career|recruit|interview|hr"},
            {"work",                        "meeting|minutes|agenda|planning|roadmap"},
            {"design",                      "design|mockup|wireframe|ui|ux|brand|logo"},
            {"architecture",                "architecture|floor plan|building|interior|urban planning"},
            {"history",                     "history|historical|ancient|medieval|modern history|war"},
            {"literature",                  "literature|novel|poetry|poem|fiction|drama"},
            {"language",                    "language|grammar|vocabulary|translation|korean|english|japanese|chinese|spanish|french"},
            {"philosophy",                  "philosophy|ethics|logic|metaphysics|epistemology"},
            {"art",                         "art|painting|illustration|photography|gallery|museum"},
            {"music",                       "music|song|audio|composition|score|piano|guitar"},
            {"sports",                      "sports|football|soccer|basketball|baseball|tennis|workout|fitness"},
            {"food",                        "food|recipe|restaurant|cooking|meal|nutrition"},
            {"travel",                      "travel|flight|hotel|booking|itinerary|ticket"},
            {"health",                      "health|medical|hospital|insurance|prescription"},
            {"home",                        "home|house|rent|lease|utility|maintenance|moving"},
            {"government",                  "government|visa|passport|immigration|tax form|permit"},
            {"personal",                    "personal|family|diary|journal|memory|photo album"},
        };
        return firstMatch(text, rules, "general");
    }

    /// REQ-4.2.3.6: find likely project context from project-like path or filename segments.
    inline std::string projectOf(const std::string& text, const std::string& path, const std::string& fileName) {
        if (!containsAny(text, "project|capstone|milestone|team"))
            return {};
        auto candidates = pathSegments(path);
        candidates.push_back(fileName);
        for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
            const auto dot = it->find_last_of('.');
            const std::string candidate = label(dot == std::string::npos ? *it : it->substr(0, dot));
            if (!candidate.empty() && containsAny(normalized(candidate), "project|capstone|milestone|team"))
                return candidate;
        }
        return "project";
    }

    /// REQ-4.2.3.3 / REQ-4.2.3.11: bucket byte size into simple searchable labels.
    inline std::string sizeClass(std::uintmax_t bytes) {
        constexpr std::uintmax_t MiB = 1024 * 1024, GiB = MiB * 1024;
        if (bytes == 0)        return {};
        if (bytes < MiB)       return "small";
        if (bytes < 100 * MiB) return "medium";
        if (bytes < GiB)       return "large";
        return "huge";
    }

    /// REQ-4.2.3.3: read fileSize from compatible file-like objects.
    template <typename FileLike>
    inline std::uintmax_t fileSizeOf(const FileLike& file) {
        if constexpr (detail::HasFileSize<FileLike>) {
            using Size = std::decay_t<decltype(file.fileSize)>;
            if constexpr (std::is_arithmetic_v<Size>)
                return file.fileSize > 0 ? static_cast<std::uintmax_t>(file.fileSize) : 0;
        }
        return 0;
    }

    /* ------------------------------------------------------------------
     * Tag assembly - turns inferred metadata into DB/search tags for
     * REQ-4.2.3.7 and REQ-4.2.3.11.
     * ---------------------------------------------------------------- */

    /// REQ-4.2.3.11: add one normalized searchable tag.
    inline void addTag(std::vector<std::string>& tags, const std::string& tag) { pushUnique(tags, trim(toLower(tag))); }

    /// REQ-4.2.3.11: add a "prefix:value" tag when value has meaningful label content.
    inline void addPrefixedTag(std::vector<std::string>& tags, const std::string& prefix, const std::string& value) {
        const std::string cleaned = label(value);
        if (!cleaned.empty())
            addTag(tags, prefix + ":" + cleaned);
    }

    /// REQ-4.2.3.4 / REQ-4.2.3.11: add year and month tags from an ISO-like timestamp for time-aware search interpretation.
    inline void addDateTags(std::vector<std::string>& tags, const std::string& prefix, const std::string& value) {
        if (value.size() >= 4) addTag(tags, prefix + "_year:"  + value.substr(0, 4));
        if (value.size() >= 7) addTag(tags, prefix + "_month:" + value.substr(0, 7));
    }

    /// REQ-4.2.3.3 / REQ-4.2.3.11: add both a size bucket and exact byte-count tag.
    inline void addSizeTags(std::vector<std::string>& tags, std::uintmax_t bytes) {
        if (bytes == 0)
            return;
        addPrefixedTag(tags, "size", sizeClass(bytes));
        addTag(tags, "size_bytes:" + std::to_string(bytes));
    }

    /* ------------------------------------------------------------------
     * Main categorization pipeline - implements SRS 4.2.2.1-D by assigning
     * internal categorization information from metadata, content, and keywords.
     * ---------------------------------------------------------------- */

    /// REQ-4.2.3.6 / REQ-4.2.3.9 / REQ-4.2.3.10 / REQ-4.2.3.13: produce categories/tags from metadata, text, keywords, and exception state.
    template <typename FileLike>
    inline CategoryMetadata categorize(const FileLike& file,
                                       const std::string& extractedText = {},
                                       const std::vector<std::string>& customKeywords = {},
                                       const std::string& exceptionReason = {}) {
        const std::string ext  = extensionOf(file);
        const std::string text = normalized(file.fileName + " " + file.filePath + " " + extractedText);

        CategoryMetadata m;
        m.subject       = subjectOf(text);
        m.project       = projectOf(text, file.filePath, file.fileName);
        m.mediaType     = mediaType(ext);
        m.documentType  = documentType(text, ext, m.mediaType);
        m.exceptionReason = label(exceptionReason);
        m.fileSize      = fileSizeOf(file);
        m.sizeClass     = sizeClass(m.fileSize);
        m.status        = !m.exceptionReason.empty() ? "exception"
                        : m.mediaType == "document" && extractedText.empty() ? "metadata_only"
                        : "ok";

        const auto add = [&](const char* key, const std::string& val) { addPrefixedTag(m.tags, key, val); };
        add("subject",       m.subject);
        add("document_type", m.documentType);
        add("media_type",    m.mediaType);
        add("status",        m.status);
        add("project",       m.project);
        add("exception",     m.exceptionReason);
        if (!ext.empty())
            addPrefixedTag(m.tags, "extension", ext.substr(1));
        addDateTags(m.tags, "created",  file.createdDate);
        addDateTags(m.tags, "modified", file.lastModified);
        addSizeTags(m.tags, m.fileSize);

        for (const auto& kw : customKeywords)
            if (pushUnique(m.customKeywords, label(kw)))
                addPrefixedTag(m.tags, "keyword", m.customKeywords.back());
        return m;
    }

    /// REQ-4.2.2.1-B/C/D: collect metadata, analyze supported document content, then generate categorization metadata.
    inline CategoryMetadata categorizeLocalFile(const std::string& path,
                                                const std::vector<std::string>& customKeywords = {},
                                                std::size_t maxTextBytes = 64 * 1024) {
        std::string exceptionReason;
        FileMetadata file = collectMetadata(path, &exceptionReason);
        TextExtractionResult extraction;
        if (exceptionReason.empty() && isSupportedDocumentExtension(file.extension)) {
            extraction = extractTextFromFile(file.filePath.empty() ? path : file.filePath, maxTextBytes);
            if (!extraction.exceptionReason.empty())
                exceptionReason = extraction.exceptionReason;
        }
        return categorize(file, extraction.text, customKeywords, exceptionReason);
    }
}
