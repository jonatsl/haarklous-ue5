# Rebuild every asset from the web game, in order (each step can also be run alone):
#   1. export geometry and data from the web game      node tools/web-export/export.mjs
#   2. rigs and the Blender build plan                 node tools/web-export/make-rigs.mjs
#   3. Blender: build FBX files and ground textures    blender -b -P tools/blender/build_assets.py / make_textures.py
#   4. Unreal: import meshes, textures, materials      tools/unreal/run-python.ps1 tools/unreal/import_assets.py
#   5. Unreal: ground and post-process materials       tools/unreal/materials_ground.py, materials_extra.py
#   6. Unreal: build the level                         tools/unreal/run-python.ps1 tools/unreal/build_level.py
#   (then bake the lighting: tools/unreal/build-lighting.ps1)
#   powershell -File tools\build-assets.ps1 [-From 3]
param([int]$From = 1, [string]$Rigs = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
Push-Location $root
try {
  if ($From -le 1) { node tools/web-export/export.mjs; if ($LASTEXITCODE) { throw "export failed" } }
  if ($From -le 2) { node tools/web-export/make-rigs.mjs; if ($LASTEXITCODE) { throw "make-rigs failed" } }
  if ($From -le 3) {
    & $blender -b --factory-startup -P tools/blender/build_assets.py -- $Rigs.Split(' ') 2>&1 | Select-String "HK:|Error|Traceback"
    & $blender -b --factory-startup -P tools/blender/make_textures.py 2>&1 | Select-String "Error|Traceback"
  }
  if ($From -le 4) {
    $extra = if ($Rigs) { @("-ScriptArgs", $Rigs) } else { @() }
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/import_assets.py @extra | Select-String "HK:|Error:|Traceback"
  }
  if ($From -le 5) {
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_ground.py | Select-String "HK:|Error:|Traceback"
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_extra.py | Select-String "HK:|Error:|Traceback"
  }
  if ($From -le 6) { powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/build_level.py | Select-String "HK:|Error:|Traceback" }
} finally { Pop-Location }
