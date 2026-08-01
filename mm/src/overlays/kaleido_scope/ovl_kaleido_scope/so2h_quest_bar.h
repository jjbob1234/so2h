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

// Placeholder art regions. Everything the bar draws is a flat colour quad or an existing
// icon texture; no new assets go through the OTR pipeline in this pass. The three regions
// below are the seams where real border art will drop in later:
//   A = bottom arm border, B = right arm border, C = bottom-right corner.

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
