[CmdletBinding()]
param(
    [string] $Target,
    [string] $Configuration = 'Release',
    [string] $OutputDirectory = "$PSScriptRoot/output"
)

$ErrorActionPreference = 'Stop'

# Check prerequisites
if (-not (Test-Path $env:CI)) {
    throw "This script requires a CI environment."
}

if (-not (Get-Command 7z -ErrorAction SilentlyContinue)) {
    throw "7z (7-Zip) is required but not installed or not in PATH."
}

# Define function for archiving
function Archive-Files {
    param(
        [string] $SourcePath,
        [string] $OutputFile
    )

    if (-not (Test-Path $SourcePath)) {
        throw "Source path '$SourcePath' does not exist."
    }

    Write-Host "Archiving '$SourcePath' to '$OutputFile'"
    $Command = "7z a -r $OutputFile $SourcePath"
    $Result = Invoke-Expression $Command

    if ($LastExitCode -ne 0) {
        throw "Failed to create archive. Exit code: $LastExitCode"
    }

    Write-Host "Archive created: $OutputFile"
}

# Create output directory if not exists
if (-not (Test-Path $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

# Construct paths
$SourcePath = "$PSScriptRoot/build/$Target/$Configuration"
$OutputFile = Join-Path -Path $OutputDirectory -ChildPath "obs-studio-$Target-$Configuration.7z"

# Run the archiving process
Archive-Files -SourcePath $SourcePath -OutputFile $OutputFile
