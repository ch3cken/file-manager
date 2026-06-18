# file2manager

`file2manager` contains the reusable .NET backend for the File Manager desktop app. The current polished UI and installer live in `../testfile`, while this folder provides the indexing, categorization, quick search, smart search, and an earlier WPF shell.

## Projects

```text
file2manager/
|-- src/File2Manager.Core/        Shared backend library
|-- src/File2Manager.App/         Earlier WPF desktop shell
|-- tests/File2Manager.SmokeTests Backend smoke tests
`-- File2Manager.sln
```

`File2Manager.Core` is referenced by both:

- `src/File2Manager.App/File2Manager.App.csproj`
- `../testfile/FileManager.UiHost.csproj`
- `../testfile/FileManager.Installer.csproj`

## Backend Features

- Local SQLite database stored at the configured database folder.
- File metadata indexing with created time, last modified time, size, extension, filename, and full path.
- Quick Search token table for fast prefix matching without scanning document content.
- Quick Search result cap of 20 files through the service layer.
- Filename and extension matches rank above custom tags/categories; directory path matches rank lower.
- Smart Search over locally stored ONNX embeddings.
- Local embedding model: `sentence-transformers/all-MiniLM-L6-v2`, quantized ONNX assets under `src/File2Manager.Core/Embeddings`.
- PDF, DOCX, TXT, Markdown, CSV, JSON, XML, HTML, and source-text extraction for smart-indexed files.
- Local generated topic tags plus user-provided custom tags.
- Explicit last-modified date filters supplied by the UI/API.
- File-system watcher support after the initial index.
- Local-only runtime behavior; no remote AI/search service is required.

## Quick Search

Quick Search is database-backed and uses metadata only. It does not select or scan `content_text`.

During indexing, each file gets quick-search tokens in `file_quick_tokens`. Tokens are split from filename, extension, custom keywords, generated categories, and directory path using separators such as slashes, spaces, dashes, underscores, commas, and semicolons.

Queries are split the same way. Each query term is matched as a prefix range:

```sql
token >= term AND token < term || '\uffff'
```

This avoids broad `LIKE` queries and lets SQLite use the quick-token index.

## Smart Search

Smart Search is semantic search. For smart-indexed files, the core extracts available text and builds an embedding from the file metadata, generated tags, custom tags, and content snippet. Search queries are embedded with the same local model and ranked with cosine similarity plus metadata/tag scoring.

Smart Search is intentionally heavier than Quick Search. It is meant for semantic matches, synonyms, topic matching, and document-content matching.

## Run the Earlier WPF Shell

```powershell
dotnet run --project .\src\File2Manager.App\File2Manager.App.csproj
```

On first launch, the setup wizard creates `config.json` next to the app executable and initializes the configured SQLite database.

For the current product UI, use:

```powershell
cd ..\testfile
dotnet run --project .\FileManager.Desktop.csproj
```

## Tests

```powershell
dotnet build .\File2Manager.sln
dotnet run --project .\tests\File2Manager.SmokeTests\File2Manager.SmokeTests.csproj --configuration Release
```

The smoke test covers:

- indexing
- quick search prefix matching
- quick search result capping
- smart search ranking
- semantic/synonym behavior
- generated tags
- custom keyword search

To optionally sample the local Downloads folder without printing file names:

```powershell
$env:FILE2MANAGER_REAL_SCAN='1'
dotnet run --project .\tests\File2Manager.SmokeTests\File2Manager.SmokeTests.csproj --configuration Release
Remove-Item Env:\FILE2MANAGER_REAL_SCAN
```

## Runtime Data

Build outputs, `config.json`, and SQLite database files are runtime artifacts and should not be committed. The repository keeps source code and required embedding assets only.
