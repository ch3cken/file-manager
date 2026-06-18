# FileManager Desktop

This folder contains the improved UI, the real `file2manager` backend integration, and a desktop WebView2 wrapper. The app opens in its own Windows window, not in the browser.

## Run During Development

```powershell
dotnet run --project .\FileManager.Desktop.csproj
```

The desktop app starts an internal local backend on an available loopback port and embeds the UI in WebView2.

## Build Installer

```powershell
powershell -ExecutionPolicy Bypass -File .\installer\build-installer.ps1
```

Outputs:

```text
artifacts\FileManagerSetup.exe
artifacts\FileManagerPortable.zip
```

`FileManagerSetup.exe` defaults to `%LOCALAPPDATA%\FileManager`, creates a Start Menu shortcut, and can launch `FileManager.exe` after installation.

The setup executable now opens a Windows installer UI. It lets the user choose:

- installation folder
- database folder, which can be separate from the app folder
- folders to index during installation
- whether to run initial indexing during install
- whether smart search and automatic categorization should be enabled for those folders
- desktop shortcut and launch-after-install options

When initial indexing is enabled, the installer writes `config.json`, creates the SQLite database in the chosen database folder, and builds the first quick/smart/categorization index before finishing.

`FileManagerPortable.zip` can be unzipped and run directly with `FileManager.exe`.

## Backend Integration

The UI talks through `BackendApi` in `app.js`. Inside the desktop app it uses the real local endpoints automatically. Direct `index.html` file mode still uses mock data because browser pages cannot call the .NET backend without the host.

1. Expose `window.FileManagerBackend` before `app.js` loads.
2. Set `window.FILE_MANAGER_USE_HTTP_API = true` and provide the HTTP endpoints below.

Expected methods:

```js
window.FileManagerBackend = {
  search(query),
  saveSetup(settings),
  saveSettings(settings),
  saveKeywords(fileId, keywords),
  openFile(path)
};
```

Expected HTTP endpoints:

```text
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

`search(query)` should return:

```json
{
  "quickResults": [
    {
      "id": "q-1",
      "name": "example.pdf",
      "path": "C:\\Users\\LG\\Downloads\\example.pdf",
      "modified": "2026-05-30 14:34",
      "extension": ".pdf",
      "tags": ["paper"]
    }
  ],
  "smartResults": [
    {
      "id": "s-1",
      "name": "semantic-result.pdf",
      "path": "C:\\Users\\LG\\Documents\\semantic-result.pdf",
      "modified": "2026-05-28 09:10",
      "extension": ".pdf",
      "confidence": "91%",
      "tags": ["semantic"]
    }
  ],
  "parsedQuery": ["Type: PDF", "Time: last week", "Mode: hybrid"],
  "timings": {
    "quick": "0.08s",
    "smart": "0.74s"
  }
}
```
