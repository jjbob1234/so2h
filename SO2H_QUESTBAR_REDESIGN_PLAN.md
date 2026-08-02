# SO2H Quest Bar — Redesign Plan (v2 layout)

Target: `mm/src/overlays/kaleido_scope/ovl_kaleido_scope/so2h_quest_bar.c` (+ new
`so2h_quest_layout.c/.h`, `so2h_quest_data.c/.h`), branch `so2h-oot-menu-scaffold`.

Reference art:
- `image_9YPfY0.png` — your hand-built target composite (the thing to match)
- `image_ibKrTo.png` — your region map (the thing that defines the boxes)
- `image_KYNs39.png` — current build (the thing to fix)

---

## 0. What's actually wrong right now

**a) The hex backdrop is pulling the wrong slice of the OoT quest-status image.**

Verified against soh (`soh/assets/xml/N64_PAL_11/textures/icon_item_static.xml:106-166` and
`z_kaleido_scope_PAL.c:44-47`): the OoT quest page background is **15 IA8 tiles of 80×32**, named
`gPauseQuestStatus<row><col>Tex`, `row` 0-2, `col` 0-4 → a **400×96** image. ROM offsets confirm the
digit order: `00`@`0x5EBC0`, `20`@`0x5F5C0` are `0xA00` apart (exactly one 80×32 IA8 tile), so tiles
sharing the *second* digit are contiguous — second digit is the column, first is the row.

We currently take `03/04/13/14/23/24` (cols 3-4, all rows = the right 160×96) which *should* be the
medallion hexagon. The screenshot shows the treble clef, staff lines, note positions and the
bottom-right song box instead — i.e. content spanning cols 0-4. So either the merged `oot.o2r`
names these differently than soh's XML, or our placement loop transposes them. **This is not
fixable by reading source — it needs the actual bytes.** See task 3.1 (dump tool).

**b) It isn't contained.** The backdrop is drawn as a raw rect that ignores the window frame, and
the medallion ring is laid out on screen coords rather than window-local coords, so icons spill over
the frame edge and one medallion sits completely outside the panel.

**c) I filled the space instead of composing it.** You said compact. I read that as dense. The
target composite has real negative space — panels are separated by a full tile of background, and
the right arm's left column is a *narrow* stack, not a full-bleed grid.

**d) Structural: the whole bar is laid out in N64 320×240 space.** Your mockup uses the sheet at
native 35 px inside a 1917-wide frame — that's 5.84 N64 px per tile. Drawing at N64 scale and
letting Fast3D upscale is exactly why the art looks soft. This changes below (§2).

---

## 1. Region map

Measured off `image_ibKrTo.png` (1917×988). Given both as source fractions and as N64-space rects
for reference; the implementation works in framebuffer pixels (§2).

### Right arm — `x 192..320, y 0..168` N64

| Region | frac x | frac y | N64 rect | Contents |
|---|---|---|---|---|
| **Heart container window** | .09–.475 of arm | .08–.40 | `203,13 → 253,66` | MM heart-container tracker art (`gItemIcons[0x7A + pieces]`, 48×48), hearts + double-defense state |
| **Quest items grid** | .09–.42 of arm | .41–.65 | `205,68 → 245,106` | 3×2 slot grid. **5 live cells + 1 dead cell.** Cells 1-3 = OoT spiritual stones (Kokiri / Goron / Zora); cells 4-5 = Stone of Agony, Gerudo Card |
| **Notebook** | overlaps | overlaps | 1.333× cell, anchored over the dead 6th cell, breaking the panel edge | Bombers' Notebook — separate element, drawn *after* the grid so it overlaps both the null slot and the frame |
| **Remains + Medallions** | .57–.985 of arm | .085–.55 | `265,14 → 318,93` | OoT hex backdrop (contained, §3), radial ring: 6 medallions outer, 4 MM remains inner |
| **Data screen** | .00–.985 of arm | .66–.99 | `192,108 → 318,164` | Recessed stat bars, §6 |

### Bottom arm — `x 0..320, y 168..240` N64

| Region | N64 rect | Contents |
|---|---|---|
| **Song selection** | `9,172 → 74,189` | 11×2 = 22 note cells, framed strip |
| **Song preview** | `5,190 → 85,238` | Staff + clef panel: song **name** + its note buttons placed on the staff |
| **Content area (placeholder)** | `91,168 → 317,221` | Framed panel, temporary grid of selectable empty cells |
| **Toolbar / menubar (placeholder)** | `90,223 → 316,239` | Framed strip, temporary selectable empty buttons |

Deliberate empty space: the gutter between the left column and the Remains window, the strip below
the Remains window, and the right end of the data screen stay background-only. Do not fill them.

---

## 2. Layout engine: native tiles + responsive morph

This replaces the current "N64 coords through `So2h_MapX`" approach for the bar only (the pause
background and vanilla pages are untouched).

**2.1 Pixel-space layout.** New `So2hLayout` struct computed once per frame from
`OTRGetGameRenderWidth()/Height()`. All rects are framebuffer pixels. `So2h_MapX` stays only for
the two arm background panels so they keep aligning with the shrunken pause background.

**2.2 Tile scale.** `tileScale = clamp(round(fbHeight / 988.0f * 35.0f) / 35.0f, ...)` snapped so
tiles land on whole pixels wherever possible. Sheet tiles are **tiled, never stretched** —
nine-slice frames repeat their edge runs. Only the data-screen recess bars and the two placeholder
panels stretch, and only along their long axis.

**2.3 Two canon layouts.**
- `LAYOUT_WIDE` — the mockup, ≥ 1.55 aspect. Data screen is a 4-column band; song grid 11×2;
  content area sits right of the song preview.
- `LAYOUT_43` — ≤ 1.45 aspect. Right arm gets relatively wider, so: data screen becomes 2 columns
  and taller (scrolls if needed); song grid becomes 6×4; the content-area placeholder moves *below*
  the song preview and the menubar spans full width.
- Between 1.45 and 1.55: hysteresis band, whichever layout is currently active stays active.

**2.4 Morph.** Both layouts are evaluated every frame. A `morphT` (0..1) eases between them with a
~14-frame ease-in-out when the band is crossed. Per-element behaviour:
- Element exists in both → lerp rect + slide.
- Element only in one → scale/alpha out toward the nearest shared neighbour's edge, so it looks
  like it slid *under* it (mask, not pop). Scissor to the parent panel while morphing.
- Squash-and-stretch: elements overshoot their target rect by ~6 % on the leading axis for ~4
  frames, then settle. Cheap, sells the slinky feel, no extra draws.
- Resizing the window mid-pause morphs live; opening the pause menu uses the existing slide-in and
  starts already morphed.

**2.5 Cost.** Batched loads (below) plus one shared layout pass. Budget guard from the crash fix
stays in place.

---

## 3. Hex backdrop — contain and re-region

**3.1 Dump tool first.** Add a hidden developer action (`DeveloperTools.cpp`, next to
`ValidateOotIconAssetsFor100PercentSave`) that walks all 15 `gPauseQuestStatus*` names, resolves
them through `OotAssets::ResolveOotPath`, and writes each to `so2h_dump/` as PNG plus a stitched
5×3 contact sheet. **You run it once and send me the contact sheet.** That settles the row/col
question in one shot instead of me guessing again.

**3.2 Sub-tile crop.** The hexagon almost certainly doesn't align to 80×32 boundaries. Once the
contact sheet identifies it, the draw uses per-tile `uls/ult/lrs/lrt` so we take the exact hexagon
bounding box, not whole tiles. New helper `So2h_DrawTexRectIA8Sub()`.

**3.3 Containment.** The backdrop draws into a scissored, window-local rect inset by one frame tile
from the Remains window's inner edge. The radial ring is positioned from the *window* centre with a
radius derived from the window's inner half-height, so medallions can never leave the frame at any
scale or aspect. Ring geometry: 6 medallions on the outer hexagon vertices (matching OoT's angles),
4 MM remains on an inner diamond.

**3.4 Fallback.** If the tiles aren't in the user's `oot.o2r`, the window falls back to the MM
sheet's `gSo2hWindow` recess — already wired by the existence checks shipped in `49def976e`.

---

## 4. Quest items grid + notebook

- 3×2 grid, cell = one `gSo2hSlotDark` tile. Cells 0-4 live, cell 5 **dead**: drawn as recess only,
  skipped by navigation.
- Contents: Kokiri Emerald, Goron's Ruby, Zora's Sapphire, Stone of Agony, Gerudo Card.
- Notebook is a **separate element**, `1.333 × cellSize`, anchored so its centre sits on the dead
  cell's centre and it overhangs the grid frame bottom-right. Drawn last, own drop shadow, own
  cursor rect. Navigation: reachable from cell 4 (right) and cell 2 (down).
- Unowned entries draw the recess + `gSo2hCrossRed` at 40 % alpha, not a blank.

---

## 5. Song selection + preview

- 22 cells, 11×2 (wide) / 6×4 (4:3). Owned = `gSo2hNoteWhite` tinted per song colour; unowned =
  `gSo2hNoteLocked`.
- Preview panel: `gSo2hStaffClef` + `gSo2hStaffLines`, extended by one tile if the longest song
  name needs it.
- Shows **song name** (existing kaleido text path) plus its note buttons placed **on the staff at
  the correct pitch offsets**, matching how both games' quest screens position them —
  `gOcarinaSongButtons[]` gives the button sequence, a small per-button y-offset table gives the
  staff line. A/C-up/C-down/C-left/C-right glyphs from `parameter_static` (IA8 16×16), coloured per
  `sOcarinaButtonColors`.
- Songs learned implicitly (Double Time, Inverted Time) count as owned via the existing
  `SONG_RULE_FROM_TIME` path.

---

## 6. Data screen

Framed panel, recessed bars, tiled frame + stretched bars. Wide: 4 columns; 4:3: 2 columns with
vertical scroll (cursor-driven, no scrollbar art needed). Bars auto-size to their label.

Stat list (label — source):

**Time & file**
- Play time — `shipSaveInfo.filePlaytime`
- Current day — `save.day` / 3
- Current time (Termina) — `save.time` → HH:MM
- Hyrule time + Day/Night — `save.time` mapped to OoT's clock, with a Day/Night marker
- Three-day resets — `playerData.threeDayResetCount`
- File created / completed — `fileCreatedAt` / `fileCompletedAt`

**Completion**
- Termina completion % — computed
- Hyrule completion % — computed
- Masks — count of owned masks / 24
- Remains — / 4
- Medallions — / 6 · Spiritual Stones — / 3
- Songs learned — / total (implicit songs included)
- Heart pieces — `pieces` / 4 · Heart containers — `healthCapacity / 16`

**Collectibles**
- Skulltula Hyrule — `so2h.oot.inventory.gsTokens` / 100
- Skulltula Swamp — `skullTokenCount >> 16` / 30
- Skulltula Beach — `skullTokenCount & 0xFFFF` / 30
- Stray Fairies: Clock Town / Woodfall / Snowhead / Great Bay / Stone Tower — `strayFairies[]`,
  each / 15 (Clock Town / 1)
- Owl statues — popcount(`owlActivationFlags`) / 10
- Regions visited — popcount(`regionsVisited`)
- Pictographs taken — `pictoFlags0`
- Bombers caught — `bombersCaughtNum` / 5

**Resources & upgrades**
- Rupees — `playerData.rupees` / wallet capacity (both games' wallets shown separately)
- Magic — `magicLevel` (none / single / double)
- Defense — `doubleDefense`
- Razor sword durability — `swordHealth`
- OoT upgrades — quiver / bomb bag / scale / strength / wallet, from `so2h.oot.inventory.upgrades`
- OoT dungeon items — maps, compasses, boss keys, total small keys
- Bank rupees — `HS_BANK_RUPEES`
- Deku playground high scores — `dekuPlaygroundHighScores[3]`

Numbers use `Gfx_DrawTexRectI8` on `sCounterTextures[]` (format fixed in `49def976e`). Labels use
the kaleido text path. Any stat whose backing data is absent (no OoT merge) is skipped and the
column reflows rather than showing `0`.

---

## 7. Placeholder panels

Both drawn as real framed panels with a grid of empty cells. Cells are **selectable regardless of
being empty** — they enter the nav graph, take the cursor, and show the highlight, but do nothing on
A. That way the nav layout is already proven when real content lands.

---

## 8. Draw-cost work (carried over)

Batched loads land as part of this rewrite, not as a separate pass: `So2h_LoadTexRGBA32()` +
`So2h_TexRectPreloaded()`, one load per repeated tile per pass. The song grid alone drops from
~26 cmds × 22 cells to three passes. The `sGfxBudgetEnd` guard and the enlarged `overlayBuffer`
stay.

---

## 9. Phases

1. **Layout engine** — `so2h_quest_layout.c`, both canon layouts, morph, tile-scale. No content
   changes; existing elements re-hosted on the new rects so it's visually verifiable.
2. **Right arm** — heart window, quest grid + notebook, Remains window with containment. Hex
   backdrop uses the fallback recess until the contact sheet arrives.
3. **Hex re-region** — after you send the dump.
4. **Bottom arm** — song grid, preview panel with names + staff-positioned buttons, placeholders.
5. **Data screen** — layout + full stat list.
6. Build, deliver, iterate.

Each phase = its own commit + CI run + delivered Windows zip, per `AGENT_BUILD_RULES.md`.

---

## 10. Risks

- **Hex tiles** — blocked on the dump; phase 3 can't be guessed a third time.
- **4:3 data screen** — 30+ stats in 2 columns will need scrolling; if you'd rather it paginate,
  say so before phase 5.
- **Song name width** — kaleido's text path is fixed-width; the longest MM song names may force the
  preview panel one tile wider than the mockup.
- **Pixel-space rewrite** — this touches every rect in the file. It is the right call for art
  fidelity but it's the largest single change here, hence phase 1 in isolation.
