#ifndef SO2H_PAUSE_WINDOW_H
#define SO2H_PAUSE_WINDOW_H

#include "ultra64.h"

/*
 * SO2H [Menu] Pause window
 *
 * The vanilla kaleidoscope draws its rotating page prism, its info panel and its cursor
 * across the whole screen, on top of a full-screen blit of the frozen pause background
 * framebuffer.
 *
 * The so2h redesign shrinks that entire scene into an animated window anchored to the
 * top-left of the screen, freeing the right column and the bottom row of the screen for
 * the backwards-L quest/song bar (see so2h_quest_bar.h).
 *
 * This module owns nothing but the animation factor and the resulting rectangle. It lives
 * in mm/2s2h rather than inside the kaleido overlay because the pause background blit that
 * has to track the window is done in z_play.c, outside the overlay.
 *
 * All coordinates here are N64 320x240 screen space. Widescreen fan-out is handled by the
 * consumers (View_SetViewport / FB_DrawFromFramebufferRect), never by hardcoding pixels.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Settled window rect (factor == 1.0f), N64 320x240 screen coordinates.
#define SO2H_WINDOW_LEFT_X 0
#define SO2H_WINDOW_TOP_Y 0
#define SO2H_WINDOW_RIGHT_X 192
#define SO2H_WINDOW_BOTTOM_Y 168

// Reset the animation. Called when the pause menu is torn down.
void So2h_PauseWindow_Reset(void);

// Advance the animation one frame. `pauseState` is a PauseState value; `isOwlWarp` must be
// non-zero for the IS_PAUSE_STATE_OWL_WARP states so the Owl Warp screen is never shrunk.
void So2h_PauseWindow_Update(s16 pauseState, s32 isOwlWarp);

// Eased 0.0f (full screen) .. 1.0f (settled window) factor.
f32 So2h_PauseWindow_GetFactor(void);

// Non-zero once the window has visibly started shrinking.
s32 So2h_PauseWindow_IsActive(void);

// Current animated window rect in N64 320x240 screen coordinates.
void So2h_PauseWindow_GetRect(s32* leftX, s32* topY, s32* rightX, s32* bottomY);

#ifdef __cplusplus
}
#endif

#endif // SO2H_PAUSE_WINDOW_H
