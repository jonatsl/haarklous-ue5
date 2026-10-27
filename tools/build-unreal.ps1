# Build the Unreal project from the command line (C++ only, no editor window).
#   powershell -File tools\build-unreal.ps1            -> HaarklousEditor Win64 Development (used by the editor, tests and -game runs)
#   powershell -File tools\build-unreal.ps1 -Target Haarklous -Config Shipping
# Only one build runs at a time (a lock file), so several helpers can call it safely.
param([string]$Target = "HaarklousEditor", [string]$Config = "Development")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$lockPath = Join-Path $env:TEMP "haarklous-ue5-build.lock"
$lock = $null
for ($i = 0; $i -lt 1800 -and -not $lock; $i++) {
  try { $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { Start-Sleep -Seconds 1 }
}
if (-not $lock) { throw "Could not get the build lock" }
try {
  & "$engine\Engine\Build\BatchFiles\Build.bat" $Target Win64 $Config "-Project=$project" -WaitMutex -NoHotReloadFromIDE 2>&1 | ForEach-Object { $_ } | Tee-Object -Variable out | Out-Null
  $code = $LASTEXITCODE
  # Print only what matters: errors, warnings from our code and the result line.
  $out | Where-Object { $_ -match 'error|warning C|Result:|Total execution time|Succeeded|Failed' -and $_ -notmatch 'Engine\Source' } | Select-Object -Last 120
  exit $code
} finally { $lock.Close() }
