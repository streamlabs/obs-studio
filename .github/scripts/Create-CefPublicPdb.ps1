#Requires -Version 7.2

# Chromium links libcef with lld-link. Although that linker advertises
# /PDBSTRIPPED in its help output, Chromium 128's bundled version rejects the
# option as unimplemented. Produce the equivalent matching public PDB with the
# Windows SDK's supported post-link tool instead.

[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $CefOutDirectory,

  [string] $PdbCopyPath,

  [string] $LlvmPdbUtilPath,

  [string] $OutputPath,

  [ValidateRange(1, [long]::MaxValue)]
  [long] $MaximumBytes = 2147483647,

  [switch] $ReplaceExisting
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-Executable {
  param(
    [Parameter(Mandatory)]
    [string] $Name,

    [string] $RequestedPath,

    [string[]] $Candidates = @()
  )

  if ($RequestedPath) {
    return (Resolve-Path -LiteralPath $RequestedPath).Path
  }

  $command = Get-Command $Name -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  foreach ($candidate in $Candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }

  throw "Could not find $Name. Pass its path explicitly."
}

function Get-PdbIdentity {
  param(
    [Parameter(Mandatory)]
    [string] $PdbPath,

    [Parameter(Mandatory)]
    [string] $PdbUtil
  )

  $summary = @(& $PdbUtil dump -summary $PdbPath 2>&1)
  if ($LASTEXITCODE -ne 0) {
    throw "llvm-pdbutil could not inspect '$PdbPath':`n$($summary -join "`n")"
  }

  $summaryText = $summary -join "`n"
  $guid = [regex]::Match($summaryText, '(?m)^\s*GUID:\s*(\{[^}]+\})\s*$')
  $age = [regex]::Match($summaryText, '(?m)^\s*Age:\s*(\d+)\s*$')
  $stripped = [regex]::Match($summaryText, '(?m)^\s*Is stripped:\s*(true|false)\s*$')
  if (-not $guid.Success -or -not $age.Success -or -not $stripped.Success) {
    throw "llvm-pdbutil returned an unrecognized summary for '$PdbPath'."
  }

  return [pscustomobject]@{
    Guid = $guid.Groups[1].Value.ToUpperInvariant()
    Age = [int] $age.Groups[1].Value
    IsStripped = $stripped.Groups[1].Value -eq 'true'
  }
}

$cefOut = (Resolve-Path -LiteralPath $CefOutDirectory).Path
$cefRoot = (Resolve-Path -LiteralPath (Join-Path $cefOut '..')).Path
$cefSource = (Resolve-Path -LiteralPath (Join-Path $cefOut '..\..')).Path
$privatePdb = Join-Path $cefOut 'libcef.dll.pdb'
if (-not (Test-Path -LiteralPath $privatePdb -PathType Leaf)) {
  throw "The CEF private PDB does not exist: $privatePdb"
}

if (-not $OutputPath) {
  $OutputPath = Join-Path $cefOut 'libcef.dll.public.pdb'
} elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
  $OutputPath = Join-Path $cefOut $OutputPath
}
$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
  throw "The public PDB output directory does not exist: $outputDirectory"
}
$publicPdb = [System.IO.Path]::GetFullPath($OutputPath)
if ($publicPdb -eq $privatePdb) {
  throw 'The public PDB output must not overwrite the private PDB.'
}
$relativeOutput = [System.IO.Path]::GetRelativePath($cefOut, $publicPdb)
if (
  [System.IO.Path]::IsPathRooted($relativeOutput) -or
  $relativeOutput -eq '..' -or
  $relativeOutput.StartsWith("..$([System.IO.Path]::DirectorySeparatorChar)") -or
  $relativeOutput.StartsWith("..$([System.IO.Path]::AltDirectorySeparatorChar)")
) {
  throw "The public PDB output must remain inside the CEF output directory: $cefOut"
}
if (Test-Path -LiteralPath $publicPdb) {
  if (-not $ReplaceExisting) {
    throw "The public PDB already exists. Pass -ReplaceExisting to regenerate it: $publicPdb"
  }
  Remove-Item -LiteralPath $publicPdb -Force
}

$windowsKitsPdbCopy = if (${env:ProgramFiles(x86)}) {
  Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Debuggers\x64\pdbcopy.exe'
}
$pdbCopy = Resolve-Executable -Name 'pdbcopy.exe' -RequestedPath $PdbCopyPath -Candidates @($windowsKitsPdbCopy)
$pdbUtil = Resolve-Executable -Name 'llvm-pdbutil.exe' -RequestedPath $LlvmPdbUtilPath -Candidates @(
  (Join-Path $cefSource 'third_party\llvm-build\Release+Asserts\bin\llvm-pdbutil.exe'),
  (Join-Path $cefRoot 'third_party\llvm-build\Release+Asserts\bin\llvm-pdbutil.exe'),
  (Join-Path $env:ProgramFiles 'LLVM\bin\llvm-pdbutil.exe'),
  (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\llvm-pdbutil.exe'),
  (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\llvm-pdbutil.exe'),
  (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\llvm-pdbutil.exe'),
  (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-pdbutil.exe')
)

$pdbCopyOutput = @(& $pdbCopy $privatePdb $publicPdb -p 2>&1)
if ($LASTEXITCODE -ne 0) {
  throw (
    "pdbcopy failed. Older Windows SDK versions cannot read PDBs larger than 4 GiB; " +
    "rebuild CEF with symbol_level=1 first.`n$($pdbCopyOutput -join "`n")"
  )
}
if (-not (Test-Path -LiteralPath $publicPdb -PathType Leaf)) {
  throw "pdbcopy did not produce the requested public PDB: $publicPdb"
}

$privateIdentity = Get-PdbIdentity -PdbPath $privatePdb -PdbUtil $pdbUtil
$publicIdentity = Get-PdbIdentity -PdbPath $publicPdb -PdbUtil $pdbUtil
if ($privateIdentity.Guid -ne $publicIdentity.Guid -or $privateIdentity.Age -ne $publicIdentity.Age) {
  throw 'The public PDB does not have the same GUID and age as the private PDB.'
}
if (-not $publicIdentity.IsStripped) {
  throw 'The generated public PDB is not marked as stripped.'
}

$publicFile = Get-Item -LiteralPath $publicPdb
if ($publicFile.Length -gt $MaximumBytes) {
  throw "The public PDB exceeds the configured size limit of $MaximumBytes bytes: $publicPdb"
}

@($privatePdb, $publicPdb) | ForEach-Object {
  $item = Get-Item -LiteralPath $_
  $hash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
  [pscustomobject]@{
    Path = $item.FullName
    Bytes = $item.Length
    SHA256 = $hash.Hash
    Guid = $privateIdentity.Guid
    Age = $privateIdentity.Age
    Public = $_ -eq $publicPdb
  }
} | Format-Table -AutoSize
