# Replace reference\web-game with a fresh read-only snapshot of the web game's main branch.
# Reads from the web game repo only; never writes there.
$ErrorActionPreference = 'Stop'
$web = 'C:\Users\Lenovo\Documents\haarklous-plass-ps-kontroller'
$root = Split-Path -Parent $PSScriptRoot
$ref = Join-Path $root 'reference\web-game'
$sha = (git -C $web rev-parse --short main).Trim()
if (Test-Path $ref) { Remove-Item -Recurse -Force $ref }
New-Item -ItemType Directory -Force $ref | Out-Null
$tar = Join-Path $env:TEMP "haarklous-web-$sha.tar"
git -C $web archive --format=tar -o $tar main
tar -x -f $tar -C $ref
Remove-Item $tar
# The web game's own agent instructions (deploying, its GitHub repo) do not apply here.
Rename-Item (Join-Path $ref 'AGENTS.md') 'AGENTS.web-game.md'
Write-Host "reference\web-game is now main at $sha. Update the commit in README.md and port what changed."
