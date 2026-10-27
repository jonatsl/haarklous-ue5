# Run the C++ automation tests (logic checked against the web version's own answers) without opening a window.
#   powershell -File tools\run-tests.ps1 [-Filter Haarklous]
param([string]$Filter = "Haarklous")
$root = Split-Path -Parent $PSScriptRoot
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$report = Join-Path $root "tools\out\test-report"
New-Item -ItemType Directory -Force $report | Out-Null
$log = Join-Path $report "tests.log"
& "$engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -nullrhi -unattended -nosplash -nopause -nosound "-ExecCmds=Automation RunTests $Filter;Quit" "-TestExit=Automation Test Queue Empty" "-ReportExportPath=$report" "-abslog=$log" -stdout 2>&1 | Out-Null
$lines = Get-Content $log
$lines | Where-Object { $_ -match 'LogAutomationController: (Display|Error|Warning).*(Test Completed|Result=|match the web|largest|Error|: \d+ / \d+)' -or $_ -match 'TEST COMPLETE|Tests? (passed|failed)' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 80
$failed = ($lines | Where-Object { $_ -match 'Result=\{Fail' }).Count
$passed = ($lines | Where-Object { $_ -match 'Result=\{Success' }).Count
"Passed: $passed  Failed: $failed"
if ($failed -gt 0 -or $passed -eq 0) { exit 1 }
