[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $InstallPath,
    [string] $ArchivePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# These upstream hook implementations and their IPC protocol match our fork. Our
# hook-directory security changes run in the host plugin, not these six helpers.
$version = '33.0.1'
$archiveHash = '0d9436451c2d772b33428e3fdae9776f071895c335de17283176306fb72651d3'
$archiveName = "OBS-Studio-${version}-Windows-x64.zip"
$installRoot = (Resolve-Path -LiteralPath $InstallPath).Path
$capturePath = Join-Path $installRoot 'core/win-capture/data'
$cachePath = Join-Path (Split-Path -Parent $installRoot) "signed-game-capture-${version}"
$binaries = @(
    'get-graphics-offsets32.exe', 'get-graphics-offsets64.exe',
    'graphics-hook32.dll', 'graphics-hook64.dll',
    'inject-helper32.exe', 'inject-helper64.exe'
)

foreach ($name in $binaries) {
    if (-not (Test-Path -LiteralPath (Join-Path $capturePath $name) -PathType Leaf)) {
        throw "Capture helper is missing from the install tree: ${name}"
    }
}

New-Item -ItemType Directory -Path $cachePath -Force | Out-Null
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $cachePath $archiveName
    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        $downloadPath = "${ArchivePath}.download"
        Invoke-WebRequest -Uri "https://github.com/obsproject/obs-studio/releases/download/${version}/${archiveName}" `
            -OutFile $downloadPath -MaximumRetryCount 3 -RetryIntervalSec 2
        if ((Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash -ne $archiveHash) {
            throw 'Downloaded OBS capture archive hash does not match.'
        }
        Move-Item -LiteralPath $downloadPath -Destination $ArchivePath
    }
}

if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $archiveHash) {
    throw 'OBS capture archive hash does not match.'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath).Path)
try {
    foreach ($name in $binaries) {
        $entry = $archive.GetEntry("core/win-capture/data/${name}")
        if (-not $entry) {
            throw "Official OBS archive is missing capture helper: ${name}"
        }
        $stagedPath = Join-Path $cachePath $name
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $stagedPath, $true)
        $signature = Get-AuthenticodeSignature -LiteralPath $stagedPath
        if ($signature.Status -ne 'Valid') {
            throw "Capture helper signature is not valid: ${name} ($($signature.Status))"
        }
        $publisher = $signature.SignerCertificate.GetNameInfo(
            [System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false
        )
        if ($publisher -ne 'OBS Project, LLC') {
            throw "Unexpected capture helper publisher: ${name} (${publisher})"
        }
    }
} finally {
    $archive.Dispose()
}

foreach ($name in $binaries) {
    Copy-Item -LiteralPath (Join-Path $cachePath $name) -Destination (Join-Path $capturePath $name) -Force
    Write-Host "Installed OBS ${version} signed capture helper: ${name}"
}
