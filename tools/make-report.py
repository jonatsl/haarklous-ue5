"""Final report: copies the screenshots named in docs/REPORT.md (as img/<name>.jpg) from the test output folders and
turns the Markdown into one self-contained HTML page with the pictures embedded.
    python tools/make-report.py
Sources: tools/out/shots/<test>/<shot>.png -> docs/report/img/<test>--<shot>.jpg (named that way in REPORT.md), plus the
online test pictures listed in EXTRA."""
import base64, html, io, os, re
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
MD = os.path.join(ROOT, 'docs', 'REPORT.md')
IMG = os.path.join(ROOT, 'docs', 'report', 'img')
OUT = os.path.join(ROOT, 'docs', 'report', 'Haarklous-UE5-report.html')
SHOTS = os.path.join(ROOT, 'tools', 'out', 'shots')
NET = os.path.join(ROOT, 'tools', 'out', 'net-tests')


def source(name):
    """img name 'test--shot' -> tools/out/shots/test/shot.png; 'net--x' -> tools/out/net-tests/(shots-deployed-game/)x.png"""
    test, shot = name.split('--', 1)
    if test == 'net':
        for p in (os.path.join(NET, 'shots-deployed-game', shot + '.png'), os.path.join(NET, shot + '.png')):
            if os.path.exists(p):
                return p
        return None
    p = os.path.join(SHOTS, test, shot + '.png')
    return p if os.path.exists(p) else None


THUMBS = {}


def thumb(name):
    """Small embedded picture for a table cell (cached, the same picture is embedded once per use)."""
    if name not in THUMBS:
        src = source(name)
        if not src:
            print('missing picture', name)
            THUMBS[name] = ''
        else:
            im = Image.open(src).convert('RGB')
            im.thumbnail((320, 200))
            buf = io.BytesIO(); im.save(buf, 'JPEG', quality=72)
            THUMBS[name] = 'data:image/jpeg;base64,' + base64.b64encode(buf.getvalue()).decode()
    return THUMBS[name]


def inline(s):
    pics = []

    def keep(m):
        pics.append((m.group(1), m.group(2)))
        return f'@@PIC{len(pics) - 1}@@'
    s = re.sub(r'!\[([^\]]*)\]\(img/([^)]+)\.jpg\)', keep, s)
    s = html.escape(s)
    s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', s)
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)
    s = re.sub(r'@@PIC(\d+)@@', lambda m: f'<img class="thumb" src="{thumb(pics[int(m.group(1))][1])}" alt="{html.escape(pics[int(m.group(1))][0])}">', s)
    return s


def main():
    os.makedirs(IMG, exist_ok=True)
    text = open(MD, encoding='utf-8').read()
    out, para, in_list, table = [], [], False, []

    def flush():
        nonlocal para, in_list, table
        if para:
            out.append('<p>' + inline(' '.join(para)) + '</p>'); para = []
        if in_list:
            out.append('</ul>'); in_list = False
        if table:
            rows = [r for r in table if not re.match(r'^\|\s*-', r)]
            cells = [[c.strip() for c in r.strip().strip('|').split('|')] for r in rows]
            out.append('<table>' + ''.join('<tr>' + ''.join(f'<{"th" if i == 0 else "td"}>{inline(c)}</{"th" if i == 0 else "td"}>' for c in row) + '</tr>'
                                           for i, row in enumerate(cells)) + '</table>')
            table = []

    for line in text.splitlines():
        m = re.match(r'^!\[([^\]]*)\]\(img/([^)]+)\.jpg\)', line.strip())
        if m:
            flush()
            cap, name = m.group(1), m.group(2)
            src = source(name)
            if not src:
                print('missing picture', name)
                continue
            im = Image.open(src).convert('RGB')
            im.thumbnail((1280, 800))
            im.save(os.path.join(IMG, name + '.jpg'), quality=82)
            buf = io.BytesIO(); im.save(buf, 'JPEG', quality=78)
            out.append(f'<figure><img src="data:image/jpeg;base64,{base64.b64encode(buf.getvalue()).decode()}" alt="{html.escape(cap)}"><figcaption>{inline(cap)}</figcaption></figure>')
            continue
        h = re.match(r'^(#{1,3}) (.*)', line)
        if h:
            flush(); n = len(h.group(1)); out.append(f'<h{n}>{inline(h.group(2))}</h{n}>'); continue
        if line.strip().startswith('|'):
            if para or in_list:
                t = table; table = []; flush(); table = t
            table.append(line); continue
        li = re.match(r'^\s*[-*] (.*)', line)
        if li:
            if para or table:
                flush()
            if not in_list:
                out.append('<ul>'); in_list = True
            out.append('<li>' + inline(li.group(1)) + '</li>'); continue
        if not line.strip():
            flush(); continue
        if in_list and line.startswith('  '):
            out[-1] = out[-1][:-5] + ' ' + inline(line.strip()) + '</li>'; continue
        if in_list or table:
            flush()
        para.append(line.strip())
    flush()
    css = """:root{--bg:#fbfaf7;--fg:#1d1f22;--mut:#5d6470;--line:#dedad2;--acc:#2f5d8a}
@media (prefers-color-scheme:dark){:root{--bg:#16181b;--fg:#e6e4df;--mut:#a3a9b2;--line:#33373d;--acc:#8fb6e0}}
body{background:var(--bg);color:var(--fg);font:16px/1.55 system-ui,Segoe UI,sans-serif;max-width:980px;margin:0 auto;padding:24px 16px 64px}
h1{font-size:28px;margin:0 0 8px}h2{margin-top:40px;border-bottom:1px solid var(--line);padding-bottom:4px}h3{margin-top:28px}
code{font:13px/1.4 Consolas,monospace;background:rgba(127,127,127,.15);padding:1px 4px;border-radius:4px}
a{color:var(--acc)}figure{margin:16px 0}img{max-width:100%;border-radius:6px;border:1px solid var(--line)}
figcaption{color:var(--mut);font-size:14px;margin-top:4px}table{border-collapse:collapse;margin:12px 0;width:100%}
th,td{border:1px solid var(--line);padding:6px 8px;text-align:left;vertical-align:top}th{background:rgba(127,127,127,.1)}
img.thumb{width:160px;max-width:100%;display:block;margin:2px 0}
@media (max-width:700px){table{font-size:13px}img.thumb{width:110px}}"""
    page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Haarklous UE5 report</title><style>{css}</style></head><body>{"".join(out)}</body></html>'
    open(OUT, 'w', encoding='utf-8').write(page)
    print('wrote', OUT, f'{len(page) / 1e6:.1f} MB')


main()
