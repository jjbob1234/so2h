# SO2H — Remains window hex-block re-region + SoH-accurate medallion placement

Status: PLANNED (supersedes phase 3 of SO2H_QUESTBAR_REDESIGN_PLAN.md, and unblocks it —
no in-game texture dump is needed any more, the geometry is resolved from soh source).

## 1. The bug: the tile name digits are transposed

`gPauseQuestStatus<A><B>Tex`, 15 tiles, IA8 80x32.
Derived from `soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c`
`func_80823A0C()` :2802-2814 — the page-background vertex builder:

```
phi_t0 = -200;
for (row = 0; row < 3; row++) {          // outer  -> texture name digit A
    phi_t0 += 80;                        // x = -120, -40, +40
    for (y = 80, c = 0; c < 5; c++, y -= 32) {   // inner -> texture name digit B
        x0 = phi_t0;  x1 = x0 + 80;
        y0 = y;       y1 = y0 - 32;
```
Texture array order is `00 01 02 03 04 10 11 ...`, i.e. index `i` -> name `A=i/5`, `B=i%5`,
and the loops assign `A` to the **x** step and `B` to the **y** step.

=> **A = screen COLUMN (0,1,2), B = screen ROW (0..4).** Not row-major.

Full bg = 3 cols x 5 rows of 80x32 = **240 x 160**, page-local rect x -120..120, y -80..80.

Consequences for the current code:
* `sHexTileArt[6]` = `03 04 13 14 23 24` is **wrong** — those are col0 row3, col0 row4,
  col1 row3, col1 row4, col2 row3, col2 row4: the *bottom two rows across all three
  columns*, which is why the screenshot showed the treble clef / staff / bottom-right box
  instead of the medallion hexagon.
* The `OotItemIcons.h` comments ("top-left of the 2x3 hexagon block") are wrong and get
  corrected in this pass.

## 2. Correct block: Jay's 2x4

Take columns 1-2, rows 0-3 (column 0 is the page title art — skipped):

| screen row | left tile (col 1) | right tile (col 2) |
|---|---|---|
| 0 | `gPauseQuestStatus10Tex` | `gPauseQuestStatus20Tex` |
| 1 | `gPauseQuestStatus11Tex` | `gPauseQuestStatus21Tex` |
| 2 | `gPauseQuestStatus12Tex` | `gPauseQuestStatus22Tex` |
| 3 | `gPauseQuestStatus13Tex` | `gPauseQuestStatus23Tex` |

8 tiles, pairs left-to-right, stacked 4 tall (Jay's option D).
Block source size = **160 x 128**, aspect 1.25.
Page-local rect it corresponds to: x **-40 .. 120**, y **80 .. -48** (y down = decreasing).

Note `gPauseQuestStatus10Tex` is language-variant in soh (`10FRA/10GER/10ENG/10JPN`).
oot.o2r carries the plain `gPauseQuestStatus10Tex` name for the base (JPN-less) variant —
resolve `10` first, and if `OotFileExists` fails, fall back in order
`gPauseQuestStatus10ENGTex` -> `gPauseQuestStatus10Tex` -> skip that one tile.
Every other tile in the block is language-neutral.

## 3. Code changes

### `mm/2s2h/OotItemIcons.h`
Replace ids 14-19 with 8 + 1 fallback, keeping the enum **append-only after 13** (nothing
outside this file indexes 14+):
```
/* 14 */ OOT_QUEST_ART_HEX_TILE_10,
/* 15 */ OOT_QUEST_ART_HEX_TILE_20,
/* 16 */ OOT_QUEST_ART_HEX_TILE_11,
/* 17 */ OOT_QUEST_ART_HEX_TILE_21,
/* 18 */ OOT_QUEST_ART_HEX_TILE_12,
/* 19 */ OOT_QUEST_ART_HEX_TILE_22,
/* 20 */ OOT_QUEST_ART_HEX_TILE_13,
/* 21 */ OOT_QUEST_ART_HEX_TILE_23,
/* 22 */ OOT_QUEST_ART_HEX_TILE_10_ENG,   // language fallback for 10
/* 23 */ OOT_QUEST_ART_MAX
```
Add, so no caller hardcodes the block shape:
```
#define OOT_QUEST_ART_HEX_BLOCK_COLS 2
#define OOT_QUEST_ART_HEX_BLOCK_ROWS 4
#define OOT_QUEST_ART_HEX_BLOCK_W (OOT_QUEST_ART_HEX_TILE_W * OOT_QUEST_ART_HEX_BLOCK_COLS) // 160
#define OOT_QUEST_ART_HEX_BLOCK_H (OOT_QUEST_ART_HEX_TILE_H * OOT_QUEST_ART_HEX_BLOCK_ROWS) // 128
```
Rewrite the stale "2x3 hexagon block" comment with the transposition finding above.

### `mm/2s2h/OotItemIcons.cpp`
`kOotQuestArt[]` entries 14-22 -> the 8 paths in section 2 order + `gPauseQuestStatus10ENGTex`.

### `so2h_quest_bar.c`
* `sHexTileArt[6]` -> `sHexTileArt[8]`, in the section-2 order (index `i`: col `i % 2`,
  row `i / 2`) — the existing `i % 2` / `i / 2` walk in `So2h_DrawHexWindow` is already
  the right walk, only the table and the tile count change.
* Aspect fit: `160x96` -> `OOT_QUEST_ART_HEX_BLOCK_W/H` (160x128), `tileH = h / 4.0f`.
  Keep the fit-to-shorter-axis logic so the block never spills the window frame.
* Tile 0 (`HEX_TILE_10`): if `OotQuestArt_GetPath()` returns NULL, retry
  `OOT_QUEST_ART_HEX_TILE_10_ENG` before skipping.

## 4. SoH-accurate medallion placement

Medallion slots are quest slots 0-5, positioned by
`D_8082B138[]` (x0) :2984 and `D_8082B198[]` (y0) :2990, size `D_8082B1F8[]` = 24:

| slot | medallion | x0 | y0 | centre x | centre y |
|---|---|---|---|---|---|
| 0 | Forest | 74 | 38 | 86 | 26 |
| 1 | Fire   | 74 |  6 | 86 | -6 |
| 2 | Water  | 46 | -12 | 58 | -24 |
| 3 | Spirit | 18 |  6 | 30 | -6 |
| 4 | Shadow | 18 | 38 | 30 | 26 |
| 5 | Light  | 46 | 56 | 58 | 44 |

(centre = x0+12, y0-12; page-local space, y up.)

Normalised into the 2x4 block rect (x -40..120, y 80..-48), with
`u = (x + 40) / 160`, `v = (80 - y) / 128` (v down):

| slot | medallion | u | v |
|---|---|---|---|
| 0 | Forest | 0.7875 | 0.421875 |
| 1 | Fire   | 0.7875 | 0.671875 |
| 2 | Water  | 0.6125 | 0.812500 |
| 3 | Spirit | 0.4375 | 0.671875 |
| 4 | Shadow | 0.4375 | 0.421875 |
| 5 | Light  | 0.6125 | 0.281250 |

Icon size 24 -> `0.15` of block width, `0.1875` of block height.
Hexagon centre lands at `u 0.6125, v 0.546875` — deliberately right of the block centre, that
is genuine OoT geometry (the hexagon lives on the right of the quest page), do not "fix" it to
0.5. The vertex ring is 56 x 68 page units centre-to-centre, i.e. TALLER than wide.

CORRECTION 2026-08-10: an earlier revision of this table had Spirit/Shadow at `u 0.3375` and
the centre at `u 0.5625`. Both were arithmetic slips (`u = (x + 40) / 160`, x = 30 -> 0.4375)
and made the hexagon 29% too wide. Verified against an in-game capture.

Implementation: replace `sMedallionAngles[6]` polar placement with a UV table

```c
// u,v within the drawn hex-art block; lifted from OOT's own quest-slot vertex tables so the
// medallions sit exactly on the hexagon vertices the art draws.
static const f32 sMedallionUV[6][2] = {
    { 0.7875f, 0.421875f }, // Forest
    { 0.7875f, 0.671875f }, // Fire
    { 0.6125f, 0.812500f }, // Water
    { 0.4375f, 0.671875f }, // Spirit
    { 0.4375f, 0.421875f }, // Shadow
    { 0.6125f, 0.281250f }, // Light
};
#define MEDALLION_U_FRAC 0.15f
#define MEDALLION_V_FRAC 0.1875f
```
and drive it off the **same** `art` rect `So2h_DrawHexWindow` computed for the block, so art
and icons can never drift apart. Therefore: `So2h_DrawHexWindow` must publish the block rect
(`static So2hRect sHexBlockRect;` written there, read by `So2h_DrawRadialIcons`) and must be
drawn before the icons — it already is, `So2h_QuestBar_Draw` :1609-1610.

The `gSo2hRoundTile` disc backing stays under each medallion, sized to the icon rect.

## 5. Boss remains (MM's 4) — inner diamond, unchanged arrangement

Radial as today (`sRemainsAngles[4] = {135,45,315,225}`), but centred on the **hex block
centre** (`u 0.6125, v 0.546875` of the block) instead of the window centre, at
`0.42 *` the medallion ring radius (measured per axis, so the inner ring inherits the
hexagon's tall proportion) so they sit inside the hexagon and can never touch a vertex. Keeps the Odolwa/Goht/Gyorg/Twinmold order.

## 6. Verification before commit
* `sHexTileArt` count == 8 and every id `< OOT_QUEST_ART_MAX`.
* `kOotQuestArt[]` length == `OOT_QUEST_ART_MAX` (compile-time via `static_assert` on the
  array, add one).
* No remaining reference to `OOT_QUEST_ART_HEX_TILE_03/04/14/24` (`rg -c`, never `-r`).
* `sMedallionAngles[6]` removed, not left unused (MSVC unused-static).
* `OOT_ITEM_ICON_MAX_ID` still `0x2C`; `PauseMenuPage` order untouched.

## 7. Open questions still owed by Jay
1. 4:3 data screen with 30+ stats — cursor-driven scroll in 2 columns, or pagination?
2. Collapse `kOotItemArt[]` + `kOotQuestArt[]` behind one generic
   `OotAssets_GetPath(const char* relPath)`?
