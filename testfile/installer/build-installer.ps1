param(
    [string]$Runtime = 'win-x64',
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$distDir = Join-Path $projectRoot 'artifacts'
$publishDir = Join-Path ([System.IO.Path]::GetTempPath()) ('FileManager-publish-' + [Guid]::NewGuid().ToString('N'))
$installerPublishDir = Join-Path ([System.IO.Path]::GetTempPath()) ('FileManager-installer-publish-' + [Guid]::NewGuid().ToString('N'))
$workDir = Join-Path $distDir 'installer-work'
$setupExe = Join-Path $distDir 'FileManagerSetup.exe'
$portableZip = Join-Path $distDir 'FileManagerPortable.zip'
$projectPath = Join-Path $projectRoot 'FileManager.Desktop.csproj'
$installerProjectPath = Join-Path $projectRoot 'FileManager.Installer.csproj'

function Assert-UnderProject([string]$PathToCheck) {
    $projectFull = [System.IO.Path]::GetFullPath($projectRoot)
    $targetFull = [System.IO.Path]::GetFullPath($PathToCheck)
    if (-not $targetFull.StartsWith($projectFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the project: $targetFull"
    }
}

Assert-UnderProject $distDir
Assert-UnderProject $workDir

Get-Process FileManagerSetup,iexpress,makecab -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

if (Test-Path -LiteralPath $distDir) {
    Remove-Item -LiteralPath $distDir -Recurse -Force
}

New-Item -ItemType Directory -Path $workDir | Out-Null
New-Item -ItemType Directory -Path $publishDir | Out-Null
New-Item -ItemType Directory -Path $installerPublishDir | Out-Null

dotnet publish $projectPath `
    --configuration $Configuration `
    --runtime $Runtime `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:PublishTrimmed=false `
    --output $publishDir

Get-ChildItem -LiteralPath $publishDir -File -Recurse |
    Where-Object { $_.Extension -in '.pdb', '.xml' } |
    Remove-Item -Force

dotnet publish $installerProjectPath `
    --configuration $Configuration `
    --runtime $Runtime `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:PublishTrimmed=false `
    --output $installerPublishDir

Get-ChildItem -LiteralPath $installerPublishDir -File -Recurse |
    Where-Object { $_.Extension -in '.pdb', '.xml' } |
    Remove-Item -Force

$payloadZip = Join-Path $workDir 'payload.zip'
$payloadItems = Get-ChildItem -LiteralPath $publishDir -Force | ForEach-Object { $_.FullName }
if (-not $payloadItems) {
    throw "Publish produced no files: $publishDir"
}
Compress-Archive -LiteralPath $payloadItems -DestinationPath $payloadZip -Force
Copy-Item -LiteralPath $payloadZip -Destination $portableZip -Force

$installerExe = Join-Path $installerPublishDir 'FileManagerInstaller.exe'
if (-not (Test-Path -LiteralPath $installerExe)) {
    throw "Installer UI was not published: $installerExe"
}
Copy-Item -LiteralPath $installerExe -Destination (Join-Path $workDir 'FileManagerInstaller.exe') -Force

$sedPath = Join-Path $workDir 'FileManagerSetup.sed'
$workDirForSed = $workDir.TrimEnd('\') + '\'

@"
[Version]
Class=IEXPRESS
SEDVersion=3

[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
CAB_CompressionType=MSZIP
CAB_CompressionLevel=3
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles

[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=FileManager has been installed.
TargetName=$setupExe
FriendlyName=FileManager
AppLaunched=FileManagerInstaller.exe
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0="payload.zip"
FILE1="FileManagerInstaller.exe"

[SourceFiles]
SourceFiles0=$workDirForSed

[SourceFiles0]
%FILE0%=
%FILE1%=
"@ | Set-Content -LiteralPath $sedPath -Encoding ASCII

$iexpress = (Get-Command iexpress.exe -ErrorAction Stop).Source
& $iexpress /N /Q $sedPath

$deadline = (Get-Date).AddMinutes(20)
while ((Get-Process iexpress,makecab -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
}

if (-not (Test-Path -LiteralPath $setupExe)) {
    throw "IExpress did not create the installer: $setupExe"
}

Remove-Item -LiteralPath $publishDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $installerPublishDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Installer created: $setupExe"
