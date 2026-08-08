#ifndef SO2H_PAUSE_MENU_H
#define SO2H_PAUSE_MENU_H

#include "ultra64.h"

/*
 * SO2H [Menu] Pause menu controller
 *
 * The seam between the kaleido overlay and so2h_ui. It exists so the overlay never has to
 * know how the menu engine is driven: kaleido calls four functions - reset, update, draw,
 * navigate - and everything about scene registration, the open/close animation and the
 * display-list budget lives here.
 *
 * WHY A SEPARATE FILE AND NOT A HANDFUL OF CALLS INSIDE z_kaleido_scope_NES.c
 * ---------------------------------------------------------------------------------------
 * z_kaleido_scope_NES.c is a 5000-line vanilla file that also has to keep working for Owl
 * Warp and Game Over. Every line added to it is a line that has to be re-reasoned about on
 * every future merge. Keeping the wiring here means the overlay's diff is four call sites
 * that read as English, and the ordering rules (update before anything reads a rect, draw
 * with the full-screen scissor restored, navigate before the per-page cursor) are stated
 * once, here, next to the code that depends on them.
 *
 * THE SCENE IS GENERATED
 * ---------------------------------------------------------------------------------------
 * The descriptor table this registers comes out of tools/so2h_gen_scene.py, from
 * tools/scenes/pause.py. Do not add nodes here. A new panel is a row in pause.py and a
 * regenerate; that is the whole point of the engine.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// Tear the menu back down to its pre-open state. Called from the same place
// So2h_QuestBar_Reset is, i.e. whenever the pause menu is off or still opening.
void So2h_PauseMenu_Reset(void);

// Advance one frame. Registers the scene on first use, starts the entrance when the pause
// menu turns on and plays the exit when it turns off. Must run before anything reads a
// solved rect this frame. `pauseState` is a PauseState; `isOwlWarp` non-zero for the Owl
// Warp states, which keep the vanilla full-screen scene and never get the new menu.
void So2h_PauseMenu_Update(s16 pauseState, s32 isOwlWarp);

// Emit the menu. Drawn last, on OVERLAY_DISP, with the full-screen scissor restored, so it
// is never clipped or scaled by the shrunken pause viewport.
void So2h_PauseMenu_Draw(struct PlayState* play);

// Non-zero if the menu is on screen in any form, including mid-entrance and mid-exit.
s32 So2h_PauseMenu_IsActive(void);

/*
 * CONTENT BINDING
 * ---------------------------------------------------------------------------------------
 * The generated descriptor table is static const and lives here in mm/2s2h/Menu, but the
 * code that paints quest icons lives in the kaleido overlay, whose symbols this translation
 * unit must not name. So content is attached at RUNTIME through So2h_Ui_BindDraw, and the
 * overlay hands us the one function that does the attaching.
 *
 * The hook is called immediately after So2h_Ui_Init, and immediately on registration if the
 * scene is already registered when it is set, so it cannot matter whether the overlay
 * installs it before or after the first update. Setting the same hook twice is a no-op.
 */
typedef void (*So2hPauseMenuBindFn)(void);
void So2h_PauseMenu_SetBindHook(So2hPauseMenuBindFn fn);

/*
 * Whether the pause cursor currently lives in the menu rather than on a vanilla page.
 *
 * Pushed in by the overlay instead of read out of PauseContext, because the two special
 * positions that mean "in the menu" (PAUSE_CURSOR_QUEST_BAR_RIGHT / _BOTTOM) are defined in
 * z_kaleido_scope.h, which is overlay-private. Copying their numeric values here would put a
 * second, silently divergent definition of the boundary contract in the tree.
 */
void So2h_PauseMenu_SetCursorInMenu(s32 inMenu);

/*
 * Navigation deliberately does NOT live here. Moving the cursor off the edge of the menu has
 * to call KaleidoScope_MoveCursorFromSpecialPos, which is internal to the kaleido overlay
 * and not linkable from mm/2s2h. So the boundary contract stays where it already is, in
 * So2h_QuestBar_UpdateCursor, which now delegates the in-bar move to So2h_Ui_Navigate.
 */

#ifdef __cplusplus
}
#endif

#endif // SO2H_PAUSE_MENU_H
