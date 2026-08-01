#ifndef SO2H_QUEST_BAR_H
#define SO2H_QUEST_BAR_H

#include "global.h"

/*
 * SO2H [Menu] Backwards-L quest / song bar.
 *
 * The pause scene is shrunk into a window in the top-left of the screen
 * (2s2h/Menu/so2h_pause_window.h). This module owns everything that fills the space that
 * frees up:
 *
 *   +---------------------------+--------+
 *   |                           |        |
 *   |      pause window         | right  |  right arm  = the merged OOT + MM quest page
 *   |   (kaleido page prism,    |  arm   |
 *   |    info panel, cursor)    |        |
 *   |                           |        |
 *   +---------------------------+--------+
 *   |            bottom arm, full width  |  bottom arm = all 22 songs + button strip
 *   +------------------------------------+
 *
 * The bar is drawn in 2D screen space on OVERLAY_DISP, NOT inside the pause perspective
 * view, for two reasons:
 *   - anything drawn in the pause 3D space would shrink along with the window,
 *   - the extents of the pause 3D space at z=-50 depend on the aspect ratio
 *     (halfH = 114 * tan(30deg) ~= 65.8, halfW = halfH * aspect) and the existing
 *     cursorX = 101.0f for PAUSE_CURSOR_PAGE_RIGHT already sits outside the 4:3 bounds,
 *     so that space is not a reliable place to lay out a screen-edge bar.
 *
 * Contents. Everything is real art driven by real save data; there are no colour-swatch
 * placeholders left. Equipment (sword, shield, quiver, bomb bag, wallet) is deliberately
 * absent - MM's equipment screen owns it.
 *
 * Right arm (192..320 x 0..168):
 *   - Hexagon sub-window, a thin nine-slice frame from the SO2H menu sheet holding OOT's
 *     own quest-page hexagon line-art: the six 80x32 IA8 gPauseQuestStatus tiles
 *     (03/04/13/14/23/24) stacked 2 wide x 3 tall into one 160x96 image and scaled down.
 *     MM-sourced art needs no added border, OOT-sourced art must be small pieces inside a
 *     sheet-built window - this is that window.
 *   - Two concentric rings sharing the hexagon's center: OOT's six medallions on the
 *     vertices (radius 33, starting at the top and running clockwise in OOT's own order),
 *     MM's four boss remains on an inner ring (radius 16) offset 45 degrees so they land in
 *     the hexagon's gaps and can never collide with a medallion.
 *   - Row A: Kokiri Emerald, Goron Ruby, Zora Sapphire, Stone of Agony.
 *   - Row B: Gerudo's Card, Bomber's Notebook, heart tracker (MM's own 48x48 IA8
 *     gItemIcons[0x7A + heartPieceCount] plus the heart total).
 *   - Row C: two skulltula counters - OOT's Gold Skulltula tokens and the current (or last
 *     visited) MM spider house - both using MM's own 24x24 skulltula icon and HUD digits.
 *
 * Bottom arm (0..320 x 168..240):
 *   - 11 x 2 grid of all 22 songs. Row 0 is the OOT lineage, row 1 the MM lineage, so a song
 *     MM inherited from OOT sits directly below its counterpart. A song lights when MM's
 *     quest bit is set, OR OOT's mirror bit is set, OR its derived rule holds (Goron's
 *     Lullaby intro; Inverted / Double Time from the Song of Time; Scarecrow's Song from
 *     scarecrowSpawnSongSet).
 *   - Button-sequence strip beneath the grid. 22 cells across 320 px leaves 28 px each,
 *     nowhere near enough for eight 16x16 glyphs per cell, so the sequence is shown once at
 *     full size for whichever song the cursor is on. Sequences come from MM's own
 *     gOcarinaSongButtons; the six OOT warp songs, which MM's ocarina code never learns,
 *     come from a local table.
 *   - The old bottom-right song preview window is gone - the 22 cells need the full width.
 *
 * Art comes from the SO2H menu skin (mm/assets/custom/textures/so2h_menu, sliced by
 * tools/so2h_slice_menu_sheet.py and shipped in 2ship.o2r), MM's own archives, and OOT's
 * merged assets via OotQuestArt_GetPath (mm/2s2h/OotItemIcons.h), which returns NULL on an
 * MM-only install so every OOT icon degrades to an empty recessed slot instead of crashing.
 * Nothing here is interactive, and song names are still not rendered.
 */

void So2h_QuestBar_Reset(void);

// Non-zero while the cursor lives in one of the two arms rather than on a page.
s32 So2h_QuestBar_IsCursorInBar(PauseContext* pauseCtx);

// Runs before the per-page cursor dispatch. Returns non-zero when it has consumed the
// frame's input, in which case the caller must skip both the per-page cursor update and
// KaleidoScope_HandlePageToggles.
s32 So2h_QuestBar_UpdateCursor(PlayState* play);

// Draws the whole L bar. Must be called with the full-screen viewport/scissor available,
// i.e. after the pause window passes are done.
void So2h_QuestBar_Draw(PlayState* play);

#endif // SO2H_QUEST_BAR_H
