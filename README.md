# file2manager

Windows desktop file search and categorization app implemented from the Team 12 SRS.

## Run

```powershell
dotnet run --project .\src\File2Manager.App\File2Manager.App.csproj
```

On first launch, the setup wizard creates `config.json` next to the application executable and initializes a local SQLite database at the configured database path.

## Test

```powershell
dotnet build .\File2Manager.sln
dotnet run --project .\tests\File2Manager.SmokeTests\File2Manager.SmokeTests.csproj
```

The smoke test covers indexing, Quick Search, Smart Search ranking, and custom keyword search.

To also sample the local Downloads folder without printing file names:

```powershell
$env:FILE2MANAGER_REAL_SCAN='1'
dotnet run --project .\tests\File2Manager.SmokeTests\File2Manager.SmokeTests.csproj
Remove-Item Env:\FILE2MANAGER_REAL_SCAN
```

## Implemented SRS Features

- Local Quick Search using a prebuilt SQLite file index with parallel initial indexing and batched writes.
- Local Smart Search over all smart-indexed files using parsed time/file-type conditions and local text-token similarity.
- PDF, DOCX, TXT, Markdown, CSV, JSON, XML, HTML, and source-text extraction for supported smart indexing.
- Metadata and content-based categorization with generated local topic tags and custom keyword reflection.
- Setup wizard, settings tabs, indexing status, fixed dark appearance, and custom keyword management.
- Global hotkey overlay with split Quick Search and Smart Search results.
- Local-only config and database storage; no external service calls at runtime.
