# ESProbe firmware build verification script.
#
# Project-owned check: performs a full `idf.py build` and verifies that the
# expected application binary is produced. Intended to run after any firmware
# change as the smallest verification route.
#
# Usage (plain PowerShell, no IDF environment required beforehand):
#   pwsh -File fw/scripts/verify_build.ps1
#
# Exit code 0 = build verified; non-zero = failure.

$ErrorActionPreference = 'Stop'

$fwDir = Split-Path -Parent $PSScriptRoot   # fw/
$binName = 'esprobe_dap.bin'

# 1. Locate the ESP-IDF PowerShell profile (version-agnostic).
#    Search order: IDF_PATH already set > IDF_TOOLS_PATH > default install dir.
if (-not $env:IDF_PATH) {
    $toolsRoot = if ($env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH } else { 'C:\Espressif\tools' }
    $profileScript = Get-ChildItem (Join-Path $toolsRoot 'Microsoft.v*.PowerShell_profile.ps1') -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $profileScript) {
        Write-Error "ESP-IDF PowerShell profile not found under $toolsRoot and IDF_PATH is not set. Set IDF_TOOLS_PATH or IDF_PATH."
        exit 2
    }
    Write-Host "[verify_build] Loading IDF environment: $($profileScript.FullName)"
    . $profileScript.FullName | Out-Null
}

Write-Host "[verify_build] IDF_PATH = $env:IDF_PATH"

# 2. Build.
Push-Location $fwDir
try {
    idf.py build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[verify_build] idf.py build failed with exit code $LASTEXITCODE"
        exit 1
    }
}
finally {
    Pop-Location
}

# 3. Verify the application binary exists and is fresh.
$binPath = Join-Path $fwDir "build\$binName"
if (-not (Test-Path $binPath)) {
    Write-Error "[verify_build] Expected binary not found: $binPath"
    exit 1
}
$bin = Get-Item $binPath
Write-Host "[verify_build] OK: $($bin.FullName) ($($bin.Length) bytes, built $($bin.LastWriteTime))"
exit 0
