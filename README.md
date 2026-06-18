# File Manager

Windows desktop file search app built from the Team 12 SRS. The current product is the .NET desktop app in `testfile/`, backed by the reusable `file2manager` indexing and search core.

The app is local-first: it stores file metadata, generated tags, custom tags, extracted document text, and embeddings in a local SQLite database. It does not require a remote search service at runtime.

Team members:

- Antonio Recuero Buleje, 20230866
- Enrique Jose Delgado Garcia, 20220825
- Elmoursi Ahmad, 20230951
- Murad Ibrahimov, 20230959

## Repository Layout

```text
file-manager/
|-- testfile/       Current desktop app, WebView2 UI, local HTTP host, installer
|-- file2manager/   .NET backend/core library and earlier WPF shell
|-- src/            C++ prototype implementation
|-- include/        C++ prototype headers
|-- tests/          C++ prototype tests
|-- models/         C++ prototype embedding model assets
|-- CMakeLists.txt  C++ prototype build
`-- vcpkg.json      C++ prototype dependencies
```

## Current App

Use `testfile/` for the application we have been iterating on.

- Native Windows window using WPF plus WebView2, not a browser tab.
- Local ASP.NET Core API hosted inside the desktop app.
- Global `Alt+Space` overlay search. Closing the window hides the app to the tray so the overlay can still open while the app runs in the background.
- Installer GUI with selectable install folder, selectable database folder, optional initial indexing, smart search categorization, desktop shortcut, and launch-after-install options.
- Installations overwrite the app folder while preserving the configured database location when it is separate or nested safely under the install path.

Run during development:

```powershell
cd .\testfile
dotnet run --project .\FileManager.Desktop.csproj
```

Build the installer:

```powershell
cd .\testfile
powershell -ExecutionPolicy Bypass -File .\installer\build-installer.ps1
```

Installer outputs:

```text
testfile\artifacts\FileManagerSetup.exe
testfile\artifacts\FileManagerPortable.zip
```

## Search Behavior

Quick Search is optimized for fast interactive typing:

- Runs automatically whenever the search text changes.
- Returns at most 20 files.
- Uses SQLite metadata queries only; it does not load `content_text`.
- Uses a `file_quick_tokens` table instead of broad `LIKE` scans.
- Splits filenames and paths into independent tokens by slash, whitespace, dash, underscore, comma, and semicolon.
- Matches by token prefix, so `rep` can match `report`.
- Ranks filename and extension matches above custom tags/categories, and ranks full-path directory matches lower.

Smart Search is slower but more flexible:

- Uses local ONNX embeddings from `sentence-transformers/all-MiniLM-L6-v2`.
- Stores per-file embedding vectors in SQLite for smart-indexed files.
- Combines semantic similarity with metadata, extracted text, local generated tags, and user custom tags.
- Supports semantic matching such as related terms and synonyms better than tag-only search.

Search modes:

- `Hybrid`: quick results plus smart results.
- `Quick only`: metadata prefix search only.
- `Smart only`: embedding/tag/content ranking only.

Modified-date filters are explicit UI controls, not inferred from natural-language query text. The current filters are any time, today, yesterday, this week, last 7 days, this month, and last 30 days. All date filtering is based on last modified time.

When indexing is active, search is blocked and the UI reports `Indexing` instead of `Index ready`.

## Backend Core

The reusable backend lives in `file2manager/src/File2Manager.Core`.

Main responsibilities:

- SQLite schema creation and migration.
- File metadata indexing.
- Quick-search token index maintenance.
- Document text extraction for TXT, Markdown, CSV, JSON, XML, HTML, PDF, DOCX, and source-like text files.
- Local topic/tag generation.
- Custom keyword persistence.
- Semantic embedding generation and cosine-similarity scoring.
- File-system watcher based incremental updates after the initial index.

The `file2manager/src/File2Manager.App` WPF app is an earlier shell around the same core. The current user-facing app is `testfile/FileManager.Desktop.csproj`.

Run backend smoke tests:

```powershell
dotnet run --project .\file2manager\tests\File2Manager.SmokeTests\File2Manager.SmokeTests.csproj --configuration Release
```

Build the desktop app:

```powershell
dotnet build .\testfile\FileManager.Desktop.csproj --configuration Release
```

## Current API Surface

The desktop app hosts these local endpoints:

```text
GET  /api/health
GET  /api/config
GET  /api/status
GET  /api/files
POST /api/search
POST /api/setup
POST /api/settings
POST /api/keywords
POST /api/files/open
POST /api/index/rebuild
```

`POST /api/search` accepts:

```json
{
  "query": "report",
  "mode": "hybrid",
  "modifiedDate": "last-7-days"
}
```

`mode` can be `hybrid`, `quick`, or `smart`. `modifiedDate` can be `any`, `today`, `yesterday`, `this-week`, `last-7-days`, `this-month`, or `last-30-days`.

## C++ Prototype

The root `src/`, `include/`, `tests/`, and `models/` folders contain the earlier C++ prototype. It still builds with CMake and vcpkg, and it includes prototype quick search, smart search, indexing, database, and overlay pieces.

Configure and build the C++ prototype:

```powershell
git submodule update --init
.\vcpkg\bootstrap-vcpkg.bat
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The C++ prototype is kept for reference and tests; new product work should target `testfile/` and `file2manager/`.
