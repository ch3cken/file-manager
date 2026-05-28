/* ==========================================================================
 *  SRS 4.2.3 Categorization – Full Test Suite
 * --------------------------------------------------------------------------
 *  REQ-4.2.3.1  [Watched Directory]          -> scope add/remove/set/isInScope
 *  REQ-4.2.3.2  [Event Detection]            -> create/modify/delete polling
 *  REQ-4.2.3.3  [Metadata Collection]        -> name/path/ext/dates/size
 *  REQ-4.2.3.4  [Primary Categorization]     -> mediaType/documentType/subject rules
 *  REQ-4.2.3.5  [Text Analysis]              -> plain-text extraction + binary guard
 *  REQ-4.2.3.6  [Categorization Metadata Generation] -> categorize() tag fields
 *  REQ-4.2.3.7  [Categorization Metadata Storage]    -> SQLite tag round-trip
 *  REQ-4.2.3.8  [Keyword Management]         -> add/replace/remove-one keywords
 *  REQ-4.2.3.9  [Keyword Reflection]         -> keyword:* tags in DB
 *  REQ-4.2.3.10 [Categorization Update]      -> recategorize after keyword change
 *  REQ-4.2.3.11 [Search Reflection]          -> query terms map to file tags
 *  REQ-4.2.3.12 [Information Update]         -> modified file refreshes tags
 *  REQ-4.2.3.13 [Exception Handling]         -> missing/binary/unsupported = nonfatal
 * ========================================================================== */
#include "categorization/categorization.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Shared test infrastructure
// ---------------------------------------------------------------------------

/// Minimal SQLite adapter used by every storage test.
struct TestDb { sqlite3* db = nullptr; sqlite3* getDb() { return db; } };

static void execSql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err)
            sqlite3_free(err);
        assert(false && "SQL setup failed");
    }
}

/// In-memory DB with files + tags tables (CASCADE DELETE on file removal).
static TestDb makeDb() {
    TestDb d;
    assert(sqlite3_open(":memory:", &d.db) == SQLITE_OK);
    execSql(d.db, "PRAGMA foreign_keys = ON;");
    execSql(d.db, R"(
        CREATE TABLE files (
            file_id       INTEGER PRIMARY KEY AUTOINCREMENT,
            file_name     TEXT,
            file_path     TEXT UNIQUE,
            extension     TEXT,
            created_date  DATETIME,
            last_modified DATETIME,
            embedding     BLOB
        );
        CREATE TABLE tags (
            tag_id    INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id   INTEGER,
            tag_name  TEXT,
            FOREIGN KEY(file_id) REFERENCES files(file_id) ON DELETE CASCADE
        );
    )");
    return d;
}

/// Count rows in the tags table matching a specific tag_name.
static int tagCount(sqlite3* db, const std::string& tag) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tags WHERE tag_name = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    const int n = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    return n;
}

/// Return file_id for path, or 0 if absent.
static int fileIdByPath(sqlite3* db, const std::string& path) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT file_id FROM files WHERE file_path = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    const int id = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    return id;
}

static bool hasTag(const std::vector<std::string>& tags, const std::string& tag) {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

static bool hasEvent(const std::vector<categorization::CategorizationEvent>& ev,
                     categorization::CategorizationEventType t) {
    for (const auto& e : ev)
        if (e.type == t)
            return true;
    return false;
}

static void writeText(const fs::path& p, const std::string& text) {
    std::ofstream(p) << text;
}

// ===========================================================================
// REQ-4.2.3.1  [Watched Directory]
// ===========================================================================

/* --------------------------------------------------------------------------
 * addWatchedDirectory must accept unique directories; duplicates are ignored.
 * removeWatchedDirectory must erase only the matching entry.
 * setWatchedDirectories must atomically replace the directory list.
 * isInScope must respect both path prefix and extension filters.
 * -------------------------------------------------------------------------- */
static void testWatchedDirectoryManagement() {
    categorization::CategorizationScope scope;

    // add two distinct directories
    assert(categorization::addWatchedDirectory(scope, "/home/user/docs"));
    assert(categorization::addWatchedDirectory(scope, "/home/user/projects"));
    assert(scope.watchedDirectories.size() == 2);

    // duplicate should not be added
    assert(!categorization::addWatchedDirectory(scope, "/home/user/docs"));
    assert(scope.watchedDirectories.size() == 2);

    // remove one
    assert(categorization::removeWatchedDirectory(scope, "/home/user/docs"));
    assert(scope.watchedDirectories.size() == 1);

    // removing a non-existent entry returns false
    assert(!categorization::removeWatchedDirectory(scope, "/home/user/docs"));

    // setWatchedDirectories replaces the list completely
    categorization::setWatchedDirectories(scope, {"/a", "/b", "/c"});
    assert(scope.watchedDirectories.size() == 3);
}

static void testIsInScope() {
    categorization::CategorizationScope scope;
    categorization::addWatchedDirectory(scope, "/watched");
    categorization::addTargetExtension(scope, ".pdf");

    // helper lambda: build a minimal FileMetadata with path and extension
    auto makeFile = [](const std::string& path, const std::string& ext) {
        categorization::FileMetadata f;
        f.filePath = path;
        f.extension = ext;
        return f;
    };

    // inside scope: correct path + matching extension
    assert(categorization::isInScope(makeFile("/watched/report.pdf", ".pdf"), scope));

    // inside watched tree but wrong extension -> out of scope
    assert(!categorization::isInScope(makeFile("/watched/photo.jpg", ".jpg"), scope));

    // right extension but outside watched directory -> out of scope
    assert(!categorization::isInScope(makeFile("/other/report.pdf", ".pdf"), scope));

    // empty extension list means all extensions are allowed
    categorization::CategorizationScope open;
    categorization::addWatchedDirectory(open, "/watched");
    assert(categorization::isInScope(makeFile("/watched/anything.xyz", ".xyz"), open));

    // subdirectory respected
    assert(categorization::isInScope(makeFile("/watched/sub/deep.pdf", ".pdf"), scope));

    // non-recursive scope: file in subdirectory must be excluded
    scope.includeSubdirectories = false;
    assert(!categorization::isInScope(makeFile("/watched/sub/deep.pdf", ".pdf"), scope));
    assert(categorization::isInScope(makeFile("/watched/flat.pdf", ".pdf"), scope));
}

static void testExtensionManagement() {
    categorization::CategorizationScope scope;
    assert(categorization::addTargetExtension(scope, ".txt"));
    assert(categorization::addTargetExtension(scope, ".pdf"));
    assert(scope.targetExtensions.size() == 2);

    // duplicate suppressed (case-insensitive normalization)
    assert(!categorization::addTargetExtension(scope, ".TXT"));
    assert(scope.targetExtensions.size() == 2);

    assert(categorization::removeTargetExtension(scope, ".txt"));
    assert(scope.targetExtensions.size() == 1);

    categorization::setTargetExtensions(scope, {".docx", ".md", ".csv"});
    assert(scope.targetExtensions.size() == 3);
}

// ===========================================================================
// REQ-4.2.3.2  [Event Detection]
// ===========================================================================

/* --------------------------------------------------------------------------
 * CategorizationEventDetector must emit Created/Modified/Deleted for files
 * that match the scope; baseline advances between polls so each event fires
 * exactly once.
 * -------------------------------------------------------------------------- */
static void testEventDetection() {
    const fs::path root = fs::temp_directory_path() / "fm_cat_event_detect";
    fs::remove_all(root);
    fs::create_directories(root);

    categorization::CategorizationScope scope;
    categorization::addWatchedDirectory(scope, root.string());
    categorization::addTargetExtension(scope, ".txt");
    categorization::CategorizationEventDetector detector(scope);
    detector.prime();

    // --- Created ---
    const fs::path file = root / "operating_systems_assignment.txt";
    writeText(file, "operating systems assignment");
    auto events = detector.poll();
    assert(hasEvent(events, categorization::CategorizationEventType::Created));
    // second poll with no change: no new events
    events = detector.poll();
    assert(!hasEvent(events, categorization::CategorizationEventType::Created));

    // --- Modified ---
    writeText(file, "operating systems assignment updated");
    events = detector.poll();
    assert(hasEvent(events, categorization::CategorizationEventType::Modified));

    // --- Deleted ---
    fs::remove(file);
    events = detector.poll();
    assert(hasEvent(events, categorization::CategorizationEventType::Deleted));

    // --- Out-of-scope extension must not produce events ---
    const fs::path img = root / "photo.jpg";
    writeText(img, "binary image data");
    events = detector.poll();
    assert(!hasEvent(events, categorization::CategorizationEventType::Created));

    // --- setScope resets baseline ---
    detector.setScope(scope);
    detector.prime();
    events = detector.poll();
    assert(events.empty());

    fs::remove_all(root);
}

// ===========================================================================
// REQ-4.2.3.3  [Metadata Collection]
// ===========================================================================

/* --------------------------------------------------------------------------
 * collectMetadata must return name, path, extension, and size for a real
 * file, and must report an exception reason for a missing file.
 * -------------------------------------------------------------------------- */
static void testMetadataCollection() {
    const fs::path tmp = fs::temp_directory_path() / "fm_cat_meta.txt";
    writeText(tmp, "hello metadata");

    std::string exReason;
    categorization::FileMetadata m = categorization::collectMetadata(tmp.string(), &exReason);

    assert(exReason.empty());
    assert(m.fileName == "fm_cat_meta.txt");
    assert(!m.filePath.empty());
    assert(m.extension == ".txt");
    assert(m.fileSize > 0);
    assert(!m.createdDate.empty());
    assert(!m.lastModified.empty());

    // extension normalization: always lowercase with leading dot
    assert(m.extension == categorization::normalizeExtension("TXT"));

    fs::remove(tmp);

    // missing file: exception reason populated; metadata is partial but safe
    categorization::FileMetadata missing = categorization::collectMetadata("/nonexistent/path/file.pdf", &exReason);
    assert(!exReason.empty());
    assert(missing.fileName == "file.pdf");
    assert(missing.extension == ".pdf");
}

static void testPathHelpers() {
    // normalizeExtension
    assert(categorization::normalizeExtension("PDF")    == ".pdf");
    assert(categorization::normalizeExtension(".PDF")   == ".pdf");
    assert(categorization::normalizeExtension("")       == "");

    // extensionFromPath
    assert(categorization::extensionFromPath("/home/user/report.PDF") == ".pdf");
    assert(categorization::extensionFromPath("/no/ext")               == "");
    // dotted directory name must not produce a false extension
    assert(categorization::extensionFromPath("/home/user.cfg/noext") == "");

    // pathMatchesDirectory: subdirectory matching
    assert(categorization::pathMatchesDirectory("/watched/sub/file.txt", "/watched", true));
    assert(!categorization::pathMatchesDirectory("/watched/sub/file.txt", "/watched", false));
    assert(categorization::pathMatchesDirectory("/watched/file.txt", "/watched", false));
    assert(!categorization::pathMatchesDirectory("/other/file.txt",  "/watched", true));
}

// ===========================================================================
// REQ-4.2.3.4  [Primary Categorization] / REQ-4.2.3.6  [Metadata Generation]
// ===========================================================================

/* --------------------------------------------------------------------------
 * mediaType classifies extensions into broad media families.
 * documentType narrows document-family files into subtypes.
 * subjectOf infers the academic/professional domain.
 * categorize() assembles all tags and fills CategoryMetadata fields.
 * -------------------------------------------------------------------------- */
static void testMediaType() {
    assert(categorization::mediaType(".pdf")  == "document");
    assert(categorization::mediaType(".docx") == "document");
    assert(categorization::mediaType(".pptx") == "document");
    assert(categorization::mediaType(".xlsx") == "document");
    assert(categorization::mediaType(".txt")  == "document");
    assert(categorization::mediaType(".jpg")  == "image");
    assert(categorization::mediaType(".png")  == "image");
    assert(categorization::mediaType(".mp4")  == "video");
    assert(categorization::mediaType(".mp3")  == "audio");
    assert(categorization::mediaType(".zip")  == "archive");
    assert(categorization::mediaType(".cpp")  == "code");
    assert(categorization::mediaType(".json") == "data");
    assert(categorization::mediaType(".exe")  == "executable");
    assert(categorization::mediaType(".xyz")  == "other");
}

static void testDocumentType() {
    // Extension-driven subtypes
    assert(categorization::documentType(" presentation ", ".pptx", "document") == "presentation");
    assert(categorization::documentType(" spreadsheet ",  ".xlsx", "document") == "spreadsheet");
    // Term-driven subtypes
    assert(categorization::documentType(" machine learning paper ", ".pdf", "document") == "paper");
    assert(categorization::documentType(" os assignment ",          ".pdf", "document") == "assignment");
    assert(categorization::documentType(" syllabus spring ",        ".pdf", "document") == "syllabus");
    assert(categorization::documentType(" midterm exam ",           ".pdf", "document") == "exam");
    assert(categorization::documentType(" meeting minutes ",        ".docx","document") == "meeting record");
    // Non-document media type passes through unchanged
    assert(categorization::documentType(" anything ", ".jpg", "image") == "image");
    // Fallback for plain document without recognized subtype
    assert(categorization::documentType(" random content ", ".pdf", "document") == "document");
}

static void testSubjectOf() {
    assert(categorization::subjectOf(" machine learning neural network ") == "machine learning");
    assert(categorization::subjectOf(" nlp language model embedding ")    == "natural language processing");
    assert(categorization::subjectOf(" operating system os ")             == "operating systems");
    assert(categorization::subjectOf(" sql database transaction ")        == "database");
    assert(categorization::subjectOf(" software engineering srs agile ")  == "software engineering");
    assert(categorization::subjectOf(" ")                                 == "general");
}

static void testCategorizeProducesExpectedFields() {
    categorization::FileMetadata f;
    f.fileName     = "machine_learning_paper.pdf";
    f.filePath     = "/downloads/machine_learning_paper.pdf";
    f.extension     = ".pdf";
    f.fileSize     = 512 * 1024; // 512 KiB -> "small"
    f.createdDate  = "2025-03-15 10:00:00";
    f.lastModified = "2025-04-01 08:30:00";

    const auto m = categorization::categorize(f, "machine learning paper introduction", {"reviewed"});

    assert(m.subject       == "machine learning");
    assert(m.mediaType    == "document");
    assert(m.documentType == "paper");
    assert(m.status        == "ok");
    assert(m.fileSize     == f.fileSize);
    assert(m.sizeClass    == "small");

    assert(hasTag(m.tags, "subject:machine learning"));
    assert(hasTag(m.tags, "media_type:document"));
    assert(hasTag(m.tags, "document_type:paper"));
    assert(hasTag(m.tags, "extension:pdf"));
    assert(hasTag(m.tags, "status:ok"));
    assert(hasTag(m.tags, "keyword:reviewed"));
    assert(hasTag(m.tags, "created_year:2025"));
    assert(hasTag(m.tags, "created_month:2025-03"));
    assert(hasTag(m.tags, "modified_year:2025"));
    assert(hasTag(m.tags, "modified_month:2025-04"));
    assert(hasTag(m.tags, "size:small"));
}

static void testProjectInference() {
    categorization::FileMetadata f;
    f.fileName = "report.pdf";
    f.filePath = "/work/capstone_team12/milestone/report.pdf";
    f.extension = ".pdf";

    const auto m = categorization::categorize(f, "team milestone report");
    assert(m.project == "capstone team12");
    assert(hasTag(m.tags, "project:capstone team12"));
}

// ===========================================================================
// REQ-4.2.3.5  [Text Analysis]
// ===========================================================================

/* --------------------------------------------------------------------------
 * extractPlainTextFromFile must read .txt content and return it in the
 * result. A file whose content is predominantly NUL bytes must be flagged
 * as binary (exception state).
 * -------------------------------------------------------------------------- */
static void testPlainTextExtraction() {
    const fs::path tmp = fs::temp_directory_path() / "fm_cat_text.txt";
    writeText(tmp, "machine learning paper on neural networks");

    auto result = categorization::extractPlainTextFromFile(tmp.string());
    assert(result.attempted);
    assert(result.success);
    assert(result.text.find("machine learning") != std::string::npos);

    fs::remove(tmp);
}

static void testBinaryGuard() {
    const fs::path tmp = fs::temp_directory_path() / "fm_cat_binary.bin";
    // write a file that is mostly NUL bytes
    {
        std::ofstream out(tmp, std::ios::binary);
        std::string data(256, '\0');
        data[0] = 'A'; // one non-NUL byte — still triggers binary guard
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    auto result = categorization::extractPlainTextFromFile(tmp.string());
    assert(result.attempted);
    assert(!result.success);
    assert(!result.exceptionReason.empty());

    fs::remove(tmp);
}

static void testMissingFileExtraction() {
    auto result = categorization::extractPlainTextFromFile("/nonexistent/file.txt");
    assert(result.attempted);
    assert(!result.success);
    assert(!result.exceptionReason.empty());
}

static void testExtractTextDispatch() {
    // .txt dispatches to the plain-text extractor
    const fs::path tmp = fs::temp_directory_path() / "fm_cat_dispatch.txt";
    writeText(tmp, "operating systems assignment content");
    auto r = categorization::extractTextFromFile(tmp.string());
    assert(r.success);
    fs::remove(tmp);

    // unsupported extension returns an empty, non-attempted result
    auto none = categorization::extractTextFromFile("/some/file.xyz");
    assert(!none.attempted);
    assert(!none.success);
}

static void testSupportedFormatDispatchOnMissingFiles() {
    // Supported binary formats should dispatch to dedicated extractors.
    const auto pdf = categorization::extractTextFromFile("/missing/document.pdf");
    assert(pdf.attempted);
    assert(!pdf.success);
    assert(!pdf.exceptionReason.empty());

    const auto docx = categorization::extractTextFromFile("/missing/document.docx");
    assert(docx.attempted);
    assert(!docx.success);
    assert(!docx.exceptionReason.empty());
}

// ===========================================================================
// REQ-4.2.3.7  [Categorization Metadata Storage]
// REQ-4.2.3.9  [Keyword Reflection]
// REQ-4.2.3.12 [Information Update]
// ===========================================================================

/* --------------------------------------------------------------------------
 * A Created event must persist file row + all category tags in SQLite.
 * A Deleted event must cascade-remove both file row and its tags.
 * A Modified event must refresh (not duplicate) the managed tags.
 * -------------------------------------------------------------------------- */
static void testEventApplicationStoresAndDeletesTags() {
    TestDb db = makeDb();
    const fs::path root = fs::temp_directory_path() / "fm_cat_storage";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path file = root / "machine_learning_paper.txt";
    writeText(file, "machine learning paper");

    categorization::CategorizationEvent event;
    event.type      = categorization::CategorizationEventType::Created;
    event.file      = categorization::collectMetadata(file.string());
    event.filePath = event.file.filePath;

    // --- Created: metadata + tags stored ---
    auto result = categorization::applyCategorizationEvent(db, event, {"reviewed"});
    assert(result.handled);
    assert(result.metadataStored);
    assert(result.categorizationStored);
    assert(result.fileId > 0);
    assert(tagCount(db.db, "subject:machine learning") == 1);
    assert(tagCount(db.db, "document_type:paper")      == 1);
    assert(tagCount(db.db, "media_type:document")      == 1);
    assert(tagCount(db.db, "keyword:reviewed")         == 1);

    // idempotent: re-applying Created must not duplicate tags
    result = categorization::applyCategorizationEvent(db, event, {"reviewed"});
    assert(tagCount(db.db, "subject:machine learning") == 1);
    assert(tagCount(db.db, "keyword:reviewed")         == 1);

    // --- Deleted: cascade removes file row and all its tags ---
    event.type = categorization::CategorizationEventType::Deleted;
    result = categorization::applyCategorizationEvent(db, event);
    assert(result.deleted);
    assert(fileIdByPath(db.db, event.filePath) == 0);
    assert(tagCount(db.db, "subject:machine learning") == 0);
    assert(tagCount(db.db, "document_type:paper")      == 0);
    assert(tagCount(db.db, "keyword:reviewed")         == 0);

    sqlite3_close(db.db);
    fs::remove_all(root);
}

static void testModifiedEventRefreshesTagsWithoutDuplication() {
    TestDb db = makeDb();
    const fs::path root = fs::temp_directory_path() / "fm_cat_modify";
    fs::remove_all(root);
    fs::create_directories(root);

    // start as operating-systems content
    const fs::path file = root / "os_assignment.txt";
    writeText(file, "operating systems assignment");

    categorization::CategorizationEvent ev;
    ev.type      = categorization::CategorizationEventType::Created;
    ev.file      = categorization::collectMetadata(file.string());
    ev.filePath = ev.file.filePath;
    categorization::applyCategorizationEvent(db, ev);
    assert(tagCount(db.db, "subject:operating systems") == 1);

    // re-write with new subject and apply as Modified
    writeText(file, "machine learning neural network lecture");
    ev.type = categorization::CategorizationEventType::Modified;
    ev.file = categorization::collectMetadata(file.string());
    categorization::applyCategorizationEvent(db, ev);

    // old subject gone, new subject present, each appears exactly once
    assert(tagCount(db.db, "subject:operating systems") == 0);
    assert(tagCount(db.db, "subject:machine learning")  == 1);

    sqlite3_close(db.db);
    fs::remove_all(root);
}

// ===========================================================================
// REQ-4.2.3.8  [Keyword Management]
// REQ-4.2.3.9  [Keyword Reflection]
// REQ-4.2.3.10 [Categorization Update]
// ===========================================================================

/* --------------------------------------------------------------------------
 * addCustomKeywords must insert keyword:* tags.
 * removeCustomKeyword must remove exactly the matching keyword:* tag.
 * replaceCustomKeywords must swap the full keyword set atomically.
 * replaceKeywordsAndRecategorize must refresh both keywords and auto tags.
 * -------------------------------------------------------------------------- */
static void testCustomKeywordLifecycle() {
    TestDb db = makeDb();
    const fs::path root = fs::temp_directory_path() / "fm_cat_kw";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path file = root / "report.txt";
    writeText(file, "quarterly business report");

    // Store initial categorization
    categorization::CategorizationEvent ev;
    ev.type      = categorization::CategorizationEventType::Created;
    ev.file      = categorization::collectMetadata(file.string());
    ev.filePath = ev.file.filePath;
    auto result  = categorization::applyCategorizationEvent(db, ev);
    const int id = result.fileId;
    assert(id > 0);

    // --- addCustomKeywords ---
    assert(categorization::addCustomKeywords(db, id, {"important", "q3"}));
    assert(tagCount(db.db, "keyword:important") == 1);
    assert(tagCount(db.db, "keyword:q3")        == 1);

    // --- removeCustomKeyword: removes exactly one keyword tag ---
    assert(categorization::removeCustomKeyword(db, id, "q3"));
    assert(tagCount(db.db, "keyword:q3")        == 0);
    assert(tagCount(db.db, "keyword:important") == 1); // untouched

    // --- replaceCustomKeywords: atomically swaps the keyword set ---
    assert(categorization::replaceCustomKeywords(db, id, {"draft", "2025"}));
    assert(tagCount(db.db, "keyword:important") == 0); // cleared
    assert(tagCount(db.db, "keyword:draft")     == 1);
    assert(tagCount(db.db, "keyword:2025")      == 1);

    // --- addKeywordsAndRecategorize: adds kw and refreshes managed tags ---
    assert(categorization::addKeywordsAndRecategorize(db, id, ev.file, {"final"}));
    assert(tagCount(db.db, "keyword:final") == 1);
    // managed tags must still be present (not cleared by the kw add)
    assert(tagCount(db.db, "media_type:document") == 1);

    // --- replaceKeywordsAndRecategorize: swaps kw and refreshes auto tags ---
    assert(categorization::replaceKeywordsAndRecategorize(db, id, ev.file, {"v2"}));
    assert(tagCount(db.db, "keyword:final") == 0); // replaced
    assert(tagCount(db.db, "keyword:v2")    == 1);
    assert(tagCount(db.db, "media_type:document") == 1); // auto tag preserved

    sqlite3_close(db.db);
    fs::remove_all(root);
}

// ===========================================================================
// REQ-4.2.3.11  [Search Reflection]
// ===========================================================================

/* --------------------------------------------------------------------------
 * reflectSearchQuery must emit the same tag vocabulary that rules.h stores
 * for matching files, enabling Smart Search to filter/rank by stored tags.
 * -------------------------------------------------------------------------- */
static void testSearchReflection() {
    // --- Core academic query ---
    {
        const auto r = categorization::reflectSearchQuery("machine learning paper");
        assert(hasTag(r.tags, "subject:machine learning"));
        assert(hasTag(r.tags, "document_type:paper"));
        assert(hasTag(r.tags, "media_type:document"));
    }

    // --- Image query ---
    {
        const auto r = categorization::reflectSearchQuery("screenshot png");
        assert(hasTag(r.tags, "media_type:image"));
        assert(hasTag(r.tags, "extension:png"));
    }

    // --- Video query ---
    {
        const auto r = categorization::reflectSearchQuery("lecture video recording");
        assert(hasTag(r.tags, "media_type:video"));
    }

    // --- Archive query ---
    {
        const auto r = categorization::reflectSearchQuery("backup zip archive");
        assert(hasTag(r.tags, "media_type:archive"));
        assert(hasTag(r.tags, "extension:zip"));
    }

    // --- Code query ---
    {
        const auto r = categorization::reflectSearchQuery("python script");
        assert(hasTag(r.tags, "media_type:code"));
        assert(hasTag(r.tags, "extension:py"));
    }

    // --- Size qualifier ---
    {
        const auto r = categorization::reflectSearchQuery("large pdf report");
        assert(hasTag(r.tags, "size:large"));
    }

    // --- Keyword tags included in main tag list ---
    {
        const auto r = categorization::reflectSearchQuery("os assignment");
        // keyword tags should be merged into r.tags
        for (const auto& kt : r.keywordTags)
            assert(hasTag(r.tags, kt));
    }

    // --- Unrecognized query must not crash; returns empty or general tags ---
    {
        const auto r = categorization::reflectSearchQuery("xyzzy frobnicator");
        (void)r; // just no crash
    }
}

/* --------------------------------------------------------------------------
 * Tag vocabulary alignment: a file categorized by categorize() and a query
 * processed by reflectSearchQuery() must share at least one tag for the same
 * piece of content — verifying the bridge between storage and search.
 * -------------------------------------------------------------------------- */
static void testSearchReflectionAlignedWithFileTag() {
    categorization::FileMetadata f;
    f.fileName  = "nlp_lecture.pdf";
    f.filePath  = "/lectures/nlp_lecture.pdf";
    f.extension  = ".pdf";

    const auto fileMeta  = categorization::categorize(f, "natural language processing lecture");
    const auto reflected = categorization::reflectSearchQuery("natural language processing lecture");

    bool aligned = false;
    for (const auto& t : reflected.tags)
        if (hasTag(fileMeta.tags, t)) {
            aligned = true; break;
        }
    assert(aligned && "search reflection tags must overlap with stored file tags");
}

// ===========================================================================
// REQ-4.2.3.13  [Exception Handling]
// ===========================================================================

/* --------------------------------------------------------------------------
 * Unsupported, missing, or binary files must not abort; they produce either
 * metadata_only or exception status without crashing.
 * -------------------------------------------------------------------------- */
static void testExceptionHandlingMissingFile() {
    std::string ex;
    const auto m = categorization::collectMetadata("/no/such/file/ever.pdf", &ex);
    assert(!ex.empty());   // reason captured
    assert(m.filePath == "/no/such/file/ever.pdf"); // partial metadata safe
    assert(m.extension == ".pdf");
}

static void testExceptionHandlingBinaryFile() {
    const fs::path tmp = fs::temp_directory_path() / "fm_cat_ex_binary.bin";
    {
        std::ofstream out(tmp, std::ios::binary);
        std::string data(512, '\0');
        data[1] = 'X';
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    // Treating it as a document extension: extraction should fail gracefully
    auto r = categorization::extractPlainTextFromFile(tmp.string());
    assert(r.attempted && !r.success && !r.exceptionReason.empty());
    fs::remove(tmp);
}

static void testExceptionHandlingUnsupportedExtension() {
    // extractTextFromFile on an unsupported extension must return a
    // non-attempted result without throwing
    const auto r = categorization::extractTextFromFile("/any/path/file.bin");
    assert(!r.attempted && !r.success);
}

static void testCategorizationStatusForUnsupportedFile() {
    // categorize() sets status = "exception" when given a non-empty reason
    categorization::FileMetadata f;
    f.fileName = "locked.pdf";
    f.filePath = "/locked.pdf";
    f.extension = ".pdf";

    const auto m = categorization::categorize(f, {}, {}, "locked pdf");
    assert(m.status == "exception");
    assert(!m.exceptionReason.empty());
    assert(hasTag(m.tags, "status:exception"));
}

static void testCategorizationStatusMetadataOnly() {
    // document extension + empty extracted text and no exception -> metadata_only
    categorization::FileMetadata f;
    f.fileName = "scanned.pdf";
    f.filePath = "/docs/scanned.pdf";
    f.extension = ".pdf";

    const auto m = categorization::categorize(f, "" /* no text */, {}, "" /* no exception */);
    assert(m.status == "metadata_only");
    assert(hasTag(m.tags, "status:metadata only"));
}

static void testApplyEventOnMissingPathIsNonfatal() {
    TestDb db = makeDb();

    categorization::CategorizationEvent ev;
    ev.type      = categorization::CategorizationEventType::Created;
    ev.filePath = "/absolutely/nonexistent/file.txt";

    // Must not throw — even for a non-existent path the pipeline completes.
    // The category's status must be "exception" (metadata collection fails
    // for a missing file and propagates the exception reason through to categorize()).
    const auto r = categorization::applyCategorizationEvent(db, ev);
    assert(r.handled);
    // Either no row was inserted, or the stored category carries exception status
    assert(!r.metadataStored || r.category.status == "exception"
           || !r.exceptionReason.empty());

    sqlite3_close(db.db);
}

static void testCategorizeLocalFileOnMissingPath() {
    const auto m = categorization::categorizeLocalFile("/no/such/path/missing.pdf");
    assert(m.status == "exception");
    assert(!m.exceptionReason.empty());
    assert(hasTag(m.tags, "status:exception"));
}

static void testCategorizeLocalFileAndStore() {
    TestDb db = makeDb();
    const fs::path root = fs::temp_directory_path() / "fm_cat_local_store";
    fs::remove_all(root);
    fs::create_directories(root);

    const fs::path file = root / "database_notes.txt";
    writeText(file, "sql database transaction notes");

    const auto r = categorization::categorizeLocalFileAndStore(db, file.string(), {"important"});
    assert(r.handled);
    assert(r.metadataStored);
    assert(r.categorizationStored);
    assert(r.fileId > 0);
    assert(tagCount(db.db, "subject:database") == 1);
    assert(tagCount(db.db, "keyword:important") == 1);

    sqlite3_close(db.db);
    fs::remove_all(root);
}

// ===========================================================================
// REQ-4.2.3.1 — Scope-filtered event application
// ===========================================================================

static void testScopedEventApplicationFiltersOutOfScope() {
    TestDb db = makeDb();
    const fs::path root = fs::temp_directory_path() / "fm_cat_scope_filter";
    fs::remove_all(root);
    fs::create_directories(root);

    categorization::CategorizationScope scope;
    categorization::addWatchedDirectory(scope, root.string());
    categorization::addTargetExtension(scope, ".pdf"); // only PDFs

    const fs::path txt = root / "notes.txt"; // not a PDF -> out of scope
    writeText(txt, "operating systems notes");

    categorization::CategorizationEvent ev;
    ev.type      = categorization::CategorizationEventType::Created;
    ev.file      = categorization::collectMetadata(txt.string());
    ev.filePath = ev.file.filePath;

    const auto r = categorization::applyCategorizationEvent(db, scope, ev);
    assert(!r.inScope);        // scoped overload must reject it
    assert(!r.metadataStored); // nothing written to DB

    sqlite3_close(db.db);
    fs::remove_all(root);
}

static void testScopedEventApplicationAcceptsInScope() {
    TestDb db = makeDb();
    const fs::path root = fs::temp_directory_path() / "fm_cat_scope_accept";
    fs::remove_all(root);
    fs::create_directories(root);

    categorization::CategorizationScope scope;
    categorization::addWatchedDirectory(scope, root.string());
    categorization::addTargetExtension(scope, ".txt");

    const fs::path txt = root / "ml_notes.txt";
    writeText(txt, "machine learning notes");

    categorization::CategorizationEvent ev;
    ev.type = categorization::CategorizationEventType::Created;
    ev.file = categorization::collectMetadata(txt.string());
    ev.filePath = ev.file.filePath;

    const auto r = categorization::applyCategorizationEvent(db, scope, ev, {"reviewed"});
    assert(r.inScope);
    assert(r.metadataStored);
    assert(r.categorizationStored);
    assert(r.fileId > 0);
    assert(tagCount(db.db, "keyword:reviewed") == 1);

    sqlite3_close(db.db);
    fs::remove_all(root);
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    // REQ-4.2.3.1
    testWatchedDirectoryManagement();
    testIsInScope();
    testExtensionManagement();

    // REQ-4.2.3.2
    testEventDetection();

    // REQ-4.2.3.3
    testMetadataCollection();
    testPathHelpers();

    // REQ-4.2.3.4 / REQ-4.2.3.6
    testMediaType();
    testDocumentType();
    testSubjectOf();
    testCategorizeProducesExpectedFields();
    testProjectInference();

    // REQ-4.2.3.5
    testPlainTextExtraction();
    testBinaryGuard();
    testMissingFileExtraction();
    testExtractTextDispatch();
    testSupportedFormatDispatchOnMissingFiles();

    // REQ-4.2.3.7 / REQ-4.2.3.9 / REQ-4.2.3.12
    testEventApplicationStoresAndDeletesTags();
    testModifiedEventRefreshesTagsWithoutDuplication();

    // REQ-4.2.3.8 / REQ-4.2.3.9 / REQ-4.2.3.10
    testCustomKeywordLifecycle();

    // REQ-4.2.3.11
    testSearchReflection();
    testSearchReflectionAlignedWithFileTag();

    // REQ-4.2.3.13
    testExceptionHandlingMissingFile();
    testExceptionHandlingBinaryFile();
    testExceptionHandlingUnsupportedExtension();
    testCategorizationStatusForUnsupportedFile();
    testCategorizationStatusMetadataOnly();
    testApplyEventOnMissingPathIsNonfatal();
    testCategorizeLocalFileOnMissingPath();
    testCategorizeLocalFileAndStore();

    // REQ-4.2.3.1 (scoped dispatch)
    testScopedEventApplicationFiltersOutOfScope();
    testScopedEventApplicationAcceptsInScope();

    return 0;
}
