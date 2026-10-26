"""Build haarklous-game.html: one self-contained offline file with every script and the stylesheet inlined.

Run after editing any source file:  python build.py
Regenerate the neighbourhood data (OpenStreetMap + Kartverket heights) only when needed:  python generate-surroundings.py
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent
html = (root / 'index.html').read_text(encoding='utf-8')
html = html.replace('<head>', '<head>\n<!-- Three.js license:\n' + (root / 'vendor/LICENSE.txt').read_text(encoding='utf-8') + '\n-->', 1)
html = html.replace('<link rel="stylesheet" href="style.css">', '<style>' + (root / 'style.css').read_text(encoding='utf-8') + '</style>')
scripts = re.findall(r'<script src="([^"]+)"></script>', html)
for script in scripts:
    content = (root / script).read_text(encoding='utf-8').replace('</script', '<\\/script')
    html = html.replace(f'<script src="{script}"></script>', '<script>' + content + '</script>', 1)
(root / 'haarklous-game.html').write_text(html, encoding='utf-8')
print(f'Built haarklous-game.html — {len(scripts)} scripts inlined, {len(html) // 1024} KB, works offline')
# The same file is what the Cloudflare Worker serves (cloud/public/index.html; deploy with `npx wrangler deploy` in cloud/).
public = root / 'cloud' / 'public'
public.mkdir(parents=True, exist_ok=True)
(public / 'index.html').write_text(html, encoding='utf-8')
