#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Run a command inside a Visual Studio developer environment.

.DESCRIPTION
  The repository's other build scripts assume the caller already has a Visual
  Studio developer environment loaded: cl.exe is on PATH and, more importantly,
  INCLUDE/LIB point at the MSVC and Windows SDK directories. When CMake invokes
  cl.exe without those variables set, every #include of a standard header fails
  with "cannot open include file: 'cstdint'" even though the compiler exists and
  the project has built successfully many times before. This script loads
  vcvars64.bat and then runs the supplied command in that environment, so
  builds work from a plain shell.

  Only the environment is inherited; nothing in the repository is modified.

.EXAMPLE
  ./scripts/with-msvc-env.ps1 cmake --build build-win64
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Command,
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$CommandArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Candidate locations, newest toolset first. A machine with several VS
# installations should use the newest one, which is what a developer would
# have open in their IDE.
$candidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)

$vcvars = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) {
    $found = Get-ChildItem -Path 'C:\Program Files*\Microsoft Visual Studio' -Filter 'vcvars64.bat' `
        -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $vcvars = $found.FullName
    }
}

if (-not $vcvars) {
    Write-Error "vcvars64.bat not found. Install Visual Studio 2022 (or the Build Tools) with the C++ workload."
    exit 2
}

Write-Host "Loading MSVC environment from: $vcvars" -ForegroundColor DarkGray

# vcvars64.bat mutates the environment of the cmd session it runs in, so capture
# the resulting variables by printing them and re-importing them here.
$raw = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"

$envMap = @{}
foreach ($line in $raw) {
    if ($line -match '^([^=]+)=(.*)$') {
        $envMap[$Matches[1]] = $Matches[2]
    }
}

foreach ($key in $envMap.Keys) {
    [Environment]::SetEnvironmentVariable($key, $envMap[$key], 'Process')
}

$resolved = Get-Command $Command -ErrorAction SilentlyContinue
if (-not $resolved) {
    Write-Error "Command not found on PATH after loading the MSVC environment: $Command"
    exit 2
}

Write-Host "Running: $Command $($CommandArgs -join ' ')" -ForegroundColor Cyan
& $Command @CommandArgs
exit $LASTEXITCODE
