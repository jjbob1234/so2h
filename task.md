# so2h pause-menu redesign — scaffolding pass

Branch: `so2h-oot-menu-scaffold`  •  Base commit: `8d188ff26`
Plan: `/home/user/plan.md` (approved by user)

## What this pass does
Shrink the whole kaleidoscope scene into an animated top-left window and fill the freed
space with a backwards-L bar: right arm = quest progress (MM + OOT), bottom arm = songs
(display only). Quest page content moved out; its enum slot kept as a mod placeholder page.

## Files touched
| File | Change |
|---|---|
| `mm/2s2h/Menu/so2h_pause_window.h/.c` | NEW. Window animation factor + rect (N64 320x240). Shared by the overlay and z_play.c. |
| `mm/2s2h/framebuffer_effects.c/.h` | NEW `FB_DrawFromFramebufferRect` (top-left origin, black fill first). `FB_DrawFromFramebufferScaled` untouched. |
| `mm/src/code/z_play.c` | Pause bg blit at the `PauseRenderDraw:` label now tracks the window rect; falls back to the vanilla full-screen blit when inactive. Added include. |
| `.../ovl_kaleido_scope/so2h_quest_bar.h/.c` | NEW. L-bar layout, data, navigation, own 2D cursor highlight. Drawn on OVERLAY_DISP. |
| `.../ovl_kaleido_scope/z_kaleido_scope.h` | Added `PAUSE_CURSOR_QUEST_BAR_RIGHT 12` / `_BOTTOM 13`. |
| `.../ovl_kaleido_scope/z_kaleido_scope_NES.c` | Window viewport in `KaleidoScope_SetView`; bar draw + 3D cursor suppression in `KaleidoScope_Draw`; bar cursor hook before the per-page dispatch; window update in `KaleidoScope_Update`; infoPanelVtx `j` guards (x2); `UpdateCursorSize` early-out for bar positions; PAUSE_QUEST -> placeholder page + placeholder cursor; PAUSE_MAP world-map art + `DrawCursor` retuned to `SO2H_HEX_DEPTH`/`SO2H_HEX_SCALE`. |

## Deliberately NOT touched
- `z_kaleido_collect.c` (whole file, incl. `KaleidoScope_DrawQuestStatus` / `KaleidoScope_UpdateQuestCursor`) — left defined, just unreferenced.
- `KaleidoScope_DrawOwlWarpMapPage` and `R_PAUSE_WORLD_MAP_YAW/_DEPTH` — Owl Warp stays on the old 4-face cube geometry; window factor is forced to 0 for owl-warp states.
- `PauseMenuPage` order (STRICTRULES.md rule 19; `tools/gen_pause_geometry.py` does not exist in this tree).
- Song playback / Bombers Notebook main-states — left intact and unreferenced as future hooks.

## Known limitations to state in the delivery summary
- Song **names** are not rendered (note glyph + colour only).
- Bar art is flat colour placeholder quads. Regions: A = bottom arm, B = right arm, C = corner.
- OOT medallions / spiritual stones / Stone of Agony / Gerudo Card render as colour swatches — MM's `icon_item_static` has no OoT art and `OOT_ITEM_ICON_MAX_ID` (0x2C) is below the OoT medallion ids.
- MM heart containers show a count only (no container icon in the archive).
- Nothing in the bar is interactive.
- Widescreen behaviour of a non-full-screen N64-space viewport under LUS is **unverified** — must be checked in-game.

## Verification done locally
- Brace/paren balance: clean on all 5 edited/added C files.
- `gcc -fsyntax-only` with approximated flags: `so2h_pause_window.c`, `framebuffer_effects.c`,
  `so2h_quest_bar.c` produce **zero** errors. `z_kaleido_scope_NES.c` goes 51 -> 52 errors, the
  single new one being a `gSPVertex` `-Wint-conversion` identical in kind to the 20 pre-existing
  ones in that file (artifact of the approximated flags, not a real defect).
- No local build tree exists; real verification is CI.

## Next
1. Commit + push to `so2h-origin so2h-oot-menu-scaffold`.
2. `AGENT_BUILD_RULES.md`: first CI check at **+15 min**, then every **5 min**, each documented with status + timestamp. Max 5 fix attempts.
3. On green: pull the `2ship-windows` artifact, repack under `/home/user/so2h_builds/`, deliver zip + written change summary.
