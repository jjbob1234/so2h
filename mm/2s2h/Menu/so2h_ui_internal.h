#ifndef SO2H_UI_INTERNAL_H
#define SO2H_UI_INTERNAL_H

#include "so2h_ui.h"

/*
 * SO2H [Menu] so2h_ui internals.
 *
 * Shared between so2h_ui_node.c / _layout.c / _draw.c / _nav.c / _sheet.c only. Content
 * authors include so2h_ui.h and never this file.
 */

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------
// Per-run state (SO2H_UI_LAYOUT_RUN). One entry per RUN container, keyed by node id.
// ---------------------------------------------------------------------------------------
typedef struct So2hUiRunState {
    So2hUiId node;
    s16 count;      // live tile count this frame
    s16 targetCount;
    f32 gap[16];    // current overlap of tile k under tile k+1, in screen units
    f32 fade;       // fade of the tile currently being dropped
    u8 inUse;
    u8 seeded;
} So2hUiRunState;

// ---------------------------------------------------------------------------------------
// Per-scroll-row state (SO2H_UI_LAYOUT_SCROLL_ROW). Independent per row, which is the whole
// point: boots, tunics and swords each keep their own offset and switching rows resets none.
// ---------------------------------------------------------------------------------------
typedef struct So2hUiScrollState {
    So2hUiId node;
    f32 offset;   // current, in cells
    f32 target;   // desired, in cells
    s16 index;    // focused cell index within the row
    u8 inUse;
    u8 seeded;
} So2hUiScrollState;

typedef struct So2hUiPageState {
    So2hUiId group;
    So2hUiId active;
    So2hUiId cursor; // remembered focus within that page
    u8 inUse;
    u8 pad_;
} So2hUiPageState;

// ---------------------------------------------------------------------------------------
// The one global. Static storage, no allocation.
// ---------------------------------------------------------------------------------------
typedef struct So2hUiCtx {
    const So2hUiDesc* desc;
    s32 descCount;
    const So2hUiVariant* variants;
    s32 variantCount;

    So2hUiNode node[SO2H_UI_MAX_NODES];

    So2hUiRunState run[SO2H_UI_MAX_RUNS];
    So2hUiScrollState scroll[SO2H_UI_MAX_SCROLL_ROWS];
    So2hUiPageState page[SO2H_UI_MAX_PAGES];

    u8 stateOverride[SO2H_UI_MAX_NODES]; // So2hUiState, or 0xFF for "use the descriptor"

    f32 screenX0;
    f32 screenX1;
    f32 screenW;
    f32 aspect;
    f32 tile;
    f32 morphT;
    s16 activeCanon;
    s16 morphFrames;

    So2hUiId focus;
    s32 showHiddenPages;
    s32 initialised;
} So2hUiCtx;

So2hUiCtx* So2h_UiCtx(void);

// node.c
s32 So2h_UiNode_IsValid(So2hUiId id);
const So2hUiDesc* So2h_UiNode_Desc(So2hUiId id);
So2hUiRunState* So2h_UiNode_Run(So2hUiId id);
So2hUiScrollState* So2h_UiNode_Scroll(So2hUiId id);
So2hUiPageState* So2h_UiNode_Page(So2hUiId id);
u8 So2h_UiNode_ResolvedState(So2hUiId id);

// layout.c
void So2h_UiLayout_Update(void);

// draw.c
Gfx* So2h_UiDraw_Tree(Gfx* gfx);

// nav.c
void So2h_UiNav_Reset(void);
void So2h_UiNav_Validate(void);

// sheet.c
const So2hUiSheetDef* So2h_UiSheet_Def(u16 sheet);
s32 So2h_UiSheet_SliceRect(u16 sheet, u16 slice, s16* sx, s16* sy, s16* sw, s16* sh);
s32 So2h_UiSheet_SliceRectF(u16 sheet, u16 slice, f32* sx, f32* sy, f32* sw, f32* sh);
s32 So2h_UiSheet_Dims(u16 sheet, s16* w, s16* h);
const void* So2h_UiSheet_Texture(u16 sheet);

#ifdef __cplusplus
}
#endif

#endif // SO2H_UI_INTERNAL_H
