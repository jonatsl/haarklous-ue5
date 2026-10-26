"""Package the game for another computer: dist/Haarklous-online.zip.

Run python build.py first. The receiver extracts the ZIP and double-clicks haarklous-game.html
(or PLAY-Controller-Windows.cmd). No installation, server or internet connection is needed;
aerial imagery is the only part that loads online.
"""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent
FILES = ["haarklous-game.html", "PLAY-Controller-Windows.cmd", "START-Windows.cmd", "README.md"]
destination = ROOT / "dist" / "Haarklous-online.zip"
destination.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in FILES:
        archive.write(ROOT / name, f"Haarklous-online/{name}")
print(f"Wrote {destination} ({destination.stat().st_size // 1024} KB)")
