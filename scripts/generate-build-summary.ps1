#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Generates a comprehensive build summary for CI/CD workflows
.DESCRIPTION
    Creates a human-readable markdown report with build information, package details,
    checksums, and artifact metadata for the Ultralight WebBrowser CI/CD pipeline
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$BuildDir,
    
    [Parameter(Mandatory=$true)]
    [string]$Platform,
    
    [Parameter(Mandatory=$true)]
    [string]$Architecture,
    
    [Parameter(Mandatory=$false)]
    [string]$Version = "dev",
    
    [Parameter(Mandatory=$false)]
    [string]$CommitSha = "",
    
    [Parameter(Mandatory=$false)]
    [string]$SdkVersion = "Unknown",
    
    [Parameter(Mandatory=$false)]
    [string]$OutputFile = "BUILD-SUMMARY.md"
)

$ErrorActionPreference = "Stop"

# Timestamp
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss UTC"

# Initialize summary content
$summary = @"
# 🚀 Ultralight WebBrowser Build Summary - $Platform $Architecture

**Platform:** $Platform  
**Architecture:** $Architecture  
**Version:** $Version  
**Build Date:** $timestamp  
**Commit:** ``$CommitSha``  
**Ultralight SDK:** $SdkVersion

---

## 📦 Generated Packages

"@

# Find all generated packages
$packages = @()
$packageExtensions = @("*.exe", "*.zip", "*.tar.gz", "*.deb", "*.rpm", "*.dmg")

foreach ($ext in $packageExtensions) {
    $found = Get-ChildItem -Path $BuildDir -Filter $ext -File -ErrorAction SilentlyContinue
    if ($found) {
        $packages += $found
    }
}

if ($packages.Count -eq 0) {
    $summary += "`n⚠️ No packages found in build directory`n"
} else {
    $summary += "`n| Package | Size | SHA256 |`n"
    $summary += "|---------|------|--------|`n"
    
    foreach ($pkg in $packages) {
        $sizeMB = [math]::Round($pkg.Length / 1MB, 2)
        $hash = (Get-FileHash -Path $pkg.FullName -Algorithm SHA256).Hash.Substring(0, 16)
        $summary += "| ``$($pkg.Name)`` | $sizeMB MB | ``$hash...`` |`n"
    }
}

$summary += @"

---

## 🔧 Build Configuration

"@

# Check CMake configuration
$cmakeCacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cmakeCacheFile) {
    $summary += "`n### CMake Settings`n`n"
    $summary += "| Setting | Value |`n"
    $summary += "|---------|-------|`n"
    
    # Extract key configuration values
    $configKeys = @(
        @{ Key = "CMAKE_BUILD_TYPE"; Label = "Build Type" },
        @{ Key = "CMAKE_CXX_COMPILER_ID"; Label = "Compiler" },
        @{ Key = "CMAKE_CXX_COMPILER_VERSION"; Label = "Compiler Version" },
        @{ Key = "CMAKE_GENERATOR"; Label = "Generator" },
        @{ Key = "CMAKE_SYSTEM_NAME"; Label = "System" },
        @{ Key = "CMAKE_SYSTEM_PROCESSOR"; Label = "Processor" }
    )
    
    $cacheContent = Get-Content $cmakeCacheFile
    $foundAny = $false
    foreach ($config in $configKeys) {
        $match = $cacheContent | Select-String -Pattern "^$($config.Key):.*=(.+)$" | Select-Object -First 1
        if ($match -and $match.Matches[0].Groups[1].Value) {
            $value = $match.Matches[0].Groups[1].Value.Trim()
            if ($value -and $value -ne "") {
                $summary += "| $($config.Label) | ``$value`` |`n"
                $foundAny = $true
            }
        }
    }
    
    if (-not $foundAny) {
        $summary += "| (No CMake settings found) | - |`n"
    }
    
    $summary += "`n"
} else {
    $summary += "`n### CMake Settings`n`n"
    $summary += "ℹ️ CMake cache not available (build may use cached configuration)`n`n"
}

$summary += @"

---

## ✅ Verification

"@

# Verify executable exists
$exePatterns = @("Ultralight-WebBrowser.exe", "Ultralight-WebBrowser")
$exeFound = $false

foreach ($pattern in $exePatterns) {
    $exe = Get-ChildItem -Path $BuildDir -Filter $pattern -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exe) {
        $exeFound = $true
        $exeSizeMB = [math]::Round($exe.Length / 1MB, 2)
        $summary += "`n✅ **Executable found:** ``$($exe.Name)`` ($exeSizeMB MB)`n"
        break
    }
}

if (-not $exeFound) {
    $summary += "`n⚠️ **Executable not found** in build directory`n"
}

# Check for assets
$assetsDir = Join-Path $BuildDir "assets"
if (Test-Path $assetsDir) {
    $assetCount = (Get-ChildItem -Path $assetsDir -Recurse -File).Count
    $summary += "✅ **Assets directory found:** $assetCount files`n"
} else {
    # Assets might be in Release folder or bundled differently
    $releaseAssets = Join-Path $BuildDir "Release/assets"
    if (Test-Path $releaseAssets) {
        $assetCount = (Get-ChildItem -Path $releaseAssets -Recurse -File).Count
        $summary += "✅ **Assets directory found:** $assetCount files`n"
    } else {
        $summary += "ℹ️ Assets bundled in package or located elsewhere`n"
    }
}

# Check for required DLLs (Windows only)
if ($Platform -eq "Windows") {
    $requiredDlls = @("AppCore.dll", "Ultralight.dll", "UltralightCore.dll", "WebCore.dll")
    $missingDlls = @()
    
    foreach ($dll in $requiredDlls) {
        $found = Get-ChildItem -Path $BuildDir -Filter $dll -File -Recurse -ErrorAction SilentlyContinue
        if (-not $found) {
            $missingDlls += $dll
        }
    }
    
    if ($missingDlls.Count -eq 0) {
        $summary += "✅ **All required DLLs found**`n"
    } else {
        $summary += "⚠️ **Missing DLLs:** $($missingDlls -join ', ')`n"
    }
}

$summary += @"

---

## 📥 Installation Instructions

"@

if ($Platform -eq "Windows") {
    $summary += @"

### Windows

1. Download the ``.exe`` installer or ``.zip`` package
2. **Installer:** Run the ``.exe`` and follow the setup wizard
3. **Portable:** Extract the ``.zip`` to your desired location and run ``Ultralight-WebBrowser.exe``

"@
} elseif ($Platform -eq "Linux") {
    $summary += @"

### Linux

**Debian/Ubuntu (.deb):**
```bash
sudo dpkg -i Ultralight-WebBrowser-*.deb
sudo apt-get install -f  # Fix dependencies if needed
```

**RedHat/Fedora (.rpm):**
```bash
sudo rpm -i Ultralight-WebBrowser-*.rpm
```

**Portable (.tar.gz):**
```bash
tar xzf Ultralight-WebBrowser-*.tar.gz
cd UltralightWebBrowser
./Ultralight-WebBrowser
```

"@
} elseif ($Platform -eq "macOS") {
    $summary += @"

### macOS

**DMG Package:**
1. Open the ``.dmg`` file
2. Drag ``Ultralight Web Browser`` to Applications
3. Launch from Applications or Spotlight

**Portable (.tar.gz):**
```bash
tar xzf Ultralight-WebBrowser-*.tar.gz
cd UltralightWebBrowser
./Ultralight-WebBrowser
```

"@
}

$summary += @"

---

## 📊 Build Statistics

"@

# Count files and calculate total size
$allFiles = Get-ChildItem -Path $BuildDir -File -Recurse
$totalFiles = $allFiles.Count
$totalSizeMB = [math]::Round(($allFiles | Measure-Object -Property Length -Sum).Sum / 1MB, 2)

$summary += "`n- **Total files:** $totalFiles`n"
$summary += "- **Total size:** $totalSizeMB MB`n"

# Package format breakdown
$formatCounts = @{}
foreach ($pkg in $packages) {
    $ext = $pkg.Extension
    if (-not $formatCounts.ContainsKey($ext)) {
        $formatCounts[$ext] = 0
    }
    $formatCounts[$ext]++
}

if ($formatCounts.Count -gt 0) {
    $summary += "- **Package formats:** $($formatCounts.Keys -join ', ')`n"
}

$summary += @"

---

**Generated by Ultralight WebBrowser CI/CD Pipeline**  
*For more information, visit the [GitHub Repository](https://github.com/yourusername/Ultralight-alt)*

"@

# Write summary to file
$outputPath = Join-Path $BuildDir $OutputFile
$summary | Out-File -FilePath $outputPath -Encoding UTF8

Write-Host "✅ Build summary generated: $outputPath" -ForegroundColor Green
Write-Host ""
Write-Host "Summary preview:" -ForegroundColor Cyan
Write-Host $summary

# Return the summary for GitHub Actions output
return $summary
