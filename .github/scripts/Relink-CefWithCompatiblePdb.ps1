#Requires -Version 7.2

<#
.SYNOPSIS
Relinks an existing Chromium/CEF libcef.dll with 4 KiB PDB pages.

.DESCRIPTION
Chromium 128 writes 8 KiB PDB pages by default. Older pdbcopy readers reject
those PDBs with EC_FORMAT. This helper obtains the exact libcef link command
from Ninja, copies (but never modifies) libcef.dll.rsp, changes its one
/pdbpagesize:8192 flag to /pdbpagesize:4096, and relinks into a separate output
directory. It verifies the relinked private PDB has 4 KiB blocks, then invokes
Create-CefPublicPdb.ps1, which validates the public PDB's GUID, age, stripped
state, and configured size limit.

No build target is run: `ninja -t commands` is read-only, all linker output is
redirected to OutputDirectory, and the original response file is hash-checked
before and after the relink.
#>

[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $CefReleaseBuildDirectory,

  [Parameter(Mandatory)]
  [string] $OutputDirectory,

  [string] $NinjaPath,

  [string] $LinkerPath,

  [string] $PdbCopyPath,

  [string] $LlvmPdbUtilPath,

  [string] $NinjaTarget = 'libcef.dll',

  [ValidateRange(1, [long]::MaxValue)]
  [long] $MaximumPublicPdbBytes = 2147483647,

  [switch] $ReplaceExistingOutputs,

  [switch] $KeepTemporaryResponseFile
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
    if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
      throw "The requested $Name does not exist: $RequestedPath"
    }
    return (Resolve-Path -LiteralPath $RequestedPath).Path
  }

  $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
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

function Test-IsSameOrChildPath {
  param(
    [Parameter(Mandatory)]
    [string] $Candidate,

    [Parameter(Mandatory)]
    [string] $Parent
  )

  $candidatePath = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
  $parentPath = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
  $comparison = [System.StringComparison]::OrdinalIgnoreCase
  return $candidatePath.Equals($parentPath, $comparison) -or
    $candidatePath.StartsWith("$parentPath$([System.IO.Path]::DirectorySeparatorChar)", $comparison)
}

function Resolve-ExistingPhysicalPath {
  param(
    [Parameter(Mandatory)]
    [string] $Path
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path)
  if (-not (Test-Path -LiteralPath $fullPath)) {
    throw "Cannot canonicalize a path that does not exist: $fullPath"
  }

  $root = [System.IO.Path]::GetPathRoot($fullPath)
  $remainder = $fullPath.Substring($root.Length).TrimStart('\', '/')
  $current = $root
  if ($remainder) {
    foreach ($component in ($remainder -split '[\\/]+')) {
      $current = Join-Path $current $component
      $item = Get-Item -LiteralPath $current -Force
      if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $target = $item.ResolveLinkTarget($true)
        if ($null -eq $target) {
          throw "Could not resolve reparse point while canonicalizing: $current"
        }
        $current = $target.FullName
      }
    }
  }
  return [System.IO.Path]::GetFullPath($current)
}

function Resolve-ProspectivePhysicalPath {
  param(
    [Parameter(Mandatory)]
    [string] $Path
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path)
  $missingComponents = [System.Collections.Generic.List[string]]::new()
  $existingPath = $fullPath
  while (-not (Test-Path -LiteralPath $existingPath)) {
    $leaf = Split-Path -Leaf $existingPath
    if (-not $leaf) {
      throw "Could not find an existing parent while canonicalizing: $fullPath"
    }
    $missingComponents.Add($leaf)
    $parent = Split-Path -Parent $existingPath
    if (-not $parent -or $parent -eq $existingPath) {
      throw "Could not find an existing parent while canonicalizing: $fullPath"
    }
    $existingPath = $parent
  }

  $physicalPath = Resolve-ExistingPhysicalPath -Path $existingPath
  for ($index = $missingComponents.Count - 1; $index -ge 0; $index--) {
    $physicalPath = Join-Path $physicalPath $missingComponents[$index]
  }
  return [System.IO.Path]::GetFullPath($physicalPath)
}

function Assert-OutputOutsideBuild {
  param(
    [Parameter(Mandatory)]
    [string] $Candidate,

    [Parameter(Mandatory)]
    [string] $CefOutCanonical,

    [Parameter(Mandatory)]
    [string] $Description
  )

  $candidateCanonical = Resolve-ProspectivePhysicalPath -Path $Candidate
  if ((Test-IsSameOrChildPath -Candidate $candidateCanonical -Parent $CefOutCanonical) -or
      (Test-IsSameOrChildPath -Candidate $CefOutCanonical -Parent $candidateCanonical)) {
    throw "$Description resolves to or overlaps the CEF build directory: $candidateCanonical"
  }
  return $candidateCanonical
}

function Assert-GeneratedOutputParentsOutsideBuild {
  param(
    [Parameter(Mandatory)]
    [string[]] $GeneratedPaths,

    [Parameter(Mandatory)]
    [string] $CefOutCanonical
  )

  foreach ($generatedPath in $GeneratedPaths) {
    $parent = Split-Path -Parent $generatedPath
    $parentCanonical = Resolve-ExistingPhysicalPath -Path $parent
    if (Test-IsSameOrChildPath -Candidate $parentCanonical -Parent $CefOutCanonical) {
      throw "Generated output parent resolves inside the CEF build directory: $generatedPath -> $parentCanonical"
    }
  }
}

function Resolve-PathFromBuildDirectory {
  param(
    [Parameter(Mandatory)]
    [string] $Path,

    [Parameter(Mandatory)]
    [string] $BuildDirectory
  )

  $candidate = if ([System.IO.Path]::IsPathRooted($Path)) {
    $Path
  } else {
    Join-Path $BuildDirectory $Path
  }
  $fullPath = [System.IO.Path]::GetFullPath($candidate)
  if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
    throw "The path reported by Ninja does not exist: $fullPath"
  }
  return (Resolve-Path -LiteralPath $fullPath).Path
}

function Get-PdbBlockSize {
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
  $match = [regex]::Match(($summary -join "`n"), '(?m)^\s*Block Size:\s*(\d+)\s*$')
  if (-not $match.Success) {
    throw "llvm-pdbutil returned no recognizable PDB block size for '$PdbPath'."
  }
  return [int] $match.Groups[1].Value
}

function Split-WindowsCommandLine {
  param(
    [Parameter(Mandatory)]
    [string] $CommandLine
  )

  if (-not ('CefPdbRelink.NativeCommandLine' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace CefPdbRelink {
  public static class NativeCommandLine {
    [DllImport("shell32.dll", SetLastError = true)]
    public static extern IntPtr CommandLineToArgvW(
        [MarshalAs(UnmanagedType.LPWStr)] string lpCmdLine,
        out int pNumArgs);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LocalFree(IntPtr hMem);
  }
}
'@
  }

  $argumentCount = 0
  $argv = [CefPdbRelink.NativeCommandLine]::CommandLineToArgvW($CommandLine, [ref] $argumentCount)
  if ($argv -eq [IntPtr]::Zero -or $argumentCount -lt 1) {
    throw "Could not parse Ninja's command line: $CommandLine"
  }

  try {
    $arguments = for ($index = 0; $index -lt $argumentCount; $index++) {
      $argumentPointer = [Runtime.InteropServices.Marshal]::ReadIntPtr(
        $argv, $index * [IntPtr]::Size)
      [Runtime.InteropServices.Marshal]::PtrToStringUni($argumentPointer)
    }
    return @($arguments)
  } finally {
    [void] [CefPdbRelink.NativeCommandLine]::LocalFree($argv)
  }
}

function Get-OptionValue {
  param(
    [Parameter(Mandatory)]
    [string] $Argument,

    [Parameter(Mandatory)]
    [string] $OptionName
  )

  if (-not $Argument.StartsWith("/${OptionName}:", [System.StringComparison]::OrdinalIgnoreCase)) {
    return $null
  }
  return $Argument.Substring($OptionName.Length + 2)
}

function Write-CommandDiagnostic {
  param(
    [Parameter(Mandatory)]
    [string] $Executable,

    [Parameter(Mandatory)]
    [string[]] $Arguments
  )

  $formatArgument = {
    param([string] $Argument)
    if ($Argument -match '[\s"]') {
      return '"' + $Argument.Replace('"', '\"') + '"'
    }
    return $Argument
  }
  $rendered = @($Executable) + @($Arguments | ForEach-Object { & $formatArgument $_ })
  Write-Host ("Relinking with: " + ($rendered -join ' '))
}

$cefOut = (Resolve-Path -LiteralPath $CefReleaseBuildDirectory).Path
$cefOutCanonical = Resolve-ExistingPhysicalPath -Path $cefOut
$cefSource = (Resolve-Path -LiteralPath (Join-Path $cefOut '..\..')).Path
$outputFull = [System.IO.Path]::GetFullPath($OutputDirectory)
$outputParent = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
  throw "The output directory's parent does not exist: $outputParent"
}

$null = Assert-OutputOutsideBuild -Candidate $outputFull -CefOutCanonical $cefOutCanonical -Description 'OutputDirectory'

if (Test-Path -LiteralPath $outputFull) {
  if (-not (Test-Path -LiteralPath $outputFull -PathType Container)) {
    throw "OutputDirectory exists but is not a directory: $outputFull"
  }
  if (-not $ReplaceExistingOutputs -and (Get-ChildItem -LiteralPath $outputFull -Force | Select-Object -First 1)) {
    throw "OutputDirectory is not empty. Use a fresh directory or pass -ReplaceExistingOutputs: $outputFull"
  }
} else {
  New-Item -ItemType Directory -Path $outputFull | Out-Null
}
$output = (Resolve-Path -LiteralPath $outputFull).Path
$outputCanonical = Resolve-ExistingPhysicalPath -Path $output
$null = Assert-OutputOutsideBuild -Candidate $outputCanonical -CefOutCanonical $cefOutCanonical -Description 'OutputDirectory after creation'

$relinkedDll = Join-Path $output 'libcef.dll'
$relinkedImportLibrary = Join-Path $output 'libcef.dll.lib'
$privatePdb = Join-Path $output 'libcef.dll.pdb'
$publicPdb = Join-Path $output 'libcef.dll.public.pdb'
$temporaryRsp = Join-Path $output 'libcef.dll.pdbpagesize-4096.rsp'
$generatedOutputs = @($relinkedDll, $relinkedImportLibrary, $privatePdb, $publicPdb, $temporaryRsp)
Assert-GeneratedOutputParentsOutsideBuild -GeneratedPaths $generatedOutputs -CefOutCanonical $cefOutCanonical
if (-not $ReplaceExistingOutputs) {
  $existingGeneratedOutput = $generatedOutputs | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  if ($existingGeneratedOutput) {
    throw "Refusing to overwrite an existing relink output. Pass -ReplaceExistingOutputs to replace it: $existingGeneratedOutput"
  }
}

$ninja = Resolve-Executable -Name 'ninja.exe' -RequestedPath $NinjaPath -Candidates @(
  (Join-Path $cefSource 'third_party\ninja\ninja.exe'),
  (Join-Path $cefSource 'third_party\depot_tools\ninja.exe'),
  (Join-Path $cefSource 'third_party\depot_tools\ninja.bat')
)
$ninjaOutput = @(& $ninja -C $cefOut -t commands $NinjaTarget 2>&1)
if ($LASTEXITCODE -ne 0) {
  throw "Ninja could not print commands for '$NinjaTarget':`n$($ninjaOutput -join "`n")"
}

$linkCandidates = foreach ($lineObject in $ninjaOutput) {
  $line = $lineObject.ToString()
  if ($line -notmatch '(?i)lld-link(?:\.exe)?') {
    continue
  }

  $tokens = Split-WindowsCommandLine -CommandLine $line
  if ($tokens.Count -lt 2 -or $tokens[0] -notmatch '(?i)lld-link(?:\.exe)?$') {
    continue
  }

  $outArguments = @($tokens | Where-Object { (Get-OptionValue -Argument $_ -OptionName 'OUT') })
  $pdbArguments = @($tokens | Where-Object { (Get-OptionValue -Argument $_ -OptionName 'PDB') })
  $implibArguments = @($tokens | Where-Object { (Get-OptionValue -Argument $_ -OptionName 'IMPLIB') })
  $responseArguments = @($tokens | Where-Object {
      $_.StartsWith('@') -and
      ([System.IO.Path]::GetFileName($_.Substring(1)) -ieq 'libcef.dll.rsp')
    })

  if ($outArguments.Count -ne 1 -or $pdbArguments.Count -ne 1 -or
      $implibArguments.Count -ne 1 -or $responseArguments.Count -ne 1) {
    continue
  }
  if ([System.IO.Path]::GetFileName((Get-OptionValue -Argument $outArguments[0] -OptionName 'OUT')) -ine 'libcef.dll') {
    continue
  }

  [pscustomobject]@{
    Line = $line
    Tokens = $tokens
    ResponseArgument = $responseArguments[0]
  }
}

if ($linkCandidates.Count -ne 1) {
  $lldLines = @($ninjaOutput | Where-Object { $_.ToString() -match '(?i)lld-link(?:\.exe)?' })
  throw (
    "Expected exactly one libcef lld-link command from Ninja; found $($linkCandidates.Count). " +
    "Pass -NinjaTarget if this build uses another target. lld-link lines were:`n$($lldLines -join "`n")"
  )
}

$linkCandidate = $linkCandidates[0]
$ninjaLinker = Resolve-PathFromBuildDirectory -Path $linkCandidate.Tokens[0] -BuildDirectory $cefOut
$linker = Resolve-Executable -Name 'lld-link.exe' -RequestedPath $LinkerPath -Candidates @($ninjaLinker)

$sourceRspArgument = $linkCandidate.ResponseArgument.Substring(1)
$sourceRsp = Resolve-PathFromBuildDirectory -Path $sourceRspArgument -BuildDirectory $cefOut
if (-not (Test-Path -LiteralPath $sourceRsp -PathType Leaf)) {
  throw "The response file selected from Ninja does not exist: $sourceRsp"
}
$sourceRspHashBefore = (Get-FileHash -LiteralPath $sourceRsp -Algorithm SHA256).Hash

$sourceRspText = [System.IO.File]::ReadAllText($sourceRsp)
$oldPageSize = [regex]::Matches($sourceRspText, '(?i)/pdbpagesize:8192\b')
$newPageSize = [regex]::Matches($sourceRspText, '(?i)/pdbpagesize:4096\b')
if ($oldPageSize.Count -ne 1 -or $newPageSize.Count -ne 0) {
  throw (
    "Expected exactly one /pdbpagesize:8192 and no existing /pdbpagesize:4096 in '$sourceRsp'; " +
    "found 8192=$($oldPageSize.Count), 4096=$($newPageSize.Count)."
  )
}
$temporaryRspText = [regex]::Replace($sourceRspText, '(?i)/pdbpagesize:8192\b', '/pdbpagesize:4096')
[System.IO.File]::WriteAllText($temporaryRsp, $temporaryRspText, [System.Text.UTF8Encoding]::new($false))

$linkArguments = [System.Collections.Generic.List[string]]::new()
$outCount = 0
$implibCount = 0
$pdbCount = 0
foreach ($argument in $linkCandidate.Tokens | Select-Object -Skip 1) {
  if ((Get-OptionValue -Argument $argument -OptionName 'OUT')) {
    $outCount++
    $linkArguments.Add("/OUT:$relinkedDll")
  } elseif ((Get-OptionValue -Argument $argument -OptionName 'IMPLIB')) {
    $implibCount++
    $linkArguments.Add("/IMPLIB:$relinkedImportLibrary")
  } elseif ((Get-OptionValue -Argument $argument -OptionName 'PDB')) {
    $pdbCount++
    $linkArguments.Add("/PDB:$privatePdb")
  } elseif ($argument -eq $linkCandidate.ResponseArgument) {
    $linkArguments.Add("@$temporaryRsp")
  } else {
    $linkArguments.Add($argument)
  }
}
if ($outCount -ne 1 -or $implibCount -ne 1 -or $pdbCount -ne 1) {
  throw "The selected Ninja command did not contain one each of /OUT, /IMPLIB, and /PDB."
}

try {
  Write-Host "Source response file: $sourceRsp"
  Write-Host "Temporary response file: $temporaryRsp"
  Write-CommandDiagnostic -Executable $linker -Arguments $linkArguments.ToArray()
  Push-Location $cefOut
  try {
    # Invoke lld directly with an argument array. Do not evaluate Ninja's shell text.
    & $linker $linkArguments.ToArray()
    if ($LASTEXITCODE -ne 0) {
      throw "lld-link failed with exit code $LASTEXITCODE."
    }
  } finally {
    Pop-Location
  }

  foreach ($requiredOutput in @($relinkedDll, $relinkedImportLibrary, $privatePdb)) {
    if (-not (Test-Path -LiteralPath $requiredOutput -PathType Leaf)) {
      throw "lld-link did not produce the expected output: $requiredOutput"
    }
  }

  $pdbUtil = Resolve-Executable -Name 'llvm-pdbutil.exe' -RequestedPath $LlvmPdbUtilPath -Candidates @(
    (Join-Path $cefSource 'third_party\llvm-build\Release+Asserts\bin\llvm-pdbutil.exe')
  )
  $privatePdbBlockSize = Get-PdbBlockSize -PdbPath $privatePdb -PdbUtil $pdbUtil
  if ($privatePdbBlockSize -ne 4096) {
    throw "Relinked private PDB has a $privatePdbBlockSize-byte block size, not the required 4096 bytes: $privatePdb"
  }
  Write-Host "Verified private PDB block size: $privatePdbBlockSize"

  $createPublicPdb = Join-Path $PSScriptRoot 'Create-CefPublicPdb.ps1'
  if (-not (Test-Path -LiteralPath $createPublicPdb -PathType Leaf)) {
    throw "The public PDB helper is missing: $createPublicPdb"
  }
  $publicPdbArguments = @{
    CefOutDirectory = $output
    LlvmPdbUtilPath = $pdbUtil
    OutputPath = $publicPdb
    MaximumBytes = $MaximumPublicPdbBytes
  }
  if ($PdbCopyPath) {
    $publicPdbArguments.PdbCopyPath = $PdbCopyPath
  }
  if ($ReplaceExistingOutputs) {
    $publicPdbArguments.ReplaceExisting = $true
  }
  & $createPublicPdb @publicPdbArguments

  $publicPdbBlockSize = Get-PdbBlockSize -PdbPath $publicPdb -PdbUtil $pdbUtil
  if ($publicPdbBlockSize -ne 4096) {
    throw "Generated public PDB has a $publicPdbBlockSize-byte block size, not the required 4096 bytes: $publicPdb"
  }
  Write-Host "Verified public PDB block size: $publicPdbBlockSize"

  $sourceRspHashAfter = (Get-FileHash -LiteralPath $sourceRsp -Algorithm SHA256).Hash
  if ($sourceRspHashBefore -ne $sourceRspHashAfter) {
    throw "The source response file changed during relinking: $sourceRsp"
  }

  Get-Item -LiteralPath $relinkedDll, $relinkedImportLibrary, $privatePdb, $publicPdb |
    Select-Object FullName, Length, LastWriteTime
} finally {
  if (-not $KeepTemporaryResponseFile -and (Test-Path -LiteralPath $temporaryRsp -PathType Leaf)) {
    Remove-Item -LiteralPath $temporaryRsp -Force
  }
}
