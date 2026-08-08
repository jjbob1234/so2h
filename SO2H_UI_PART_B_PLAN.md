# SO2H Part B — port the quest bar onto so2h_ui

Status: DRAFT, awaiting Jay's approval.
Predecessor: Part A landed as `5510fac80`, CI run `31233825343` green 2026-08-08 02:13 UTC.

---

## 0. The blocker nobody has hit yet

`rg "So2hUiDesc" mm` finds **zero** descriptor tables in C. Every `So2hUiDesc` reference in
`mm/2s2h/Menu/*.c` is the engine *reading* a table — nothing anywhere *authors* one.

The 107-node pause scene lives **only** in `tools/scenes/pause.py`. Every render, GIF and
rect dump Jay has approved (pause38 through pause44) came out of Python. The C engine has
never laid out a single real node.

So Part B is really two jobs, and job 1 has to be settled first:

1. **Get the scene into C.**
2. **Hook it into kaleido and retire the old quest-bar drawing.**

### Option 1 — generate the C table from `pause.py` (RECOMMENDED)

New `tools/so2h_gen_scene.py`, mirroring `so2h_gen_sheets.py`:

- imports `tools/scenes/pause.py`, calls `build(aspect)` for both canon aspects,
- emits `mm/2s2h/Menu/so2h_ui_scene_pause.c` + `.h`: one `static const So2hUiDesc
  sPauseDesc[]` with the 107 rows, plus the `So2hUiVariant[]` carrying the 4:3 deltas
  (the 7 nodes that only exist at 16:9 become variant-gated rows),
- emits a `So2hUiId` enum from the node names, so `SO2H_UI_QUEST_SLOT` etc. are real
  identifiers rather than magic indices,
- re-emits on every `python3 tools/so2h_gen_scene.py`, same as the sheets generator,
- CI-checkable: `--check` re-generates to a temp file and diffs, so a stale table fails
  the build instead of silently drifting.

Cost: ~350 lines of generator. Pays for itself immediately — the Python renderer, the anim
harness, the sim sweep and the game all read the *same* authored numbers, so an approved
GIF is a guarantee about the game and not a hope. Jay keeps authoring in `pause.py`, which
is where every scene edit has happened for six sessions running.

### Option 2 — hand-author the C table, keep Python as a preview

Cost: ~700 lines of hand-written struct initialisers, and two sources of truth forever.
Every future tweak has to be made twice and eyeballed for drift. The zero-drift rect
regression that has protected this scene since pause43 stops meaning anything, because it
only ever checks Python against Python.

Not recommended, listed for completeness.

---

## 1. Kaleido integration

Once a table exists:

- `So2h_Ui_Init(sPauseDesc, ARRAY_COUNT(sPauseDesc), sPauseVariants, ...)` once, from the
  same place `So2h_QuestBar_Reset` is called (`z_kaleido_scope_NES.c:3857`).
- `So2h_Ui_Open()` when `So2h_PauseWindow_Update` sees the menu turn on, `So2h_Ui_Close()`
  when it turns off, `So2h_Ui_CloseDone()` gating the state advance so the close animation
  actually gets to play.
- `So2h_Ui_Update()` next to `So2h_PauseWindow_Update` (`:3854`).
- `So2h_Ui_Draw(gfx)` replacing the `So2h_QuestBar_Draw(play)` call at `:3702`.
- `So2h_Ui_Navigate` behind `So2h_QuestBar_UpdateCursor` (`:4796`), preserving the boundary
  contract exactly: `So2h_QuestBar_IsCursorInBar`, `PAUSE_CURSOR_QUEST_BAR_RIGHT` 12,
  `_BOTTOM` 13, `MORPH_FRAMES` 14, `OVERSHOOT` 0.06f, hysteresis 1.45/1.55.

Untouched, per standing rules: Owl Warp, `R_PAUSE_WORLD_MAP_YAW`/`_DEPTH`, the map-art fix
at `z_kaleido_scope_NES.c:1123-1142`, `z_kaleido_collect.c`, and `PauseMenuPage` ordering.

## 2. Content ported onto the API

- Quest grid → `SO2H_SHEET_SHEETSLOTITEM` cells; Bombers' Notebook is the one `BigButton
  SQUARE`.
- Songs split onto their own page; page switching by the two bottom-corner `MiniButton`
  arrows (never L/R).
- Remaining pages (Equipment, Items, Masks, Data) declared but hidden day one.
- `so2h_quest_layout.c`'s dual canon tables deleted — code only. **No art is deleted**; all
  43 legacy tiles stay on disk and registered.

## 3. Dev hooks (Jay asked for these here rather than in Part A)

All behind `SO2H_UI_DEV`, defaulting on in Debug and off in Release:

- `gSo2h.Ui.ForceOpen` — hold the menu open outside of pause, for looking at it.
- `gSo2h.Ui.ShowHiddenPages` — reveals the four unfinished pages (`So2h_Ui_SetShowHiddenPages`
  already exists in the engine, currently unreachable).
- `gSo2h.Ui.Replay` — re-run the open animation on the spot with a fresh seed, so the
  clatter and the stagger can be judged without pausing and unpausing.
- `gSo2h.Ui.TimeScale` — 0.1x..1x on the reveal clock, to inspect a single window's travel.
- `gSo2h.Ui.Outlines` — draw each node's solved rect and id, the in-game twin of the
  Python renderer's debug pass.
- `gSo2h.Ui.SfxMute` — kill the pool without touching the game's audio sliders.

Entries added to the existing 2S2H dev menu, alongside the audio settings at
`mm/2s2h/BenGui/BenMenu.cpp:467-525`.

## 4. SFX becomes audible here

The pool from Part A is compiled and wired into `OTRAudio_Thread` but silent, because
nothing calls `So2h_Ui_Open`. It starts making noise the moment section 1 lands — no
further audio work is expected. First real listen is the first Part B build; `SO2H_UI_SFX_BUS`
(0.85) and the two `_VOL` constants are the knobs if the clatter is too loud or too thin.

## 5. Verification

- Eight-file `gcc -fsyntax-only` sweep stays at zero own warnings (MSVC hard-errors unused
  statics).
- `tools/so2h_gen_scene.py --check` clean.
- Zero rect drift vs `/tmp/rects44_16x9.json` / `/tmp/rects44_4x3.json`.
- Part B "no visual change" is judged against the quest bar as it ships today: same to the
  eye, sim asserts within 0.5 design units.
- `tools/so2h_ui_sim.py` extended per the approved plan (aspect sweep 1.20→2.40, clip
  escape, sibling overlap, ring nesting, animation termination) — this is the first build
  where those invariants can actually bite, so they get written before the commit, not after.

## 6. Commit

One commit, `menu: port quest bar onto so2h_ui`, pushed to `so2h-origin
so2h-oot-menu-scaffold`. CI cadence per `AGENT_BUILD_RULES.md`: first check +15 min, then
every 5 min with UTC timestamps, max 3-5 fix attempts then a written failure report. On
green: Windows zip + `CHANGE_SUMMARY_*.md` into `/home/user/so2h_builds/`.

**This is the build where the menu can finally be opened in game.**

---

## Still-open questions, unanswered since pause41/42

1. At 4:3 the context menu sits at y 154.6 (mid-wall), so the bar does not cut its bottom
   border the way it does at 16:9. Leave it, or always tuck it under the bar?
2. `statWin` / `bonusWin` overlap — shorten `statWin`, or raise `bonusWin`'s layer?
3. Thin checkerboard strip at 16:9 around x 190–194 (`gameDeco` right edge 190.05 vs
   `rightWall` left edge 190.30).
4. 4:3 bottom-left wallpaper gap after the song block moved.
5. Your flat paint-bucket `(41,41,27)` square is currently drawn as `ItemBox` `(59,59,26)`
   — no flat-fill art of that colour exists. Want one, or is the ItemBox fine?
