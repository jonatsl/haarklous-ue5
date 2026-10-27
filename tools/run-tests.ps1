# Run the C++ automation tests (logic checked against the web version's own answers) without opening a window.
#   powershell -File tools\run-tests.ps1 [-Filter Haarklous]
param([string]$Filter = "Haarklous")
$root = Split-Path -Parent $PSScriptRoot
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$report = Join-Path $root "tools\out\test-report"
New-Item -ItemType Directory -Force $report | Out-Null
$log = Join-Path $report "tests.log"
# Same lock as tools/build-unreal.ps1: builds and test runs take turns (several helpers share this PC's 14 GB).
$lockPath = Join-Path $env:TEMP "haarklous-ue5-build.lock"
$lock = $null
for ($i = 0; $i -lt 3600 -and -not $lock; $i++) { try { $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { Start-Sleep -Seconds 1 } }
if (-not $lock) { throw "Could not get the build lock" }
try {
& "$engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -nullrhi -unattended -nosplash -nopause -nosound "-ExecCmds=Automation RunTests $Filter;Quit" "-TestExit=Automation Test Queue Empty" "-ReportExportPath=$report" "-abslog=$log" -stdout 2>&1 | Out-Null
} finally { $lock.Close() }
$lines = Get-Content $log
$lines | Where-Object { $_ -match 'LogAutomationController: (Display|Error|Warning).*(Test Completed|Result=|match the web|largest|Error|Expected|: \d+ / \d+)' -or $_ -match 'TEST COMPLETE|Tests? (passed|failed)' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 80
$failed = ($lines | Where-Object { $_ -match 'Result=\{Fail' }).Count
$passed = ($lines | Where-Object { $_ -match 'Result=\{Success' }).Count
"Passed: $passed  Failed: $failed"
if ($failed -gt 0 -or $passed -eq 0) { exit 1 }
