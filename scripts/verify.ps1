# Regression gate for Ultralight-WebBrowser.
#
# Build, run the four unit suites, then launch the browser N times and require
# every launch to survive. The launch check exists because a regression shipped
# earlier (a resource_path_prefix override) passed the build and every test but
# exited within ~15s at runtime. Nothing in the test suite covers startup.
#
# Usage: powershell -File scripts/verify.ps1 [-Launches 6] [-SkipBuild]

param(
  [int]$Launches = 6,
  [int]$SettleSeconds = 14,
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-win64'
$exe = Join-Path $build 'Ultralight-WebBrowser.exe'
$tests = @('UtilsTest', 'MemoryMonitorTest', 'ThemeManagerTest', 'MediaFallbackTest', 'DownloadManagerTest')
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'

$fail = 0

function Step($name) { Write-Output ""; Write-Output "== $name" }

Step 'Build'
if (-not $SkipBuild) {
  # Ninja needs the MSVC environment for the STL headers, so build through
  # vcvars64 rather than relying on an already-initialised shell.
  $out = cmd /c "call `"$vcvars`" >nul 2>&1 && cmake --build `"$build`"" 2>&1
  $bad = $out | Select-String -Pattern 'error C|FAILED|ninja: build stopped'
  if ($bad) {
    $bad | Select-Object -First 10 | ForEach-Object { Write-Output "   $_" }
    Write-Output 'BUILD FAILED'
    exit 1
  }
  Write-Output '   ok'
} else {
  Write-Output '   skipped'
}

Step 'Unit tests'
# Tests print intentional diagnostics to stderr (e.g. ThemeManagerTest logs each
# rejected input). Redirect to a file: PowerShell surfaces native stderr as a
# terminating NativeCommandError under ErrorActionPreference=Stop, which would
# abort the run on a passing test.
$log = Join-Path $env:TEMP 'ul_verify_tests.log'
foreach ($t in $tests) {
  $p = Join-Path $build "$t.exe"
  if (-not (Test-Path $p)) { Write-Output "   $t : MISSING"; $fail++; continue }
  $proc = Start-Process -FilePath $p -PassThru -Wait -NoNewWindow `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err"
  if ($proc.ExitCode -eq 0) { Write-Output "   $t : pass" }
  else {
    Write-Output "   $t : FAIL ($($proc.ExitCode))"
    Get-Content "$log.err" -ErrorAction SilentlyContinue |
      Select-Object -Last 5 | ForEach-Object { Write-Output "      $_" }
    $fail++
  }
}

Step "Startup ($Launches launches, ${SettleSeconds}s each)"
if (-not (Test-Path $exe)) { Write-Output '   executable missing'; exit 1 }

Get-Process Ultralight-WebBrowser -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700

$alive = 0; $dead = 0
for ($i = 1; $i -le $Launches; $i++) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  # The default FileSystem resolves resources against the working directory,
  # so launching from the build dir is the supported configuration.
  $psi.WorkingDirectory = $build
  $psi.UseShellExecute = $false
  $psi.RedirectStandardError = $true
  $pr = [System.Diagnostics.Process]::Start($psi)
  $null = $pr.WaitForExit($SettleSeconds * 1000)
  if ($pr.HasExited) {
    $dead++
    Write-Output "   launch ${i}: EXITED code=$($pr.ExitCode)"
    $err = $pr.StandardError.ReadToEnd()
    if ($err.Trim()) { Write-Output "      stderr: $($err.Trim())" }
  } else {
    $alive++
    Write-Output "   launch ${i}: alive"
    $pr.Kill()
  }
  Start-Sleep -Milliseconds 900
}
Write-Output "   alive=$alive exited=$dead"
if ($dead -gt 0) { $fail++ }

Write-Output ""
if ($fail -eq 0) { Write-Output 'VERIFY PASSED'; exit 0 }
Write-Output "VERIFY FAILED ($fail problem(s))"
exit 1