<#
.SYNOPSIS
    Smoke-loads every internal page and fails on any JavaScript console error.

.DESCRIPTION
    The internal pages are plain HTML/JS with no build step and no test harness,
    so a syntax error or a bad null dereference in one of them only shows up as a
    blank page in the GUI. There is no JS engine on the build machine to lint or
    parse them with.

    The browser already forwards every page's console output to stderr as
    "[CONSOLE:LEVEL] message (line N, source)" (Tab::OnAddConsoleMessage). This
    script launches the browser once per page with UL_START_URL pointing at it,
    and fails if that page logged anything at error level. That turns "open it
    and look" into something automated.

    UL_START_URL is used rather than the session file because session restore
    deliberately ignores file:/// pages (GetMeaningfulSavedTabCount filters them
    out as internal), so a session pointing at settings.html is never restored.

.PARAMETER Pages
    Page names to check, without the .html suffix. Defaults to every page under
    assets.

.PARAMETER SettleSeconds
    How long to let each page run before killing the browser.

.PARAMETER BuildDir
    Build directory containing the executable and its data directory.
#>
[CmdletBinding()]
param(
    [string[]] $Pages,
    [int] $SettleSeconds = 8,
    [string] $BuildDir = 'build-win64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $build 'Ultralight-WebBrowser.exe'

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "Browser executable not found: $exe"
    exit 1
}

# The browser loads assets from <build>/assets, which CMake only refreshes as a
# post-build step of relinking the exe. After editing a page without touching
# C++, that copy is stale and this script would check the old file. Mirror the
# copy here so the check always sees the working tree.
$repoAssets = Join-Path $root 'assets'
$buildAssets = Join-Path $build 'assets'
if (Test-Path -LiteralPath $repoAssets) {
    # Copy the contents, not the directory itself: pointing Copy-Item at an
    # existing directory would nest it as assets\assets and silently copy
    # nothing over the top.
    if (-not (Test-Path -LiteralPath $buildAssets)) {
        New-Item -ItemType Directory -Path $buildAssets -Force | Out-Null
    }
    Copy-Item -Path (Join-Path $repoAssets '*') -Destination $buildAssets -Recurse -Force
}

if (-not $Pages) {
    $Pages = Get-ChildItem (Join-Path $root 'assets') -Filter *.html |
        ForEach-Object { $_.BaseName } |
        Sort-Object
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("ul_pagecheck_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

$problems = 0
$checked = 0

try {
    foreach ($page in $Pages) {
        $errFile = Join-Path $tmp "$page.err"
        $outFile = Join-Path $tmp "$page.out"

        $env:UL_START_URL = "file:///$page.html"
        $proc = Start-Process -FilePath $exe -PassThru -WorkingDirectory $build `
            -RedirectStandardError $errFile -RedirectStandardOutput $outFile
        if (-not $proc.WaitForExit($SettleSeconds * 1000)) {
            $proc.Kill()
            $proc.WaitForExit()
        }
        Remove-Item Env:\UL_START_URL -ErrorAction SilentlyContinue

        $checked++
        $errors = @()
        if (Test-Path -LiteralPath $errFile) {
            $errors = Select-String -Path $errFile -Pattern 'CONSOLE:ERROR' | ForEach-Object { $_.Line }
        }

        if ($errors.Count -gt 0) {
            $problems++
            Write-Output ("  {0,-22} FAIL" -f $page)
            $errors | Select-Object -First 5 | ForEach-Object { Write-Output "      $_" }
        }
        else {
            Write-Output ("  {0,-22} ok" -f $page)
        }
    }
}
finally {
    Remove-Item Env:\UL_START_URL -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output ""
Write-Output "pages checked: $checked, with console errors: $problems"

if ($problems -gt 0) {
    Write-Output "PAGE CHECK FAILED"
    exit 1
}
Write-Output "PAGE CHECK PASSED"
exit 0