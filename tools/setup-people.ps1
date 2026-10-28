# Install the MakeHuman tools for tools/blender/build_people.py into a private Blender profile (tools\out\blender-user),
# so the user's own Blender stays untouched. Needs the downloads of tools/fetch-library.py (tools\out\library\makehuman).
#   powershell -File tools\setup-people.ps1
# Then: set BLENDER_USER_RESOURCES=<repo>\tools\out\blender-user and run blender -b -P tools/blender/build_people.py
$root = Split-Path -Parent $PSScriptRoot
$blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
$mh = Join-Path $root "tools\out\library\makehuman"
$env:BLENDER_USER_RESOURCES = Join-Path $root "tools\out\blender-user"
& $blender -b --factory-startup --command extension install-file -r user_default -e (Join-Path $mh "add-on-mpfb-v2.0.18.zip")
$data = Join-Path $env:BLENDER_USER_RESOURCES "extensions\.user\user_default\mpfb\data"
New-Item -ItemType Directory -Force $data | Out-Null
foreach ($p in "makehuman_system_assets", "skins01", "skins02", "shirts01", "pants01", "shoes01", "hats02", "hair01") {
  Expand-Archive -Force (Join-Path $mh "$($p)_cc0.zip") $data
}
"MakeHuman assets in $data"
