# so2h_ui build tracker

Branch `so2h-oot-menu-scaffold`. Plan: `/home/user/plan.md` (approved).

## Part A - so2h_ui core

| file | state |
|---|---|
| `mm/2s2h/Menu/so2h_ui.h` | written, syntax clean |
| `mm/2s2h/Menu/so2h_ui_internal.h` | written, syntax clean |
| `mm/2s2h/Menu/so2h_ui_node.c` | written, syntax clean |
| `mm/2s2h/Menu/so2h_ui_layout.c` | written, syntax clean |
| `mm/2s2h/Menu/so2h_ui_sheet.c` | written, syntax clean |
| `mm/2s2h/Menu/so2h_ui_draw.c` | written, syntax clean |
| `mm/2s2h/Menu/so2h_ui_nav.c` | written, syntax clean |
| `mm/assets/custom/textures/so2h_menu/sheets.json` | written, generator green |
| `tools/so2h_gen_sheets.py` | written, generator green |
| `mm/assets/so2h_ui_sheets_assets.h` | GENERATED |
| `mm/2s2h/Menu/so2h_ui_sheets.h` / `.c` | GENERATED, syntax clean |
| `tools/so2h_ui_model.py` | written, runs; mirrors the solver + frame-axis splitter |
| `tools/so2h_ui_sim.py` | written, **418603 assertions green** |
| `tools/so2h_ui_render.py` | written, renders 4:3 and 16:9 from the real sheets |

Syntax check command (host, gcc, no MSVC available in sandbox):

```
gcc -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
  -DNDEBUG -Imm/include -Imm/include/PR -Imm/src -Ilibultraship/include \
  -IZAPDTR/ZAPD/resource/type -Imm -Imm/2s2h -Imm/assets -I. mm/2s2h/Menu/<file>.c
```
All seven files pass with zero warnings attributable to them (the BTN_* redefinition
warnings come from `mm/include/controller.h` vs libultraship and are pre-existing).

## Decisions taken while writing draw.c / nav.c

- **Clipping is software, not `gDPSetScissor`.** The scissor command's coordinate fields are
  unsigned 12-bit 10.2 (`mm/include/PR/gbi.h:3817`) and LUS reads them back unsigned
  (`libultraship/src/fast/interpreter.cpp:2416`, `GfxDpSetScissor(uint32_t...)`). Our screen
  space starts at about x = -53 at 16:9, so a scissor there wraps to +970 instead of clipping.
  Clipping is therefore analytic: intersect the destination rect, then advance s/t by the same
  fraction of the source rect. s/t are S10.5 so the crop lands on 1/32 texel, which is what
  keeps a scroll row panning smoothly instead of stepping a whole texel. A hardware scissor
  IS still emitted around `SO2H_UI_CUSTOM` callbacks (clamped to >= 0), because their output
  is opaque to us.
- **`dsdx`/`dtdy` come from the uncropped destination width.** Computing them from the cropped
  width makes art squash as it slides out of its clip window.
- **NINESLICE and RUN are one function with a `repeat` flag.** They only disagree about what
  happens to the cells inside the sheet's declared repeating range: stretch vs repeat-at-
  native-pitch-and-crop-the-last-one.
- **A frame's repeating range comes from the sheet, not the node.** `runColLo..runColHi` /
  `runRowLo..runRowHi` in `So2hUiSheetDef`, fed by sheets.json.
- **Focus is (node, index).** A GRID / SCROLL_ROW / RUN container is one focus target with a
  cell index, not N targets - otherwise a 4x5 grid that costs one descriptor row would cost
  twenty nav nodes. The index is stored in the scroll side-table, which is keyed by node id,
  so every container gets its own remembered cursor for free.
- **`So2h_Ui_Navigate` returns 0 when the move leaves the tree**, which is the existing
  boundary contract with `z_kaleido_scope_NES.c:4796`.
- **Removed `typedef union Gfx Gfx;` from so2h_ui.h.** `Gfx` in `PR/gbi.h:1707` is an
  *anonymous* union, so a tagged forward declaration is a different type and a hard error.
- **Added `So2h_Ui_SetGfxBudget(Gfx*)`** so the core keeps the `so2h_quest_bar.c:423`
  overrun guard without depending on `play`.
- **Added `So2h_Ui_GetFocusIndex(void)`** and made `So2h_UiDraw_Slice` public, so a content
  callback draws through exactly the same emitter as the chrome around it.

## Known limits to revisit
- `So2h_UiDrawQuad` packs s/t as S10.5 in an s16, so a sheet wider or taller than 1023 texels
  would overflow. Every current sheet is well under. Assert this in the generator.
- `SO2H_UI_MAX_SCROLL_ROWS` (16) now also backs grid cursor indices, so it caps *containers*,
  not just scroll rows.


## Answers from Jay (2026-08-07) - these size the static arrays

| question | answer | consequence |
|---|---|---|
| Equipment sizing | 3 horizontal 1-cell strips + 1 vertical 1-cell strip | not a 4x5 grid. Four `SCROLL_ROW` containers: three horizontal, one vertical. `SO2H_UI_MAX_SCROLL_ROWS` 16 is ample. Vertical scroll rows must work, so `SCROLL_ROW` gets an axis flag. |
| `SO2H_UI_MAX_NODES` | 192 is the cap, keep it | no change |
| Page set | Equipment, Items, Masks, Data/stats, **Songs (split out of the quest bar)** | 5 hidden pages + quest bar as page 0 = 6, under `SO2H_UI_MAX_PAGES` 12. Songs moving out of the quest bar is new scope for Part B - it is a *move*, not a redraw, so "looks the same to the eye" still holds page-for-page. |
| Page switching | **two buttons at bottom-left and bottom-right**, not L/R shoulder | the PAGEGROUP tab strip becomes two `CELL` nodes anchored `END`/`END` on y and `START`/`END` on x. They are focusable, so nav reaches them; no new input plumbing. |
| Scope beyond pause | yes, keep it general | confirmed: core stays in `mm/2s2h/Menu/` with zero kaleido includes. Already true. |
| Medallions | OoT rips for all 6, behind a one-line art-source table | dropped the 4 named medallion slices from `sheets.json` rather than mix two art origins on one row |
| Part B strictness | looks the same to the eye, sub-pixel drift fine | the sim asserts within 0.5 design units, not bit equality |

## Sheet manifest results

`python3 tools/so2h_gen_sheets.py` registers **54 sheets: 11 new + all 43 legacy tiles**, none
retired. Bumped `SO2H_UI_MAX_SHEETS` 32 -> 64 to fit.

Run bands are **a single middle cell** on both axes for every frame sheet. A wider band would
alternate two different tiles on the rows where the band is not uniform - the bottom row
`L M N N O` would grow as `L M N M N N O`. One cell grows every row correctly:
`A B B C D` -> `A B [B..] C D`, `E F F F G` -> `E F [F..] F G`, `L M N N O` -> `L M [N..] N O`.

The generator caught one real thing on its first run: `Empty_Frame`'s centre cell is fully
transparent, which would open a hole in the panel as it widens. That is deliberate art (it is
the frame you lay over live content), so the manifest now says `"hollowInterior": true` and the
check is waived for that sheet only. Every other sheet is still guarded.

Also fixed while wiring this up: `So2h_UiCellSrc` now clamps the source rect to the texture.
The 43 legacy pieces are 1x1 sheets at odd sizes (52x35, 35x52, 210x210), and a square `unit`
would have read past the bottom of the non-square ones.

## Host verification (no build required)

The real solver only runs inside a Windows build with a ROM, so three host tools stand in.

`tools/so2h_ui_model.py` is a deliberately dumb Python mirror of `so2h_ui_layout.c` plus
`So2h_UiSolveFrameAxis` from `so2h_ui_draw.c` - same names, same order of operations, same
magic numbers. Everything below shares it, so the sim and the renderer cannot disagree with
each other about where a seam lands.

`tools/so2h_ui_sim.py` sweeps and asserts:

| # | invariant |
|---|---|
| 1 | a node whose clip is not `SPILL` never draws outside the nearest enclosing `CLIP_SELF` rect |
| 2 | two visible same-layer siblings never overlap by more than half a tile on both axes at once |
| 3 | RUN cell starts are monotone in the growth direction and consecutive tiles never separate |
| 4 | the anchored edge of a RUN sits exactly on the parent edge, at every aspect and every frame |
| 5 | the tree reaches a fixed point and stays there - no asymptotic creep, no oscillation |
| 6 | a scroll offset stays inside `[0, count - visible]` |
| 7 | `solve_frame_axis` spans exactly what it should, emits no negative sizes, stays in budget |

Coverage: aspect 1.20 -> 2.40 in 0.01 steps (both with Equipment hidden and shown), a run-span
sweep at 4:3 / 16:9 / 21:9 shrinking the container 0 -> 180 units, a 720-frame continuous
aspect ramp up-down-up so the morph and the run easing are driven through their *transients*
rather than only observed at rest, and every scroll index driven to both ends and back.
**418603 assertions, all green, in 10 s.**

`--selfcheck` prints all 28 mirrored constants beside the C ones scraped out of the headers,
so drift between the model and the build is visible rather than silent.

`tools/so2h_ui_render.py` renders a tree to PNG from the real sheets. It reads the sheet table
out of the *generated* `so2h_ui_sheets.c` rather than `sheets.json`, so if those two ever
disagree the renderer sides with what the build compiles. It reproduces analytic clipping and
the uncropped-destination `dsdx`/`dtdy` rule, so art does not squash as it slides out of a clip
window. `--annotate --clip` overlays node rects, names and clip rects; `--sheet <name> --grid`
dumps an annotated contact sheet with slice indices and the run band marked.

### Two real defects this caught before any build

1. **`SO2H_UI_MIN_QUAD_PX` drift.** The model had 0.25, the C has 0.4f. Caught by `--selfcheck`
   on its first run. Every repeating band would have terminated one sliver later in the mirror
   than in the game, which is exactly the kind of thing that makes a host tool quietly useless.
2. **Sheet-table parse.** `gSo2hUiSheets[SO2H_SHEET_MAX] = {` has the same shape as a
   designated initialiser, so the first real row (`gSo2hSheetFrameFilled`, the primary body
   panel) was being swallowed. 53 sheets parsed instead of 54.

### Confirmed by the sweep, not by reading

- The morph lands exactly on 0.0 and 1.0; it does not creep.
- Screen x0 really does go to -53.33 at 16:9, which is what makes a hardware scissor unusable
  and is why clipping is analytic.
- `solve_frame_axis` with no declared band shrinks to fit and never stretches, which is Jay's
  even-axis rule holding in code.
- Below about 40 units the 5-cell frames drop their middle cell entirely rather than emit a
  zero-width quad. The frame reads as `A B C D` at that size. Degenerate but bounded, monotone
  and terminating - noted rather than changed.
