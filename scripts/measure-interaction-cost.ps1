#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Measure CPU cost while the browser handles synthetic pointer input.

.DESCRIPTION
  Paint cost, idle cost and input-handler cost can all look healthy while the UI
  still feels sluggish, because each of them measures one slice of a frame. What
  is missing is the total: how much CPU the process actually burns while the user
  interacts. This drives wheel and mouse-move traffic at the foreground window
  and reports process CPU per second alongside the app's own verbose diagnostics
  (frame rate and per-handler timing), so the three can be compared directly.

  Deliberately sends no clicks. A click on the settings page toggles whichever
  setting is under the cursor and auto-saves it to the user profile, which is not
  something a measurement script should do to someone's browser.

.EXAMPLE
  ./scripts/measure-interaction-cost.ps1 -Seconds 12
#>
[CmdletBinding()]
param(
    [int]$Seconds = 12,
    [string]$BuildDir = "build-win64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Probe {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, int dwData, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int c);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint MOUSEEVENTF_WHEEL = 0x0800;
    public const uint WM_NULL = 0x0000;
    public const uint SMTO_ABORTIFHUNG = 0x0002;
}
'@

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
$exe = Join-Path $root (Join-Path $BuildDir 'Ultralight-WebBrowser.exe')
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "Browser executable not found: $exe"
    exit 2
}

$errLog = Join-Path ([IO.Path]::GetTempPath()) ("ul-interaction-" + [Guid]::NewGuid().ToString('N') + ".err")

$env:ULTRALIGHT_VERBOSE = '1'
Write-Host "Launching with ULTRALIGHT_VERBOSE=1" -ForegroundColor Cyan
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) `
    -RedirectStandardError $errLog -PassThru -WindowStyle Normal

# Wait for the real window; the returned object may be a launcher whose PID
# differs from the window owner.
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(40)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $live = Get-Process -Name 'Ultralight-WebBrowser' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($live) {
        $live.Refresh()
        if ($live.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $live.MainWindowHandle; break }
    }
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Warning "no window handle found"; exit 3 }

# Raise the window above everything for the duration. Wheel messages go to
# whatever window is under the cursor regardless of focus, so on a real desktop
# they land on another application unless the browser is actually on top --
# which produces a run that reports the browser's idle cost while looking like
# it measured interaction. WindowFromPoint below verifies that directly instead
# of assuming it.
[Probe]::ShowWindow($hwnd, 9) | Out-Null
[Probe]::SetWindowPos($hwnd, [Probe]::HWND_TOPMOST, 0, 0, 0, 0,
    [Probe]::SWP_NOMOVE -bor [Probe]::SWP_NOSIZE) | Out-Null
[Probe]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Seconds 2

$live = Get-Process -Name 'Ultralight-WebBrowser' | Select-Object -First 1
$live.Refresh()

# Aim at the middle of the window's client area, below the toolbar.
$wr = New-Object Probe+RECT
if ([Probe]::GetWindowRect($hwnd, [ref]$wr)) {
    $cx = [int](($wr.Left + $wr.Right) / 2)
    $cy = [int]($wr.Top + (($wr.Bottom - $wr.Top) * 0.6))
} else {
    $cx = 700; $cy = 520
}
[Probe]::SetCursorPos($cx, $cy) | Out-Null
Start-Sleep -Milliseconds 400

$under = [Probe]::WindowFromPoint((New-Object Probe+POINT -Property @{ X = $cx; Y = $cy }))
Write-Host "window=$hwnd pid=$($live.Id) cursor=($cx,$cy) windowUnderCursor=$under" -ForegroundColor Green
if ($under -ne $hwnd) {
    Write-Warning "The browser is NOT the window under the cursor; wheel input would go elsewhere."
    Write-Warning "Results below would measure idle cost, not interaction cost. Aborting."
    [Probe]::SetWindowPos($hwnd, [Probe]::HWND_NOTOPMOST, 0, 0, 0, 0,
        [Probe]::SWP_NOMOVE -bor [Probe]::SWP_NOSIZE) | Out-Null
    exit 4
}

$cores = [Environment]::ProcessorCount
function Cpu-Since($p, $since) { $p.Refresh(); return ($p.CPU - $since) }

$live.Refresh(); $c0 = $live.CPU
Start-Sleep -Seconds 5
$live.Refresh(); $cIdleEnd = $live.CPU
$idleRate = ($cIdleEnd - $c0) / 5.0
Write-Host ("idle     : {0:N3} CPU-s/s  ({1:N1}% of one core)" -f $idleRate, (100.0 * $idleRate)) -ForegroundColor Yellow

$live.Refresh(); $c1 = $live.CPU
$sw = [Diagnostics.Stopwatch]::StartNew()
$lastReport = 0
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    for ($i = 0; $i -lt 10; $i++) {
        [Probe]::mouse_event([Probe]::MOUSEEVENTF_WHEEL, 0, 0, -120, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 10
        [Probe]::SetCursorPos(($cx + ($i * 9)), ($cy + ($i * 5))) | Out-Null
    }
    if (($sw.Elapsed.TotalSeconds - $lastReport) -ge 4) {
        $lastReport = $sw.Elapsed.TotalSeconds
        $live.Refresh()
        $rate = ($live.CPU - $c1) / $sw.Elapsed.TotalSeconds
        $lag = [IntPtr]::Zero
        $responsive = [Probe]::SendMessageTimeout($hwnd, [Probe]::WM_NULL, [IntPtr]::Zero, [IntPtr]::Zero, [Probe]::SMTO_ABORTIFHUNG, 2000, [ref]$lag) -ne [IntPtr]::Zero
        Write-Host ("t={0,5:N1}s  cpu={1:N3} CPU-s/s ({2:N1}% of one core)  responsive={3}" -f `
            $sw.Elapsed.TotalSeconds, $rate, (100.0 * $rate), $responsive) -ForegroundColor Cyan
    }
}
$sw.Stop()
$live.Refresh()
$loadRate = ($live.CPU - $c1) / $sw.Elapsed.TotalSeconds
Write-Host ""
Write-Host ("interaction: {0:N3} CPU-s/s ({1:N1}% of one core)" -f $loadRate, (100.0 * $loadRate)) -ForegroundColor Yellow
Write-Host ("vs idle     : {0:N1}x more CPU than idle" -f $(if ($idleRate -gt 0.0001) { $loadRate / $idleRate } else { [double]::PositiveInfinity })) -ForegroundColor Yellow
Write-Host ("machine has {0} logical cores; 100% of one core = {1:N1}% of the machine" -f $cores, (100.0 / $cores)) -ForegroundColor DarkGray

Write-Host ""
Write-Host "===== app diagnostics =====" -ForegroundColor Cyan
Get-Content -LiteralPath $errLog -ErrorAction SilentlyContinue |
    Where-Object { $_ -match '\[diag\]' } |
    Select-Object -Last 40

Remove-Item -LiteralPath $errLog -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "Browser left running (pid $($live.Id)). Close it yourself when done." -ForegroundColor Green
exit 0