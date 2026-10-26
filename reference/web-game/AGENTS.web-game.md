# Haarklous plass open-world game — continuation instructions

Read `docs/HANDOFF.md` (CURRENT STATE first) before continuing after a context reset or compaction.

User preferences established in this chat:

- Ask **one question at a time**; when asking about something visual, show the corresponding image in the same message (absolute local path in Markdown).
- The user wants us to build and maintain the game, not merely give instructions. Preserve every confirmed decision in files as work proceeds.
- The project is now an open-world game (24 October 2026). The measured-apartment viewer, 2D drawings, measurement ledgers and layout studies were moved to `C:\Users\Lenovo\Documents\haarklous-plass-archive-2026-10-24` at the user's request; do not reintroduce them into this folder.
- Keep the apartment as "your kitchen + bedroom design" (frozen in `apartment-data.js`); kitchen and bedroom design must not change, only finishes.
- Primary input is a PS4 DualShock 4 (USB or Bluetooth); keep keyboard/mouse working too. Keep controls documented in README.md and the in-game start/pause screens.
- Performance target 20–30 fps or better; keep static geometry merged (`lib.mergeStatic`) and avoid coplanar surfaces (they caused visible flicker).

Multiplayer server ("Haarklous online", renamed at the user's request 24 Oct 2026): Cloudflare Worker `haarklous-online` at https://haarklous-online.server-smoke.workers.dev (user asked for Cloudflare, no GitHub, no own domain; account 46fd597da1c5fe9de4c3ebbaae86f906). After changing game files, rebuild and redeploy from `cloud/` with `npx wrangler deploy` so the online version matches; keep this single URL.

Workflow: edit sources, run `python build.py` (writes `haarklous-game.html` and `cloud/public/index.html`), run `node --test tests/*.cjs`, verify in the browser with `tests/browser-harness.js`. Clean up disposable test files and obsolete exports after use (authorised by the user). Keep one canonical game file; do not publish test URLs.

Source code: private GitHub repo https://github.com/jonatsl/haarklous-online (branch `main`, created 25 Oct 2026 at the user's request). Keep commit messages and descriptions in very simple human language with bullet points. Build outputs (`dist/`, `cloud/public/`) and local tool folders are in `.gitignore`.
