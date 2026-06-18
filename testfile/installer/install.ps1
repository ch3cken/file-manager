param(
    [string]$InstallDir = "$env:LOCALAPPDATA\FileManager",
    [switch]$NoDesktopShortcut
)

$ErrorActionPreference = 'Stop'

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$payloadZip = Join-Path $sourceDir 'payload.zip'

if (-not (Test-Path -LiteralPath $payloadZip)) {
    throw "Installer payload was not found: $payloadZip"
}

$installDirFull = [System.IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($InstallDir))
$localAppDataFull = [System.IO.Path]::GetFullPath($env:LOCALAPPDATA)

if (-not $installDirFull.StartsWith($localAppDataFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "This installer writes only under LocalAppData by default. Requested path: $installDirFull"
}

$stagingDir = Join-Path ([System.IO.Path]::GetTempPath()) ('FileManager-install-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagingDir | Out-Null

try {
    Expand-Archive -LiteralPath $payloadZip -DestinationPath $stagingDir -Force

    if (Test-Path -LiteralPath $installDirFull) {
        Remove-Item -LiteralPath $installDirFull -Recurse -Force
    }
    New-Item -ItemType Directory -Path $installDirFull | Out-Null

    Get-ChildItem -LiteralPath $stagingDir -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $installDirFull -Recurse -Force
    }

    $exePath = Join-Path $installDirFull 'FileManager.exe'
    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "Installed executable was not found: $exePath"
    }

    $shell = New-Object -ComObject WScript.Shell
    $programsDir = [Environment]::GetFolderPath('Programs')
    $startMenuDir = Join-Path $programsDir 'FileManager'
    New-Item -ItemType Directory -Path $startMenuDir -Force | Out-Null

    $startMenuShortcut = $shell.CreateShortcut((Join-Path $startMenuDir 'FileManager.lnk'))
    $startMenuShortcut.TargetPath = $exePath
    $startMenuShortcut.Arguments = ''
    $startMenuShortcut.WorkingDirectory = $installDirFull
    $startMenuShortcut.Description = 'FileManager local search'
    $startMenuShortcut.Save()

    if (-not $NoDesktopShortcut) {
        $desktopShortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'FileManager.lnk'))
        $desktopShortcut.TargetPath = $exePath
        $desktopShortcut.Arguments = ''
        $desktopShortcut.WorkingDirectory = $installDirFull
        $desktopShortcut.Description = 'FileManager local search'
        $desktopShortcut.Save()
    }

    Start-Process -FilePath $exePath -WorkingDirectory $installDirFull
}
finally {
    if (Test-Path -LiteralPath $stagingDir) {
        Remove-Item -LiteralPath $stagingDir -Recurse -Force
    }
}
