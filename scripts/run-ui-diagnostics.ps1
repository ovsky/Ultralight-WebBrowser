#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Launch the browser, drive synthetic input at it, and report frame/event health.

.DESCRIPTION
  UI lag and mis-targeted clicks cannot be diagnosed from source alone: the
  question is whether the window listener is slow, or whether the engine is
  delivering input late because it is not presenting frames. The browser already
  has a diagnostic for exactly that (InputDiagScope in src/UI.cpp, active with
  ULTRALIGHT_VERBOSE=1) which reports the event count and mean handler time per
  label. This script launches the app with that enabled, focuses its window,
  moves the pointer into the content area, generates wheel and mouse-move
  traffic, then kills the process and prints whatever the app wrote to stderr.

  Interpretation:
    * many events + high handler ms  -> the handler is the bottleneck
    * few events + low handler ms    -> the engine is not presenting frames

.EXAMPLE
  ./scripts/run-ui-diagnostics.ps1 -Seconds 12
#>
[CmdletBinding()]
param(
    [int]$Seconds = 12,
    [string]$BuildDir = "build-win64",
    [int]$WheelTicks = 240,
    [int]$MouseMoves = 200,
    # Optional single click at an explicit point, "x,y" in window coordinates.
    [string]$ClickAt = '',
    # Open the settings page first via Ctrl+, so a test runs against the page the
    # lag reports are about rather than whatever the session restored.
    [switch]$OpenSettings,
    # Required before any synthetic click is sent. See the -ClickAt handling.
    [switch]$AllowClicks
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeInput {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, int dwData, UIntPtr extra);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern short VkKeyScan(char ch);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int W, int H, bool repaint);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int X, int Y, int cx, int cy, uint flags);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const uint MOUSEEVENTF_WHEEL = 0x0800;
    public const uint MOUSEEVENTF_MOVE = 0x0001;
    public const uint MOUSEEVENTF_ABSOLUTE = 0x8000;
    public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    public const uint MOUSEEVENTF_LEFTUP = 0x0004;
    public const uint KEYEVENTF_KEYUP = 0x0002;
}
'@

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
$exe = Join-Path $root (Join-Path $BuildDir 'Ultralight-WebBrowser.exe')
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "Browser executable not found: $exe"
    exit 2
}

$stdout = [System.IO.Path]::GetTempFileName()
$stderr = [System.IO.Path]::GetTempFileName()

$env:ULTRALIGHT_VERBOSE = '1'

Write-Host "Launching: $exe" -ForegroundColor Cyan
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Normal

# The engine takes a moment to create its window and load the chrome view.
$deadline = (Get-Date).AddSeconds(30)
$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ($proc.HasExited) { break }
    $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
        $hwnd = $proc.MainWindowHandle
        break
    }
}

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Warning "No main window handle detected. Continuing with input generation anyway."
}
else {
    Write-Host "Window handle: $hwnd" -ForegroundColor Green
    # Give the window a real size and a known position so the synthetic pointer
    # coordinates land inside the content area rather than off-screen.
    [NativeInput]::ShowWindow($hwnd, 9) | Out-Null   # SW_RESTORE
    [NativeInput]::MoveWindow($hwnd, 40, 40, 1400, 900, $true) | Out-Null

    # Synthetic pointer input is delivered to whatever window is under the
    # cursor, whatever the focus is. On a real desktop that is usually some other
    # application -- during development of this, a browser window launched from a
    # script stayed behind an already-open window and every wheel tick and click
    # went to that app instead, which looks exactly like "the browser ignores all
    # input". Raise the window above everything for the duration, then put it back
    # so the user's window stacking is left as it was found.
    [NativeInput]::SetWindowPos($hwnd, [NativeInput]::HWND_TOPMOST, 0, 0, 0, 0,
        [NativeInput]::SWP_NOMOVE -bor [NativeInput]::SWP_NOSIZE -bor [NativeInput]::SWP_SHOWWINDOW) | Out-Null
    [NativeInput]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 1200

    $fg = [NativeInput]::GetForegroundWindow()
    if ($fg -ne $hwnd) {
        Write-Warning "Foreground window is $fg but the browser is $hwnd."
        Write-Warning "Windows denies SetForegroundWindow to a process that is not already foreground, so"
        Write-Warning "synthetic input may not reach the browser at all -- and an absent '[diag]' line then"
        Write-Warning "means 'no input arrived', not 'the handler is silent'. Click the browser window"
        Write-Warning "once by hand and rerun if a run produces no diag output."
    }
}

# Where to point: centre of the window, which is the page/content area, well
# below the toolbar.
$cx = 700
$cy = 500
[NativeInput]::SetCursorPos($cx, $cy) | Out-Null
Start-Sleep -Milliseconds 400

Write-Host "Generating $WheelTicks wheel ticks and $MouseMoves mouse moves..." -ForegroundColor Cyan

if ($OpenSettings) {
    # Ctrl+, is the settings shortcut (assets/shortcuts.json). Synthetic
    # keystrokes need the target window focused, which MoveWindow alone does not
    # guarantee, so this is best-effort: check the app's own stderr for the
    # settings page before trusting a result that depends on it.
    Write-Host "Sending Ctrl+, to open settings..." -ForegroundColor Yellow
    $VK_CONTROL = 0x11
    $VK_OEM_COMMA = 0xBC
    [NativeInput]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 400
    [NativeInput]::keybd_event($VK_CONTROL, 0, 0, [UIntPtr]::Zero)
    [NativeInput]::keybd_event($VK_OEM_COMMA, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [NativeInput]::keybd_event($VK_OEM_COMMA, 0, [NativeInput]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
    [NativeInput]::keybd_event($VK_CONTROL, 0, [NativeInput]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
    Start-Sleep -Seconds 3
}

# Optional single click at an explicit point, used to exercise the hit-test
# diagnostic in UI::OnMouseEvent. Takes "x,y" in window coordinates.
#
# Guarded because a click on the settings page toggles the setting under the
# cursor and, with auto-save on, writes it to the user's profile. That happened
# while developing this script: a diagnostic run flipped the real
# launch_dark_theme setting. Clicking has to be asked for by name.
if ($ClickAt -and -not $AllowClicks) {
    Write-Error "-ClickAt needs -AllowClicks: a click mutates whichever setting is under the cursor and auto-saves it to the user profile."
    exit 3
}
if ($ClickAt) {
    $parts = $ClickAt.Split(',')
    $kx = [int]$parts[0]
    $ky = [int]$parts[1]
    Write-Host "Clicking at ($kx,$ky)..." -ForegroundColor Yellow
    [NativeInput]::SetCursorPos($kx, $ky) | Out-Null
    Start-Sleep -Milliseconds 500
    [NativeInput]::mouse_event([NativeInput]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [NativeInput]::mouse_event([NativeInput]::MOUSEEVENTF_LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 900
}

for ($i = 0; $i -lt $WheelTicks; $i++) {
    [NativeInput]::mouse_event([NativeInput]::MOUSEEVENTF_WHEEL, 0, 0, -120, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 12
}

for ($i = 0; $i -lt $MouseMoves; $i++) {
    [NativeInput]::SetCursorPos(($cx + ($i % 40) * 8), ($cy + ($i % 30) * 6)) | Out-Null
    [NativeInput]::mouse_event([NativeInput]::MOUSEEVENTF_MOVE, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 12
}

# Long enough for the 2s diagnostic window to emit at least one report.
Start-Sleep -Seconds ([Math]::Max(3, $Seconds - [int](($WheelTicks + $MouseMoves) * 12 / 1000)))

if (-not $proc.HasExited) {
    Write-Host "Stopping process..." -ForegroundColor Yellow
    # Hand the desktop back the way it was found: drop the always-on-top flag
    # before closing so the user's window stacking is not permanently changed.
    if ($hwnd -ne [IntPtr]::Zero) {
        try {
            [NativeInput]::SetWindowPos($hwnd, [NativeInput]::HWND_NOTOPMOST, 0, 0, 0, 0,
                [NativeInput]::SWP_NOMOVE -bor [NativeInput]::SWP_NOSIZE) | Out-Null
        } catch { }
    }
    try { $proc.CloseMainWindow() | Out-Null } catch { }
    Start-Sleep -Milliseconds 800
    if (-not $proc.HasExited) {
        try { Stop-Process -Id $proc.Id -Force } catch { }
    }
}

Write-Host ""
Write-Host "===== stderr =====" -ForegroundColor Cyan
if (Test-Path $stderr) { Get-Content -LiteralPath $stderr | Where-Object { $_ -notmatch 'WARNING: trying to load platform resource' } }

Write-Host ""
Write-Host "===== stdout =====" -ForegroundColor Cyan
if (Test-Path $stdout) { Get-Content -LiteralPath $stdout | Select-Object -Last 60 }

Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue
exit 0