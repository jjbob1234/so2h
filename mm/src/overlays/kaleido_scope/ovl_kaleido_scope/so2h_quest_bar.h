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
 *   |      pause window         | right  |  right arm  = quest progress
 *   |   (kaleido page prism,    |  arm   |               (MM + OOT collectibles)
 *   |    info panel, cursor)    |        |
 *   |                           |        |
 *   +---------------------------+--------+
 *   |        bottom arm         | corner |  bottom arm = songs (display only)
 *   +---------------------------+--------+
 *
 * The bar is drawn in 2D screen space on OVERLAY_DISP, NOT inside the pause perspective
 * view, for two reasons:
 *   - anything drawn in the pause 3D space would shrink along with the window,
 *   - the extents of the pause 3D space at z=-50 depend on the aspect ratio
 *     (halfH = 114 * tan(30deg) ~= 65.8, halfW = halfH * aspect) and the existing
 *     cursorX = 101.0f for PAUSE_CURSOR_PAGE_RIGHT already sits outside the 4:3 bounds,
 *     so that space is not a reliable place to lay out a screen-edge bar.
 *
 * Scaffolding pass: layout, navigation and real save data only. Nothing here is
 * interactive, and song names are not rendered yet.
 */

// Art. The three regions are now drawn as nine-slice panels built from the SO2H menu skin
// (mm/assets/custom/textures/so2h_menu, sliced by tools/so2h_slice_menu_sheet.py and shipped
// in 2ship.o2r), not flat colour quads:
//   A = bottom arm (song staves), B = right arm (quest slots), C = bottom-right corner,
//   which is the dedicated song preview window (clef + staff + the highlighted song's note).
// Remaining flat quads: the OoT medallion / spiritual stone swatches, which still have no
// icon art in icon_item_static, and the arm cursor outline.

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
