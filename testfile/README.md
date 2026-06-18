# FileManager Desktop

This folder contains the current File Manager app: a Windows desktop UI, a WebView2 host, a local ASP.NET Core API, and a GUI installer. The app opens in its own Windows window, not in the browser.

The backend comes from `../file2manager/src/File2Manager.Core`.

## Run During Development

```powershell
dotnet run --project .\FileManager.Desktop.csproj
```

The desktop app starts an internal local backend on an available loopback port and embeds `index.html` in WebView2.

To run only the local HTTP UI host for debugging:

```powershell
dotnet run --project .\FileManager.UiHost.csproj --urls http://127.0.0.1:5088
```

Use a different port if that address is already in use.

## Build Installer

```powershell
powershell -ExecutionPolicy Bypass -File .\installer\build-installer.ps1
```

Outputs:

```text
artifacts\FileManagerSetup.exe
artifacts\FileManagerPortable.zip
```

`FileManagerSetup.exe` opens a Windows installer UI. It lets the user choose:

- installation folder
- database folder, separate from the app folder or safely nested under it
- folders to index during installation
- whether to run initial indexing during install
- whether smart search and automatic categorization should be enabled for those folders
- desktop shortcut and launch-after-install options

When initial indexing is enabled, the installer writes `config.json`, creates the SQLite database, and builds the first quick/smart/categorization index before finishing. Reinstalling overwrites the installation folder while preserving the configured database location when possible.

`FileManagerPortable.zip` can be unzipped and run directly with `FileManager.exe`.

## Main UI

- Quick Search runs automatically as the search text changes.
- Smart Search runs in hybrid/smart modes and uses local embeddings.
- Search modes are `Hybrid`, `Quick only`, and `Smart only`.
- Modified-date filtering is selected from the UI and is based on last modified time.
- Supported modified-date filters are any time, today, yesterday, this week, last 7 days, this month, and last 30 days.
- Search is disabled while indexing; the UI shows `Indexing` with the indexing state.
- Folder pickers use native Windows folder selection dialogs.
- Custom tags can be saved per file and are reflected back into search.

## Overlay And Tray

The desktop host registers `Alt+Space` as a global overlay shortcut. The app keeps running in the system tray when the main window is closed, so the overlay can still open while the program is in the background.

When the overlay is opened from a minimized/background state, the main app surface is hidden and only the overlay is presented.

## Backend Integration

The UI talks through `BackendApi` in `app.js`. Inside the desktop app it uses the real local HTTP endpoints automatically. Direct `index.html` file mode still uses mock data because a browser page cannot call the .NET backend without the host.

Expected JavaScript bridge methods:

```js
window.FileManagerBackend = {
  getConfig(),
  getStatus(),
  getFiles(query),
  search(query, mode, modifiedDate),
  saveSetup(settings),
  saveSettings(settings),
  saveKeywords(fileId, keywords),
  openFile(path),
  rebuildIndex(),
  chooseFolder(initialPath, title)
};
```

Local HTTP endpoints:

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

`POST /api/search` request:

```json
{
  "query": "report",
  "mode": "hybrid",
  "modifiedDate": "last-7-days"
}
```

`mode` can be `hybrid`, `quick`, or `smart`. `modifiedDate` can be `any`, `today`, `yesterday`, `this-week`, `last-7-days`, `this-month`, or `last-30-days`.

Example response shape:

```json
{
  "quickResults": [
    {
      "id": "C:\\Users\\LG\\Documents\\report.pdf",
      "name": "report.pdf",
      "path": "C:\\Users\\LG\\Documents\\report.pdf",
      "modified": "2026-05-30 14:34",
      "extension": ".pdf",
      "tags": ["paper", "cs350"]
    }
  ],
  "smartResults": [
    {
      "id": "C:\\Users\\LG\\Documents\\semantic-notes.docx",
      "name": "semantic-notes.docx",
      "path": "C:\\Users\\LG\\Documents\\semantic-notes.docx",
      "modified": "2026-05-28 09:10",
      "extension": ".docx",
      "confidence": "91%",
      "tags": ["embedding", "search"]
    }
  ],
  "parsedQuery": ["Type: any", "Modified: 2026-06-11 to 2026-06-18", "Mode: hybrid"],
  "timings": {
    "quick": "0.03s",
    "smart": "0.42s"
  },
  "message": ""
}
```

## Build Check

```powershell
dotnet build .\FileManager.Desktop.csproj --configuration Release
```
