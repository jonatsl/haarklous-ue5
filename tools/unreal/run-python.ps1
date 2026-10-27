# Run an Unreal Python editor script headless (asset import, level building ...).
#   powershell -File tools\unreal\run-python.ps1 tools\unreal\import_assets.py [-- extra args]
# Shares the lock of tools\build-unreal.ps1 so builds, tests and imports take turns.
param([Parameter(Mandatory = $true)][string]$Script, [string]$ScriptArgs = "", [switch]$WithRHI)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$scriptPath = (Resolve-Path $Script).Path
$logDir = Join-Path $root "tools\out\logs"; New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("python-" + [IO.Path]::GetFileNameWithoutExtension($scriptPath) + ".log")
$lockPath = Join-Path $env:TEMP "haarklous-ue5-build.lock"
$lock = $null
for ($i = 0; $i -lt 3600 -and -not $lock; $i++) { try { $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { Start-Sleep -Seconds 1 } }
if (-not $lock) { throw "Could not get the build lock" }
try {
  # The commandlet reads backslashes as escapes: pass the path with forward slashes, arguments via HK_ARGS.
  $cmd = $scriptPath.Replace('\', '/')
  $env:HK_ARGS = $ScriptArgs
  $rhi = if ($WithRHI) { @() } else { @("-nullrhi") }
  & "$engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$cmd" -unattended -nosplash -nopause -nosound @rhi "-abslog=$log" -stdout 2>&1 | Out-Null
  $code = $LASTEXITCODE
} finally { $lock.Close() }
Get-Content $log | Where-Object { $_ -match 'LogPython|Error|HK:' -and $_ -notmatch 'LogPython: Display: Running' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 60
exit $code
