# Rebuild every asset from the web game, in order (each step can also be run alone):
#   1. export geometry and data from the web game      node tools/web-export/export.mjs
#   2. rigs and the Blender build plan                 node tools/web-export/make-rigs.mjs
#   3. Blender: build FBX files and ground textures    blender -b -P tools/blender/build_assets.py / make_textures.py
#      photo-real pass: CC0 scans (python tools/fetch-library.py, downloads once), building map, grass and leaf
#      cards, then balconies / downpipes / chimneys / kerbs (blender -b -P tools/blender/build_details.py)
#   4. Unreal: import meshes, textures, materials      tools/unreal/run-python.ps1 tools/unreal/import_assets.py
#   5. Unreal: ground, post-process and realism materials  materials_ground.py, materials_extra.py, materials_realism.py,
#      materials_photoreal.py, material_usage.py
#   6. Unreal: build the level                         tools/unreal/run-python.ps1 tools/unreal/build_level.py
#      then the photographed sky on its dome            tools/unreal/materials_sky.py
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
    python tools/fetch-library.py; if ($LASTEXITCODE) { throw "fetch-library failed" }
    python tools/make-building-map.py; if ($LASTEXITCODE) { throw "make-building-map failed" }
    python tools/make-grass-atlas.py; if ($LASTEXITCODE) { throw "make-grass-atlas failed" }
    python tools/make-leaf-atlas.py; if ($LASTEXITCODE) { throw "make-leaf-atlas failed" }
    python tools/make-sky.py; if ($LASTEXITCODE) { throw "make-sky failed" }
    & $blender -b --factory-startup -P tools/blender/build_grass.py 2>&1 | Select-String "HK:|Error|Traceback"
    & $blender -b --factory-startup -P tools/blender/build_details.py 2>&1 | Select-String "HK:|Error|Traceback"
  }
  if ($From -le 4) {
    $extra = if ($Rigs) { @("-ScriptArgs", $Rigs) } else { @() }
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/import_assets.py @extra | Select-String "HK:|Error:|Traceback"
  }
  if ($From -le 5) {
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_ground.py | Select-String "HK:|Error:|Traceback"
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_extra.py | Select-String "HK:|Error:|Traceback"
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_realism.py | Select-String "HK:|Error:|Traceback"
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_photoreal.py | Select-String "HK:|Error:|Traceback"
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/material_usage.py | Select-String "HK:|Error:|Traceback"
  }
  if ($From -le 6) {
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/build_level.py | Select-String "HK:|Error:|Traceback"
    powershell -NoProfile -File tools/unreal/run-python.ps1 tools/unreal/materials_sky.py | Select-String "HK:|Error:|Traceback"
  }
} finally { Pop-Location }
