# Part B working scratchpad

Plan: `SO2H_UI_PART_B_PLAN.md`. Approved by Jay: generate the C table from `pause.py`,
ALL SIX dev hooks, open questions 1-5 deferred until he can see it in game.

Predecessor: Part A = `5510fac80`, CI `31233825343` green 2026-08-08 02:13 UTC.

## Progress

- [x] **Gap found:** Part A's C engine had no per-node art `scale`, but `pause.py` uses it
      everywhere (`dataRows` derives `s = h / (3*T64)`; every framed window states one).
      Without it, ring insets and cell sizes are wrong for every scaled node.
- [x] `f32 scale` added to `So2hUiStyle` (`so2h_ui.h`), documented as derive-from-size.
- [x] Ring solving honours the PARENT's scale: `so2h_ui_layout.c` passes
      `ctx->tile * pd->style.scale` to `So2h_UiSheet_RingPx`. Mirrors `so2h_ui_model.py:436`.
- [x] Draw side threaded: `So2h_UiCellScreen(sheet, scale)`, `So2h_UiDrawFrame(..., scale)`,
      `So2h_UiDrawTiled(..., scale)`, `So2h_UiDraw_Slice(..., f32 scale)`; tree walk passes
      `d->style.scale`.
- [x] Removed a verbatim DUPLICATE declaration of `So2h_UiDraw_Slice` in `so2h_ui.h`
      (it was declared twice, identical comment and all).
- [x] Re-swept the 8 engine files after the `scale` edits: zero own warnings (only the
      pre-existing `BTN_*` redefinition noise from libultraship's controller.h).
- [x] Zero rect drift after the parent-scale ring change: 107 nodes @16:9, 100 @4:3.
- [x] `tools/so2h_gen_scene.py` RUNS: 107/100 nodes, 10 sparse canon overrides, `--check`
      clean, generated `.c` compiles with zero own warnings. Four fixes on first run:
      - sheets are named by asset symbol in the scene, so the enum map is built from
        `sheets.json` with `so2h_gen_sheets`' own rule (`name[5:].upper()`) instead of a
        non-existent `Sheet.id` attribute; slices resolve to `SO2H_SLICE_<SHEET>_<NAME>`.
      - node ids are prefixed `SO2H_PAUSE_`, NOT `SO2H_UI_`: the node called `root` would
        otherwise spell `SO2H_UI_ROOT`, which is already a `So2hUiKind` enumerator, and the
        table would have compiled meaning the wrong thing rather than failing.
      - built at 16:9 and 4:3, not `M.ASPECT_WIDE`/`M.ASPECT_43` — those are the morph
        THRESHOLDS (1.55/1.45) and building at them gave a short toolbar (104/102 nodes).
      - `ARRAY_COUNT` is not reachable from that TU; the accessors use `sizeof`.
- [x] Kaleido hooks (Init/Open/Close/CloseDone/Update/Draw/Navigate), boundary contract
      preserved exactly. `So2h_QuestBar_IsCursorInBar` and the two special positions (12/13)
      are unchanged; `So2h_QuestBar_UpdateCursor` now delegates every in-menu move to
      `So2h_Ui_Navigate` and hands a failed move back to vanilla exactly as before.
- [x] `so2h_quest_bar.c` gutted: 23 chrome drawers, the 19-entry neighbour table,
      `So2h_QuestBar_Navigate` and `So2h_QuestBar_Draw` deleted along with the art tables
      only they used. 1607 -> ~1030 lines, zero unused statics.
- [x] Content bound at RUNTIME, not generated. `So2h_Ui_BindDraw` is the seam: the generated
      table is `static const` in `mm/2s2h/Menu` and must never name a kaleido symbol.
      Callbacks: `So2h_Content_QuestSlot` (6), `_Heart`, `_Notebook`, `_SongCell` (12),
      `_SongStaff`. One `So2h_QuestBar_Bind()` table, installed via
      `So2h_PauseMenu_SetBindHook` from `So2h_QuestBar_Reset` so the ordering cannot race.
- [x] Cursor highlight moved to `so2h_pause_menu.c`, driven off `So2h_Ui_GetFocus()` rather
      than a bar-local index. Same look: `+= 0x400`, `160 + 95*Math_SinS`, RGB 255/255/160,
      four 1px edges. Whether the cursor is in the menu is PUSHED in by the overlay
      (`So2h_PauseMenu_SetCursorInMenu`) because the two special positions are overlay-private.
- [x] `so2h_quest_layout.c/.h` DELETED outright, not just trimmed - with the solver gone the
      whole file was dead and only the `So2hRect` type was still referenced. Content now uses
      `So2hUiRect` directly, so the converter went too. Code only; no art touched.
- [x] All six dev hooks live, registered under Dev Tools -> SO2H Menu in `BenMenu.cpp`:
      `ForceOpen`, `ShowHiddenPages`, `Replay` (counter + button), `TimeScale` (0-4x slider),
      `Outlines`, `SfxMute`. TimeScale is whole animation STEPS in front of the advance block,
      so slowing the entrance cannot move where anything lands; 0.0f freezes it.
- [x] Verify: 11-file syntax sweep at ZERO non-`BTN_` diagnostics (the ten engine files plus
      `so2h_quest_bar.c`); `so2h_gen_scene.py --check` and `so2h_gen_sheets.py --check` clean;
      zero rect drift (107 @16:9, 100 @4:3); `so2h_ui_sim.py` 418,603 assertions OK and
      `--selfcheck` 28/28 constants matched. `z_kaleido_scope_NES.c` compared against HEAD:
      no new diagnostics (its host-sweep noise is pre-existing vanilla GBI noise).
- [ ] Commit `menu: port quest bar onto so2h_ui`, push, CI cadence, zip + summary.

### Deferred out of Part B (raise with Jay after his first in-game look)

- Songs 12-21 and the `SONG_PAGE_L`/`SONG_PAGE_R` arrows. The 12 declared cells map to songs
  0..11. Page flip needs button plumbing that `So2h_QuestBar_UpdateCursor` does not have.
- Open questions 1-5 from the plan (4:3 `ctxWin` border, `statWin`/`bonusWin` overlap, the
  thin checkerboard strip at 16:9 x 190-194, the 4:3 bottom-left wallpaper gap, `ItemBox` vs
  the flat `(41,41,27)` square).
- Song *names* are still not rendered; the medallions and boss remains land with the hex
  block in Part C.

## Notes / decisions

- Default `scale` must be **1.0f, not 0.0f** — a zeroed table row means canon size. Every
  emitted row states it explicitly, and both C readers clamp `<= 0` to 1.0f, so a
  hand-written row that forgets it still behaves.
- `So2h_UiDraw_Slice` is public API; the `scale` param went on the END so existing content
  callbacks read naturally. There are currently no external callers to fix.

## Standing rules in play

- No new/edited art by me. Nothing gets deleted (code-only removals allowed).
- Never reorder `PauseMenuPage`. Don't touch Owl Warp, the map-art fix, `z_kaleido_collect.c`.
- MSVC hard-errors unused statics, so the syntax sweep must stay at zero own warnings.
- CI cadence per `AGENT_BUILD_RULES.md`: +15 min, then every 5 min, UTC timestamps.
