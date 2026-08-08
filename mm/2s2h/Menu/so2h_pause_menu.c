#include "so2h_pause_menu.h"
#include "so2h_ui.h"
#include "so2h_ui_scene_pause.h"
#include "so2h_ui_sheets.h"
#include "so2h_pause_window.h"

#include "global.h"

#include <libultraship/bridge/consolevariablebridge.h>

/*
 * SO2H [Menu] Pause menu controller. See so2h_pause_menu.h for why this seam exists.
 *
 * The whole file is a state machine with two bits: "has the scene been registered" and "was
 * the menu on last frame". Everything else the menu does - stagger, travel, clatter, exit
 * under gravity - is the engine's, driven by nothing but So2h_Ui_Open / _Close / _Update.
 */

// The scene table is static const, so registration is a one-time pointer hand-off rather
// than a copy. Re-registering every open would also reset focus, which we do not want.
static s32 sSo2hMenuRegistered = 0;
// Whether the menu was up last frame. The edges of this are the only two events in the file.
static s32 sSo2hMenuWasOn = 0;
// The overlay's content binder. See So2h_PauseMenu_SetBindHook in the header for why the
// hand-off is a runtime pointer and not a `draw` column in the generated table.
static So2hPauseMenuBindFn sSo2hBindHook = NULL;

// ---------------------------------------------------------------------------------------
// Dev hooks
//
// Off by default and read through CVars, so a Release build with the dev menu compiled out
// still gets the defaults and no branch anywhere has to be #ifdef'd at the call site.
// ---------------------------------------------------------------------------------------
static s32 So2h_PauseMenu_DevForceOpen(void) {
    return CVarGetInteger("gSo2h.Ui.ForceOpen", 0);
}

static void So2h_PauseMenu_ApplyDevState(void) {
    static s32 sLastReplay = 0;
    s32 replay;

    So2h_Ui_SetShowHiddenPages(CVarGetInteger("gSo2h.Ui.ShowHiddenPages", 0));
    So2h_Ui_SetTimeScale(CVarGetFloat("gSo2h.Ui.TimeScale", 1.0f));
    So2h_Ui_SetOutlines(CVarGetInteger("gSo2h.Ui.Outlines", 0));

    // Replay is a counter, not a flag: the dev menu bumps it and the entrance re-runs with a
    // fresh seed. A flag would need the UI to clear it again and would misfire on the frame
    // the window loses focus.
    replay = CVarGetInteger("gSo2h.Ui.Replay", 0);
    if (replay != sLastReplay) {
        sLastReplay = replay;
        So2h_Ui_Open();
    }
}

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------
void So2h_PauseMenu_Reset(void) {
    if (sSo2hMenuWasOn) {
        So2h_Ui_Reset();
    }
    sSo2hMenuWasOn = 0;
}

/**
 * Is the new menu wanted this frame?
 *
 * Same window as So2h_PauseWindow_Update deliberately: the collage is anchored to the
 * shrunken pause window, so if the window is not engaged the menu has nothing to sit
 * beside. Owl Warp and the Game Over flow keep the vanilla full-screen scene.
 */
static s32 So2h_PauseMenu_Wanted(s16 pauseState, s32 isOwlWarp) {
    if (So2h_PauseMenu_DevForceOpen()) {
        return 1;
    }
    return !isOwlWarp && (pauseState >= PAUSE_STATE_OPENING_2) && (pauseState <= PAUSE_STATE_SAVEPROMPT);
}

void So2h_PauseMenu_Update(s16 pauseState, s32 isOwlWarp) {
    s32 on = So2h_PauseMenu_Wanted(pauseState, isOwlWarp);

    if (!sSo2hMenuRegistered) {
        s32 nodeCount = 0;
        s32 variantCount = 0;
        const So2hUiDesc* table = So2h_UiScene_Pause(&nodeCount);
        const So2hUiVariant* variants = So2h_UiScene_PauseVariants(&variantCount);

        // Sheets FIRST. The registry is what So2h_UiSheet_Def reads, and without it every
        // lookup returns NULL: So2h_UiDrawQuad declines on sheet->texture == NULL and
        // So2h_UiSheet_RingPx returns a zero ring, so the menu solves and navigates
        // perfectly and paints absolutely nothing. That is exactly the failure mode this
        // call prevents, so it stays in front of So2h_Ui_Init.
        So2h_Ui_RegisterSheets(gSo2hUiSheets, SO2H_SHEET_MAX);
        So2h_Ui_Init(table, nodeCount, variants, variantCount);
        sSo2hMenuRegistered = 1;

        // So2h_Ui_Init clears every binding, so content has to be re-attached here and
        // nowhere else. So2h_Ui_Reset deliberately does NOT clear them - it runs every frame
        // the menu is off and would unbind everything on the first unpause.
        if (sSo2hBindHook != NULL) {
            sSo2hBindHook();
        }
    }

    if (on && !sSo2hMenuWasOn) {
        So2h_Ui_Open();
    } else if (!on && sSo2hMenuWasOn) {
        So2h_Ui_Close();
    }
    sSo2hMenuWasOn = on;

    // Keep updating through the exit: So2h_Ui_Close only *starts* the fall, and the engine
    // needs frames to finish it. So2h_Ui_CloseDone is the engine saying it is safe to stop.
    if (on || !So2h_Ui_CloseDone()) {
        So2h_PauseMenu_ApplyDevState();
        So2h_Ui_Update();
    }
}

void So2h_PauseMenu_SetBindHook(So2hPauseMenuBindFn fn) {
    if (sSo2hBindHook == fn) {
        return;
    }
    sSo2hBindHook = fn;

    // Installed late: the scene is already registered, so bind now rather than wait for an
    // Init that will never come again.
    if (sSo2hMenuRegistered && (fn != NULL)) {
        fn();
    }
}

s32 So2h_PauseMenu_IsActive(void) {
    return sSo2hMenuRegistered && (sSo2hMenuWasOn || !So2h_Ui_CloseDone());
}

// ---------------------------------------------------------------------------------------
// Cursor highlight
//
// The vanilla KaleidoScope_DrawCursor lives in the pause 3D space and would be trapped
// inside the shrunken window, so while the cursor is in the menu the 3D one is suppressed
// (z_kaleido_scope_NES.c) and this 2D highlight is drawn instead.
//
// It lives here rather than in the content file because it is a property of the ENGINE's
// focus, not of any one cell's contents: it follows So2h_Ui_GetFocus and reads that node's
// solved rect, so a cell the scene moves is highlighted correctly with no code change.
// ---------------------------------------------------------------------------------------
#define SO2H_CURSOR_R 255
#define SO2H_CURSOR_G 255
#define SO2H_CURSOR_B 160
#define SO2H_CURSOR_STEP 0x400
#define SO2H_CURSOR_BASE 160.0f
#define SO2H_CURSOR_SWING 95.0f

static s16 sSo2hCursorPhase = 0;
// Set by the overlay each time it runs the cursor update; see the header.
static s32 sSo2hCursorInMenu = 0;

void So2h_PauseMenu_SetCursorInMenu(s32 inMenu) {
    sSo2hCursorInMenu = inMenu;
}

static Gfx* So2h_PauseMenu_Edge(Gfx* gfx, s16 x0, s16 y0, s16 x1, s16 y1, u8 alpha) {
    if ((x1 <= x0) || (y1 <= y0)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, SO2H_CURSOR_R, SO2H_CURSOR_G, SO2H_CURSOR_B, alpha);
    gDPFillWideRectangle(gfx++, x0, y0, x1, y1);

    return gfx;
}

static Gfx* So2h_PauseMenu_DrawCursor(Gfx* gfx, PauseContext* pauseCtx) {
    const So2hUiRect* rect;
    So2hUiId focus;
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;
    u8 pulse;

    if (pauseCtx->state != PAUSE_STATE_MAIN) {
        return gfx;
    }
    if (!sSo2hCursorInMenu) {
        return gfx;
    }

    focus = So2h_Ui_GetFocus();
    if (focus == SO2H_UI_INVALID) {
        return gfx;
    }
    rect = So2h_Ui_GetRect(focus);
    if (rect == NULL) {
        return gfx;
    }

    sSo2hCursorPhase += SO2H_CURSOR_STEP;
    pulse = (u8)(SO2H_CURSOR_BASE + (SO2H_CURSOR_SWING * Math_SinS(sSo2hCursorPhase)));

    x0 = (s16)((rect->x0 < 0.0f) ? (rect->x0 - 0.5f) : (rect->x0 + 0.5f));
    y0 = (s16)((rect->y0 < 0.0f) ? (rect->y0 - 0.5f) : (rect->y0 + 0.5f));
    x1 = (s16)((rect->x1 < 0.0f) ? (rect->x1 - 0.5f) : (rect->x1 + 0.5f));
    y1 = (s16)((rect->y1 < 0.0f) ? (rect->y1 - 0.5f) : (rect->y1 + 0.5f));

    // Four one-pixel edges rather than a filled quad, so the cell contents stay readable
    // underneath the highlight.
    gfx = So2h_PauseMenu_Edge(gfx, x0, y0, x1, (s16)(y0 + 1), pulse);
    gfx = So2h_PauseMenu_Edge(gfx, x0, (s16)(y1 - 1), x1, y1, pulse);
    gfx = So2h_PauseMenu_Edge(gfx, x0, y0, (s16)(x0 + 1), y1, pulse);
    gfx = So2h_PauseMenu_Edge(gfx, (s16)(x1 - 1), y0, x1, y1, pulse);

    return gfx;
}

// ---------------------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------------------
void So2h_PauseMenu_Draw(struct PlayState* playArg) {
    PlayState* play = (PlayState*)playArg;
    Gfx* gfx;

    if (!So2h_PauseMenu_IsActive()) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL39_Overlay(play->state.gfxCtx);

    gfx = OVERLAY_DISP;

    // Arm the budget guard with the tail of the arena we are writing into. Every emitter in
    // the engine refuses to write once fewer than SO2H_UI_GFX_RESERVE entries remain, so a
    // heavy frame drops its tail instead of running off the end of overlayBuffer into the
    // neighbouring pool buffers - which does not fault, it just makes Fast3D execute garbage
    // and kills the process with no log line.
    So2h_Ui_SetGfxBudget((Gfx*)play->state.gfxCtx->overlay.d);

    gDPPipeSync(gfx++);
    // The pause pages render through a shrunken viewport, which leaves the scissor clipped to
    // the window rect (View_ApplyLetterbox scissors to view->viewport). Restore the full
    // screen before drawing anything outside the window.
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);

    gfx = So2h_Ui_Draw(gfx);
    gfx = So2h_PauseMenu_DrawCursor(gfx, &play->pauseCtx);

    gDPPipeSync(gfx++);
    So2h_Ui_SetGfxBudget(NULL);
    OVERLAY_DISP = gfx;

    CLOSE_DISPS(play->state.gfxCtx);
}
