#ifndef SO2H_UI_H
#define SO2H_UI_H

#include "ultra64.h"

/*
 * SO2H [Menu] so2h_ui - the declarative menu API
 *
 * ---------------------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------------------
 * The first pass of the so2h pause redesign hand-built every screen. Adding one panel meant
 * editing six files, four of which were the engine rather than the content: a region enum, a
 * rect in *both* canon layout tables, a draw function plus a hand-placed call in a hardcoded
 * draw sequence, a per-region special case in the cursor code, and a texture define.
 *
 * so2h_ui replaces all of that with one static descriptor table. The contract this module is
 * held to:
 *
 *     Adding a new panel, a new grid of icons or a new stat readout is a DATA EDIT -
 *     one table row, and at most one draw callback - with zero changes to layout,
 *     clipping, navigation or asset code.
 *
 * ---------------------------------------------------------------------------------------
 * COORDINATE SPACE
 * ---------------------------------------------------------------------------------------
 * Solved rects are handed out in *screen space*: the widescreen-extended N64 rect space that
 * gDPFillWideRectangle / gSPWideTextureRectangle consume. x runs from
 * OTRGetRectDimensionFromLeftEdge(0) to OTRGetRectDimensionFromRightEdge(320) (roughly
 * -53..373 at 16:9), y always runs 0..240.
 *
 * Descriptors are authored in *design units* instead: 240 tall, and either 320 wide (the 4:3
 * canon) or 426 wide (the wide canon). The solver runs the whole tree twice, once per canon,
 * converts both to screen space, and only then lerps between them - so a morph never skews.
 * This is the same rule the old so2h_quest_layout.c followed, and the same morph constants,
 * lifted rather than reinvented.
 *
 * A node that wants different geometry per canon does NOT get authored twice. It gets one
 * row plus a sparse So2hUiVariant override for the axis that differs.
 *
 * ---------------------------------------------------------------------------------------
 * NO ALLOCATION
 * ---------------------------------------------------------------------------------------
 * This runs inside the kaleido overlay. Every array here is fixed size and statically
 * allocated. There is no malloc, no realloc and no ownership to reason about.
 */

#ifdef __cplusplus
extern "C" {
#endif

// `Gfx` itself arrives via ultra64.h -> PR/gbi.h, where it is an *anonymous* union. Forward
// declaring it here as `typedef union Gfx Gfx;` would introduce a differently-tagged type and
// break the build, so don't.
struct GraphicsContext;

// ---------------------------------------------------------------------------------------
// Limits. All static; overrunning any of them is a build-time assert, never a runtime fault.
// ---------------------------------------------------------------------------------------
#define SO2H_UI_MAX_NODES 192
#define SO2H_UI_MAX_PAGES 12
#define SO2H_UI_MAX_RUNS 24
#define SO2H_UI_MAX_SCROLL_ROWS 16
#define SO2H_UI_MAX_CLIP_DEPTH 4
#define SO2H_UI_MAX_LAYERS 8
#define SO2H_UI_MAX_VARIANTS 64

#define SO2H_UI_INVALID 0xFFFF

// Morph behaviour, carried over unchanged from so2h_quest_layout.h so the redesign keeps the
// feel it already has.
#define SO2H_UI_MORPH_FRAMES 14
#define SO2H_UI_OVERSHOOT 0.06f
#define SO2H_UI_ASPECT_WIDE 1.55f
#define SO2H_UI_ASPECT_43 1.45f

// Design-space widths of the two canon layouts. Height is always 240.
#define SO2H_UI_DESIGN_H 240.0f
#define SO2H_UI_DESIGN_W_43 320.0f
#define SO2H_UI_DESIGN_W_WIDE 426.0f

typedef u16 So2hUiId;

typedef struct So2hUiRect {
    f32 x0;
    f32 y0;
    f32 x1;
    f32 y1;
} So2hUiRect;

typedef enum So2hUiCanon { SO2H_UI_CANON_43, SO2H_UI_CANON_WIDE, SO2H_UI_CANON_MAX } So2hUiCanon;

// ---------------------------------------------------------------------------------------
// Node kinds
//
// The kind decides what a node *is*. How it places its children is a separate axis
// (So2hUiLayoutMode), so a PAGE can flow its children as a grid, a run or a scroll row
// without needing a kind of its own for each combination.
// ---------------------------------------------------------------------------------------
typedef enum So2hUiKind {
    SO2H_UI_ROOT,       // the screen; exactly one, id 0
    SO2H_UI_PANEL,      // a framed container
    SO2H_UI_GROUP,      // an invisible container, geometry only
    SO2H_UI_PAGEGROUP,  // owns activePage + an optional tab strip
    SO2H_UI_PAGE,       // a page inside a PAGEGROUP; solved only when active
    SO2H_UI_CELL,       // a focusable leaf
    SO2H_UI_DECOR,      // drawn, never focusable, never counted by nav
    SO2H_UI_CUSTOM,     // geometry from the API, contents entirely from the draw callback
    SO2H_UI_KIND_MAX
} So2hUiKind;

typedef enum So2hUiState {
    SO2H_UI_ENABLED,  // solved, drawn, focusable
    SO2H_UI_DISABLED, // solved, drawn in the grey skin, NOT focusable
    SO2H_UI_HIDDEN    // not solved, not drawn, not focusable, occupies no slot
} So2hUiState;

// ---------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------
typedef enum So2hUiLayoutMode {
    SO2H_UI_LAYOUT_FREE,       // children place themselves from their own constraints
    SO2H_UI_LAYOUT_GRID,       // cols x rows, uniform cells, gap between
    SO2H_UI_LAYOUT_RUN,        // integer tiles at native pitch; shortens by sliding tiles under
    SO2H_UI_LAYOUT_SCROLL_ROW, // fixed pitch, pans horizontally past a clip window
    SO2H_UI_LAYOUT_SCROLL_COL, // the same, vertically - the equipment column strip
    SO2H_UI_LAYOUT_STACK_H,    // children laid end to end horizontally
    SO2H_UI_LAYOUT_STACK_V     // ... vertically
} So2hUiLayoutMode;

/*
 * Per-axis constraint. Two floats and a mode fully describe every placement the old dual
 * canon tables expressed, and several they could not.
 *
 *   START   : pos = a,                       size = b
 *   END     : size = b,                      pos  = parentSize - a - b
 *   STRETCH : pos = a,                       size = parentSize - a - b
 *   CENTER  : size = b,                      pos  = (parentSize - b) * 0.5 + a
 *   FILL    : pos = 0,                       size = parentSize
 *
 * `a` and `b` are design units relative to the parent's solved size.
 */
typedef enum So2hUiAxisMode {
    SO2H_UI_AXIS_START,
    SO2H_UI_AXIS_END,
    SO2H_UI_AXIS_STRETCH,
    SO2H_UI_AXIS_CENTER,
    SO2H_UI_AXIS_FILL
} So2hUiAxisMode;

typedef struct So2hUiAxis {
    u8 mode; // So2hUiAxisMode
    f32 a;
    f32 b;
} So2hUiAxis;

// Sparse per-canon override. Only the axes that actually differ between 4:3 and wide need a
// row here; everything else is authored once.
typedef struct So2hUiVariant {
    So2hUiId node;
    u8 canon; // So2hUiCanon
    u8 hasX;
    u8 hasY;
    u8 state; // So2hUiState override, or 0xFF for "no override"
    So2hUiAxis x;
    So2hUiAxis y;
} So2hUiVariant;

// ---------------------------------------------------------------------------------------
// Clipping / layering / spill
//
// Regions in the mock deliberately overlap and sit off-grid, so containment is opt-in rather
// than assumed. A node either clips its children, is clipped by its parent, or explicitly
// spills past it.
// ---------------------------------------------------------------------------------------
typedef enum So2hUiClip {
    SO2H_UI_CLIP_INHERIT, // clipped by whatever the parent's scissor already is
    SO2H_UI_CLIP_SELF,    // pushes its own rect as the scissor for its subtree
    SO2H_UI_CLIP_SPILL    // ignores the inherited scissor; draws past its parent on purpose
} So2hUiClip;

// ---------------------------------------------------------------------------------------
// Presentation. Every field here is optional and every one of them is a data edit.
// ---------------------------------------------------------------------------------------
typedef enum So2hUiReveal {
    SO2H_UI_REVEAL_NONE,
    SO2H_UI_REVEAL_FADE,
    SO2H_UI_REVEAL_SLIDE,
    SO2H_UI_REVEAL_UNROLL // the RUN solver played forwards: the panel builds out of its tiles
} So2hUiReveal;

typedef struct So2hUiStyle {
    u16 sheet;  // So2hUiSheetId, or SO2H_UI_INVALID
    u16 slice;  // named slice within that sheet, or SO2H_UI_INVALID
    u8 drawMode; // So2hUiDrawMode
    u8 reveal;   // So2hUiReveal
    u8 r, g, b;  // tint; 255,255,255 for untinted
    u8 alpha;
    u8 layer;    // 0..SO2H_UI_MAX_LAYERS-1; higher draws later
} So2hUiStyle;

typedef enum So2hUiDrawMode {
    SO2H_UI_DRAW_NONE,
    SO2H_UI_DRAW_STRETCH,   // one slice scaled to the rect
    SO2H_UI_DRAW_NATIVE,    // one slice at native size, centred
    SO2H_UI_DRAW_NINESLICE, // corners fixed, edges stretched, middle stretched
    SO2H_UI_DRAW_TILE,      // slice repeated at native pitch
    SO2H_UI_DRAW_RUN        // grows by repeating the sheet's declared runCols / runRows
} So2hUiDrawMode;

// Draw callback. Receives the node's own solved rect and must not compute geometry from
// anything else. Returns the advanced Gfx pointer.
typedef Gfx* (*So2hUiDrawFn)(Gfx* gfx, So2hUiId node, const So2hUiRect* rect, void* user);

// Per-cell state provider for grids and scroll rows, so a mostly-disabled grid costs one
// table row and a predicate rather than N rows.
typedef u8 (*So2hUiCellStateFn)(So2hUiId node, s16 index);

// ---------------------------------------------------------------------------------------
// The descriptor. One row per node, static const, authored parents-first.
// ---------------------------------------------------------------------------------------
typedef struct So2hUiDesc {
    So2hUiId id;
    So2hUiId parent;
    u8 kind;   // So2hUiKind
    u8 state;  // So2hUiState
    u8 layout; // So2hUiLayoutMode, for this node's CHILDREN
    u8 clip;   // So2hUiClip

    So2hUiAxis x;
    So2hUiAxis y;

    // GRID / RUN / SCROLL_ROW parameters. Unused fields stay zero.
    s16 cols;
    s16 rows;
    f32 gap;       // design units between cells
    f32 pitch;     // SCROLL_ROW / RUN: native cell pitch in design units
    s16 count;     // SCROLL_ROW: total cells, including disabled ones
    u8 growEnd;    // RUN collapse end / SCROLL_ROW append end: 0 = start, 1 = end, 2 = screen
    u8 pad_;

    So2hUiStyle style;

    So2hUiDrawFn draw;
    So2hUiCellStateFn cellState;

    // Optional explicit navigation. SO2H_UI_INVALID means "solve it geometrically".
    So2hUiId navUp;
    So2hUiId navDown;
    So2hUiId navLeft;
    So2hUiId navRight;

    const char* debugName;
} So2hUiDesc;

// ---------------------------------------------------------------------------------------
// Solved node. Runtime mirror of the descriptor; index-parallel to it.
// ---------------------------------------------------------------------------------------
typedef struct So2hUiNode {
    So2hUiRect rect;                     // screen space, morphed
    So2hUiRect canonRect[SO2H_UI_CANON_MAX]; // screen space, per canon, pre-morph
    So2hUiRect clipRect;                 // effective scissor for this node's subtree
    f32 alpha;                           // resolved 0..1, including reveal
    f32 revealT;                         // 0..1
    u8 state;                            // resolved So2hUiState (page visibility folded in)
    u8 visible;
    u8 focusable;
    u8 pad_;
} So2hUiNode;

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

/**
 * Registers the descriptor table. Call once per pause-menu open, before any update or draw.
 * `count` must be <= SO2H_UI_MAX_NODES. The table must be authored parents-first; this is
 * checked and a violation is fatal in a debug build and ignored (node skipped) otherwise.
 */
void So2h_Ui_Init(const So2hUiDesc* table, s32 count, const So2hUiVariant* variants, s32 variantCount);

/** Clears all animation state: morph, runs, scroll offsets, reveals, focus. */
void So2h_Ui_Reset(void);

/** Advances one frame and re-solves the whole tree. Call once, before any read. */
void So2h_Ui_Update(void);

/** Walks the tree in layer order and emits every visible node. */
Gfx* So2h_Ui_Draw(Gfx* gfx);

/**
 * Arms the display-list budget guard with the tail of the arena the caller is writing into
 * (typically play->state.gfxCtx->overlay.d). Every emitter refuses to write once fewer than
 * SO2H_UI_GFX_RESERVE entries remain, so a frame degrades instead of running off the end of
 * overlayBuffer into the neighbouring pool buffers - which does not fault, it just makes
 * Fast3D execute garbage and kills the process with no log line.
 *
 * Passing NULL disables the guard, which is only correct for the host-side simulator.
 */
void So2h_Ui_SetGfxBudget(Gfx* end);

// ---------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------
const So2hUiNode* So2h_Ui_GetNode(So2hUiId id);
const So2hUiRect* So2h_Ui_GetRect(So2hUiId id);

/** Cell rect inside a GRID / SCROLL_ROW parent. Out-of-range indices give an empty rect. */
void So2h_Ui_GetCellRect(So2hUiId parent, s16 index, So2hUiRect* out);

/** One sheet tile in screen units. */
f32 So2h_Ui_TilePx(void);

/** 0 = fully 4:3, 1 = fully wide. */
f32 So2h_Ui_MorphT(void);

// ---------------------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------------------
void So2h_Ui_SetActivePage(So2hUiId pageGroup, So2hUiId page);
So2hUiId So2h_Ui_GetActivePage(So2hUiId pageGroup);

/** Runtime state override, so save-file conditions can grey out or hide content. */
void So2h_Ui_SetState(So2hUiId id, So2hUiState state);

/** Debug: reveal every HIDDEN page without a code change (so2h_show_hidden_pages). */
void So2h_Ui_SetShowHiddenPages(s32 show);
s32 So2h_Ui_GetShowHiddenPages(void);

// ---------------------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------------------
void So2h_Ui_ScrollTo(So2hUiId row, s16 index);
s16 So2h_Ui_GetScrollIndex(So2hUiId row);

// ---------------------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------------------
typedef enum So2hUiDir { SO2H_UI_UP, SO2H_UI_DOWN, SO2H_UI_LEFT, SO2H_UI_RIGHT } So2hUiDir;

void So2h_Ui_SetFocus(So2hUiId id);
So2hUiId So2h_Ui_GetFocus(void);

/**
 * Cell index within the focused node. A GRID / SCROLL_ROW / RUN container is one focus target
 * with an index rather than N focus targets, so this is how a caller learns *which* item the
 * cursor is on. Always 0 for a plain CELL.
 */
s16 So2h_Ui_GetFocusIndex(void);

/**
 * Moves focus one step. Returns non-zero if the move was consumed. A zero return means the
 * cursor walked off the edge of the tree and the caller should hand back to vanilla - this is
 * the boundary contract So2h_QuestBar_UpdateCursor already has with z_kaleido_scope_NES.c.
 */
s32 So2h_Ui_Navigate(So2hUiDir dir);

/** Non-zero if the screen-space point is inside any focusable node. */
s32 So2h_Ui_HitTest(f32 x, f32 y, So2hUiId* outId);

// ---------------------------------------------------------------------------------------
// Sheets
//
// A sheet is a texture plus a grid. Slices are addressed by index into that grid, so a new
// icon is a number in a table and never a new texture define, a new draw function or a new
// asset-header edit.
//
// Registration data is generated from mm/assets/custom/textures/so2h_menu/sheets.json by
// tools/so2h_gen_sheets.py. The manifest is checked in and hand-editable; the grid is NEVER
// inferred from pixels, and the generator fails the build if a cell declared as repeating is
// not byte-identical to its run partners.
// ---------------------------------------------------------------------------------------
#define SO2H_UI_MAX_SHEETS 64

typedef struct So2hUiSheetDef {
    u16 id;
    const void* texture; // TexturePtr; RGBA32
    s16 width;
    s16 height;
    s16 unit;    // 32 or 64
    s16 cols;
    s16 rows;
    u8 fmt;      // So2hUiTexFmt
    u8 runColLo; // inclusive column range that repeats when a RUN panel grows
    u8 runColHi;
    u8 runRowLo;
    u8 runRowHi;
    u8 pad_;
    const char* debugName;
} So2hUiSheetDef;

typedef enum So2hUiTexFmt { SO2H_UI_FMT_RGBA32, SO2H_UI_FMT_IA8 } So2hUiTexFmt;

/** Registers the generated sheet table. Safe to call more than once; later calls replace. */
void So2h_Ui_RegisterSheets(const So2hUiSheetDef* sheets, s32 count);

/** Slice index from a grid position, for hand-written call sites. */
u16 So2h_Ui_Slice(u16 sheet, s16 col, s16 row);

/**
 * Draws one slice of one sheet into one rect. This is the single emitter the tree walk itself
 * uses, exposed so a So2hUiDrawFn callback draws through exactly the same path as the chrome
 * around it - there is no second, privileged way to put art on screen.
 *
 * `clip` may be NULL for "the whole screen"; normally a callback passes the clipRect of the
 * node it was called for. `mode` is a So2hUiDrawMode.
 */
Gfx* So2h_UiDraw_Slice(Gfx* gfx, u16 sheet, u16 slice, const So2hUiRect* rect, const So2hUiRect* clip, u8 mode, u8 r,
                       u8 g, u8 b, u8 a);

/**
 * Draws one slice of one sheet into one rect. This is the single emitter the tree walk itself
 * uses, exposed so a So2hUiDrawFn callback draws through exactly the same path as the chrome
 * around it - there is no second, privileged way to put art on screen.
 *
 * `clip` may be NULL for "the whole screen"; normally a callback passes the clipRect of the
 * node it was called for. `mode` is a So2hUiDrawMode.
 */
Gfx* So2h_UiDraw_Slice(Gfx* gfx, u16 sheet, u16 slice, const So2hUiRect* rect, const So2hUiRect* clip, u8 mode, u8 r,
                       u8 g, u8 b, u8 a);

// ---------------------------------------------------------------------------------------
// Shared easing, exposed so content callbacks animate on the same curve as the chrome.
// ---------------------------------------------------------------------------------------
f32 So2h_Ui_SmoothStep(f32 t);

#ifdef __cplusplus
}
#endif

#endif // SO2H_UI_H
