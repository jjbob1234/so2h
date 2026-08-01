#include "so2h_pause_window.h"
#include "global.h"

// Raw linear progress, 0.0f .. 1.0f.
static f32 sSo2hWindowT = 0.0f;
// Smoothstep of sSo2hWindowT, what everything actually reads.
static f32 sSo2hWindowEased = 0.0f;

// ~12 frames to open or close.
#define SO2H_WINDOW_STEP (1.0f / 12.0f)

void So2h_PauseWindow_Reset(void) {
    sSo2hWindowT = 0.0f;
    sSo2hWindowEased = 0.0f;
}

void So2h_PauseWindow_Update(s16 pauseState, s32 isOwlWarp) {
    f32 target = 0.0f;

    // The window is a pause-menu-only affordance. It is not shown for:
    //  - the Owl Warp screen, which still uses the old 4-face cube geometry and its own
    //    full-screen map page (KaleidoScope_DrawOwlWarpMapPage, deliberately untouched),
    //  - the Game Over flow, which reuses the kaleido states but has no quest bar.
    if (!isOwlWarp && (pauseState >= PAUSE_STATE_OPENING_2) && (pauseState <= PAUSE_STATE_SAVEPROMPT)) {
        target = 1.0f;
    }

    if (sSo2hWindowT < target) {
        sSo2hWindowT += SO2H_WINDOW_STEP;
        if (sSo2hWindowT > target) {
            sSo2hWindowT = target;
        }
    } else if (sSo2hWindowT > target) {
        sSo2hWindowT -= SO2H_WINDOW_STEP;
        if (sSo2hWindowT < target) {
            sSo2hWindowT = target;
        }
    }

    sSo2hWindowEased = sSo2hWindowT * sSo2hWindowT * (3.0f - (2.0f * sSo2hWindowT));
}

f32 So2h_PauseWindow_GetFactor(void) {
    return sSo2hWindowEased;
}

s32 So2h_PauseWindow_IsActive(void) {
    return sSo2hWindowEased > 0.005f;
}

void So2h_PauseWindow_GetRect(s32* leftX, s32* topY, s32* rightX, s32* bottomY) {
    f32 t = sSo2hWindowEased;

    if (leftX != NULL) {
        *leftX = SO2H_WINDOW_LEFT_X;
    }
    if (topY != NULL) {
        *topY = SO2H_WINDOW_TOP_Y;
    }
    if (rightX != NULL) {
        *rightX = (s32)(SCREEN_WIDTH + ((SO2H_WINDOW_RIGHT_X - SCREEN_WIDTH) * t));
    }
    if (bottomY != NULL) {
        *bottomY = (s32)(SCREEN_HEIGHT + ((SO2H_WINDOW_BOTTOM_Y - SCREEN_HEIGHT) * t));
    }
}
