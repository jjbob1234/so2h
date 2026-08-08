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
    SO2H_UI_REVEAL_UNROLL, // the RUN solver played forwards: the panel builds out of its tiles
    // OFF-SCREEN SWAP. The node never changes its contents on screen. When a consumer calls
    // So2h_Ui_Swap() the node drives DOWN out of the bottom of the screen, and only once it
    // is fully off-screen does the new content - row count, size, scale, sheet, anything -
    // get applied; then it drives back up to its solved rect at the new size. The re-solve
    // therefore always happens where nobody can see it, so a window may change shape as
    // violently as it likes and the transition still reads as one deliberate move.
    //
    // This is what makes the context menu cheap to extend: a consumer that wants a
    // different menu writes its rows and calls So2h_Ui_Swap. It does not fade, it does not
    // cross-dissolve, and there is no per-state animation code anywhere.
    SO2H_UI_REVEAL_SWAP,
    // GROW. The node starts as a short letterbox pinned at its OWN TOP EDGE and opens
    // downward to its solved height, scaling its whole subtree with it. Nothing inside it
    // declares anything: a window animates its contents by existing.
    SO2H_UI_REVEAL_GROW,
    // FALL. Same displacement as SLIDE, always from above, on a gravity curve instead of an
    // ease: it accelerates the whole way down, lands, and bounces once. Authored per cell,
    // so an icon grid drops in as individual objects rather than as one sliding sheet.
    SO2H_UI_REVEAL_FALL,
    // POP. Travel finishes EARLY, and the frames left over are spent PAST the resting place:
    // the node sails a fixed few units beyond its rect along the axis it entered on, then
    // snaps back onto it. The overshoot is an absolute distance, not a share of the travel,
    // so a node that comes in from off-screen still only pops "a little".
    SO2H_UI_REVEAL_POP
} So2hUiReveal;

// Which screen edge a SLIDE / SWAP node travels through. AUTO is DERIVED at solve time from
// the node's own rect: it uses the screen edge its centre is already nearest to, and the
// travel distance is measured off that rect, so a node is exactly fully hidden at t=0 at
// every aspect and no window carries a hand-authored travel distance.
typedef enum So2hUiRevealFrom {
    SO2H_UI_FROM_AUTO,
    SO2H_UI_FROM_LEFT,
    SO2H_UI_FROM_RIGHT,
    SO2H_UI_FROM_TOP,
    SO2H_UI_FROM_BOTTOM
} So2hUiRevealFrom;

typedef struct So2hUiStyle {
    u16 sheet;  // So2hUiSheetId, or SO2H_UI_INVALID
    u16 slice;  // named slice within that sheet, or SO2H_UI_INVALID
    // PER-NODE ART SCALE, 1.0 = the sheet drawn at the canon tile size. The collage puts the
    // same sheets on screen at very different sizes, so a window states how big ITS tile is
    // and the border art keeps its proportions instead of stretching. The rings scale with
    // it - solved against parentTile * parentScale - or the chrome and the content it is
    // supposed to contain drift apart as soon as a frame is drawn at anything but 1.0.
    // Derive it from the size you want (scale = wantedSize / (tiles * srcTexels)); never
    // pick a scale first and let the size fall out of it.
    f32 scale;
    u8 drawMode; // So2hUiDrawMode
    u8 reveal;   // So2hUiReveal
    u8 r, g, b;  // tint; 255,255,255 for untinted
    u8 alpha;
    u8 layer;    // 0..SO2H_UI_MAX_LAYERS-1; higher draws later
    // THE OPEN SEQUENCE IS TWO NUMBERS PER NODE AND NOTHING ELSE. Pausing plays the menu on:
    // each window enters through revealFrom, and revealDelay frames of stagger are spent
    // before it starts moving. There is no timeline, no keyframe list and no per-window
    // animation code anywhere in the engine - a new panel joins the open by naming a delay,
    // and a panel that names none is simply already there.
    u8 revealFrom;  // So2hUiRevealFrom
    u8 revealDelay; // frames of stagger before this node starts its reveal
    // ...plus a per-node slop of 0..revealJitter extra frames, drawn from this open's seed.
    // The table sets the CHOREOGRAPHY; the jitter makes sure no two opens are bar-for-bar
    // identical. Defaults to SO2H_UI_REVEAL_JITTER; set 0 on a node that must be exact.
    u8 revealJitter;
    // PROGRAMMATIC DROP SHADOW. Default is ON for every node: the table says nothing and the
    // node casts a shadow, because "everything in a stacked collage casts" is the correct
    // default and opting out is the exception. Set noShadow on the few nodes that must not
    // (a hole, a tint-only overlay, a cell that lives inside its own parent's recess).
    // See SO2H_UI_SHADOW_* below for the one place the look is tuned.
    u8 noShadow;
} So2hUiStyle;

// ---------------------------------------------------------------------------------------
// Drop shadow (one implementation, one place to tune, no per-feature code)
// ---------------------------------------------------------------------------------------
// The shadow is not art and it is not baked into any sheet. Each casting node's own
// silhouette - the alpha of exactly the quads it is about to emit - is rendered to an
// offscreen target, offset, blurred, and composited under the node in COLOUR BURN, so the
// shadow darkens the chrome beneath it instead of greying it out. Because it is derived
// from the node's real alpha it is automatically correct for nine-slices, holes (RING),
// runs and rotated art, and it costs nothing to author.
//
// The blur is stated in PIXELS AT A 1440-TALL FRAMEBUFFER and scaled by the real
// framebuffer height, so the shadow is the same physical softness at 720p and at 4K
// instead of being a different look per resolution.
#define SO2H_UI_SHADOW_BLUR_PX 32.0f  // gaussian radius @ 1440p
#define SO2H_UI_SHADOW_REF_H 1440.0f
// The offset is stated as a MULTIPLE OF THE BLUR REACH, not as a fixed distance: an offset
// smaller than the blur simply hides the shadow underneath its own caster, so the two
// numbers have to move together.
#define SO2H_UI_SHADOW_OFS_X 0.75f    // x blur reach -> ~4.0 design units
#define SO2H_UI_SHADOW_OFS_Y 1.05f    // x blur reach -> ~5.6 design units
#define SO2H_UI_SHADOW_STRENGTH 0.7f  // how hard the burn bites, 0..1

// ---------------------------------------------------------------------------------------
// THE OPEN AND CLOSE PERFORMANCE IS THIS BLOCK OF NUMBERS. There is no timeline object, no
// keyframe list and no per-window animation code: a window joins the performance by naming
// a reveal and a delay, and the feel of the whole menu is tuned here, once.
// ---------------------------------------------------------------------------------------
#define SO2H_UI_REVEAL_FRAMES 22 // how long one window's entrance takes
#define SO2H_UI_GROW_FROM 0.10f  // a GROW window starts at this fraction of its solved height
#define SO2H_UI_REVEAL_JITTER 6  // default max extra frames of per-node desync, per open
// POP: arrive early, sail a little past the resting place, then snap back onto it.
#define SO2H_UI_POP_SETTLE 0.65f // fraction of the entrance spent travelling; rest is the pop
#define SO2H_UI_POP_OVER 6.0f    // design units of overshoot - absolute, not a share of travel
// The bulge is So2h_UiPulse(u*u), not a sine: squaring u leans the peak late (u = 0.707), so
// the node drifts out and snaps back fast - and the overlay never pulls in sinf.
// The close is NOT the open played backwards: everything gathers upward for a few frames of
// held breath, then the floor drops out and each node falls under gravity, stretching
// vertically (and pinching horizontally) as it picks up speed.
#define SO2H_UI_CLOSE_RISE_F 6      // frames of build-up
#define SO2H_UI_CLOSE_RISE_U 5.0f   // design units it rises during the build-up
#define SO2H_UI_CLOSE_GRAVITY 2.6f  // design units per frame^2 once it lets go
#define SO2H_UI_CLOSE_JITTER 4      // max frames of per-node desync on the way out
#define SO2H_UI_CLOSE_SQUASH 0.55f  // vertical stretch ceiling at terminal speed
// Only a fraction of that stretch is paid back as horizontal squeeze - a strict 1/sy
// conserves area but turns a wide window into a noodle, which reads as a bug, not as speed.
#define SO2H_UI_CLOSE_PINCH 0.30f

// ---------------------------------------------------------------------------------------
// THE SOUND OF THE PERFORMANCE. One voice per animated node, deliberately overloaded: every
// window that travels gets its own slide voice and its own landing thud, at its own pitch,
// so a busy page arrives as a clatter instead of a single click.
//
// HYBRID ROUTE. Two mechanisms live side by side, for two different jobs:
//
//   1. THE POOL (used by the animation). A menu-owned pool of SO2H_UI_SFX_VOICES voices that
//      mixes its own s16 stereo output into the SAME buffer the game's audio thread has just
//      filled, immediately before it is handed to AudioPlayer_Play. It owns nothing else - no
//      sequence, no channel, no sfx table entry - and it scales itself by the game's
//      MasterVolume * SoundEffectsVolume CVars every block, so the sliders and mute still
//      apply exactly as they do to game audio.
//
//      Why not the sfx bank: gChannelsPerBank[layout][BANK_SYSTEM] is 2 in ALL FOUR channel
//      layouts, so the bank can hold at most two simultaneous menu voices and evicts the rest
//      by priority. The intended clatter collapses into a two-voice stutter. The bank -> channel
//      partition is baked into the compiled NA_BGM_GENERAL_SFX sequence that ships in the ROM,
//      so it cannot be widened from this repo. Hence a pool.
//
//   2. THE BANK SLOTS (kept, but idle during the animation). The same two custom samples are
//      still bound over two UNUSED system-bank dummy sfx slots, so future one-off UI sounds
//      (cursor move, confirm) can be fired the ordinary AudioSfx_PlaySfx way where a single
//      voice is all that is wanted. Gated behind SO2H_UI_SFX_BANK_ENABLED and reachable only
//      through So2h_UiSfx_PlayBank() - it must NOT fire during reveal/close, or every sound
//      would be triggered twice.
//
// Threading: the triggers run on the GAME thread, the mix runs on the AUDIO thread. They meet
// over a single-producer/single-consumer command ring; voices are only ever allocated and
// freed by the audio thread while draining it.
// ---------------------------------------------------------------------------------------
#define SO2H_UI_SFX_ENABLED 1      // compile the whole layer out with 0
#define SO2H_UI_SFX_BANK_ENABLED 1 // also bind the two sfx-bank slots for future one-offs
// NA_SE_SY_DUMMY_13 / NA_SE_SY_DUMMY_14, spelled out so this header does not have to pull in
// sfx.h. so2h_ui_sfx.c static-asserts them against the real defines.
#define SO2H_UI_SFX_SLIDE_ID 0x480D  // travel voice, started when a node begins moving
#define SO2H_UI_SFX_PLACED_ID 0x480E // landing voice, fired the frame it settles
#define SO2H_UI_SFX_SLIDE_IDX 0x0D   // soundEffects[] index the slide sample is bound to
#define SO2H_UI_SFX_PLACED_IDX 0x0E  // soundEffects[] index the placed sample is bound to
// 44100 Hz source against the 32000 Hz mixer. Both oggs are mono, so channels does not enter.
#define SO2H_UI_SFX_TUNING 1.378125f
#define SO2H_UI_SFX_SLIDE_PITCH 0.10f  // +/- pitch spread on the travel voice, per node
#define SO2H_UI_SFX_PLACED_PITCH 0.16f // +/- pitch spread on the landing voice, per node
#define SO2H_UI_SFX_SLIDE_VOL 0.45f    // travel is the bed, landing is the accent
#define SO2H_UI_SFX_PLACED_VOL 0.90f

// Pool geometry. 64 voices is far more than the scene can ask for (107 nodes, but never all
// travelling at once) - overprovisioned on purpose so the clatter is never thinned by eviction.
#define SO2H_UI_SFX_VOICES 64
#define SO2H_UI_SFX_CMDS 128 // command ring slots; power of two, index masked
// Output side: 32000 Hz stereo s16, up to SAMPLES_HIGH(560) * 3 frames per audio block.
#define SO2H_UI_SFX_OUT_RATE 32000
#define SO2H_UI_SFX_MAX_FRAMES 2048 // accumulator size; blocks are clamped to this
#define SO2H_UI_SFX_BUS 0.85f       // one knob for "the menu is too loud" without touching mixes
// Envelope, in output frames at 32 kHz. Short enough to be inaudible, long enough to kill the
// click a hard start or a hard cut makes.
#define SO2H_UI_SFX_ATTACK_F 64  // ~2 ms
#define SO2H_UI_SFX_RELEASE_F 128 // ~4 ms

/**
 * Bind the two custom samples. Safe to call every frame - it does nothing once bound, and
 * quietly gives up (leaving the menu silent) if the resources have not finished decoding yet.
 */
void So2h_UiSfx_Bind(void);

/** Start / stop one node's travel voice, and fire its landing voice. nodeId picks the pitch. */
void So2h_UiSfx_Slide(s32 nodeId, u32 seed);
void So2h_UiSfx_SlideStop(s32 nodeId);
void So2h_UiSfx_Placed(s32 nodeId, u32 seed);

/**
 * Mix the menu's own voices into an already-filled interleaved stereo s16 block.
 * AUDIO THREAD ONLY - call it from OTRAudio_Thread immediately before AudioPlayer_Play.
 */
void So2h_UiSfx_MixInto(s16* out, s32 frames);

/**
 * Fire one of the two samples through the ordinary sfx bank (single voice, no stacking).
 * For future one-off UI sounds only; the reveal / close animation must not use this.
 */
void So2h_UiSfx_PlayBank(s32 placed);

typedef enum So2hUiDrawMode {
    SO2H_UI_DRAW_NONE,
    SO2H_UI_DRAW_STRETCH,   // one slice scaled to the rect
    SO2H_UI_DRAW_NATIVE,    // one slice at native size, centred
    SO2H_UI_DRAW_NINESLICE, // corners fixed, edges stretched, middle stretched
    SO2H_UI_DRAW_TILE,      // slice repeated at native pitch
    SO2H_UI_DRAW_RUN,       // grows by repeating the sheet's declared runCols / runRows
    // Same as NINESLICE / RUN, except every cell INSIDE the outer border ring is skipped -
    // the frame's own fill is never emitted, leaving a real hole. This is how a window gets
    // put AROUND live content (the gameplay image) using body art that is normally opaque:
    // the hole is exactly the rect an attach=BORDER child resolves to, so the child and the
    // hole cannot drift. It is a per-node draw mode and not a sheet property, because the
    // same sheet is still drawn filled elsewhere in the same tree.
    SO2H_UI_DRAW_RING
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

    // Which ring of the parent frame this node is solved against. See So2hUiSheetDef.
    //   SO2H_UI_ATTACH_CONTENT (0, default) - inside the inner border ring. Where content goes.
    //   SO2H_UI_ATTACH_BORDER              - inside the outer border ring only.
    //   SO2H_UI_ATTACH_EDGE                - the raw rect, both rings ignored.
    u8 attach;

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
    // REVEAL_SWAP state. swapT runs 1 -> 0 (driving off the bottom) and then 0 -> 1 (coming
    // back); swapOut is which half it is in. The node's y is displaced by
    // (1 - smoothstep(swapT)) * (screenBottom - rect.y0), so "off-screen" is derived from
    // the solved rect and stays correct when the rect changes size mid-swap.
    f32 swapT;
    // Frames this node has already spent WAITING on its own revealDelay + jitter. The
    // stagger is spent before the node starts moving, so every window shares one clock and
    // the whole opening sequence is a column of delay numbers in the descriptor table.
    s16 revealHold;
    // Exit state: frames since So2h_Ui_Close(), or -1 when this node is not closing. Each
    // node subtracts its own So2h_Ui_Close jitter from it, so the block comes apart slightly
    // out of sync instead of moving as one rigid sheet.
    s16 closeF;
    u8 swapOut;
    u8 swapPending;                      // a consumer asked for new content; apply at swapT 0
    u8 state;                            // resolved So2hUiState (page visibility folded in)
    u8 visible;
    u8 focusable;
    // Scratch flag for the subtree walkers: "this node is under the root currently being
    // moved". The descriptor table is authored parents-first, so marking a subtree is one
    // forward pass and moving a window with its contents needs no child lists or recursion.
    u8 inSubtree;
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

/*
 * Attach content to an already-registered node.
 *
 * The descriptor table is generated from tools/scenes/pause.py and is static const, and the
 * code that actually paints quest icons and song notes lives in the kaleido overlay, which
 * the generated file must not name. So content is bound at runtime instead of being written
 * into the row: the scene says WHERE a panel is, the binding says WHAT is drawn in it, and
 * neither one has to know about the other.
 *
 * Adding a panel therefore stays at one row in the scene plus one callback and its bind line
 * - no layout, clipping, navigation or asset code is touched.
 *
 * Bindings survive So2h_Ui_Reset and are cleared only by So2h_Ui_Init. Passing NULL unbinds.
 * `user` is handed back to the callback verbatim; the engine never dereferences it.
 */
void So2h_Ui_BindDraw(So2hUiId id, So2hUiDrawFn fn, void* user);

// Attach a per-cell enable predicate to a GRID / RUN / SCROLL_ROW, so navigation skips the
// cells content considers empty. Same lifetime rules as So2h_Ui_BindDraw. Overrides the
// descriptor's own cellState.
void So2h_Ui_BindCellState(So2hUiId id, So2hUiCellStateFn fn);

/**
 * Requests an off-screen content swap on a REVEAL_SWAP node (see So2hUiReveal).
 * The node drives off the bottom of the screen, `apply` is invoked once at the bottom of
 * the move with nothing visible, and the node drives back up at whatever size it now
 * solves to. Calling it again mid-move is safe: the newest request wins.
 * Nodes that are not REVEAL_SWAP apply immediately and return false.
 */
typedef void (*So2hUiSwapFn)(s32 nodeId, void* arg);

s32 So2h_Ui_Swap(s32 nodeId, So2hUiSwapFn apply, void* arg);

/**
 * Restarts the entrance with a fresh desync seed. Call when the pause menu opens.
 * Every node's jitter this open is So2h_UiHash(nodeId ^ seed), so the desync is unique per
 * open but stable within one open however many times the frame is re-solved.
 */
void So2h_Ui_Open(void);

/**
 * Plays the exit: a few frames of upward build-up, then everything falls out of frame under
 * gravity with slight per-node desync. Poll So2h_Ui_CloseDone() to know when the menu may
 * actually be torn down; until then keep calling So2h_Ui_Update / So2h_Ui_Draw.
 */
void So2h_Ui_Close(void);
s32 So2h_Ui_CloseDone(void);

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
/*
 * DEV HOOKS
 * ---------------------------------------------------------------------------------------
 * Six of them, all off/neutral by default and all driven from CVars by so2h_pause_menu.c, so
 * nothing here has to be #ifdef'd at a call site. Two live in the engine (below), one in the
 * SFX pool (gSo2h.Ui.SfxMute, read in so2h_ui_sfx.c) and three in the controller
 * (ForceOpen, ShowHiddenPages, Replay).
 */

// Animation speed multiplier. 1.0f is real time, 0.0f freezes the menu mid-entrance. Applied
// as whole animation steps in front of the advance block, so slowing the entrance down cannot
// move where anything lands.
void So2h_Ui_SetTimeScale(f32 scale);

// Draw a 1px box around every visible node's solved rect, over everything else.
void So2h_Ui_SetOutlines(s32 on);

void So2h_Ui_SetGfxBudget(Gfx* end);

// The tail currently armed, so a bound draw callback emitting raw gfx++ of its own can
// apply the same guard instead of writing past the arena the engine is respecting.
Gfx* So2h_Ui_GetGfxBudget(void);

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

typedef enum So2hUiGrow { SO2H_UI_GROW_RUN, SO2H_UI_GROW_STRETCH } So2hUiGrow;

typedef enum So2hUiAttach {
    SO2H_UI_ATTACH_CONTENT, // inside the inner border ring - the default, where content goes
    SO2H_UI_ATTACH_BORDER,  // inside the outer border ring, on top of the inner ring
    SO2H_UI_ATTACH_EDGE     // the raw rect, both rings ignored
} So2hUiAttach;

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

    // The rings, in SOURCE TEXELS from the sheet edge. These are TILE-ALIGNED, because that is
    // how the art is actually built: a 5x5 @64 frame is an outer border ring (row/col 0 and 4),
    // an inner border ring (row/col 1 and 3), and a single centre cell that tiles the fill.
    //
    //   0 .. border    the outer border ring - one whole tile. Carries the drop shadow/bevel.
    //   border..content the inner border ring - the second whole tile.
    //   content ..     the fill area, painted by repeating the centre cell. Children go here.
    //
    // So for a 5x5 frame these are border = 1 * unit and content = 2 * unit, exactly. They are
    // never scanned off pixels - sheets.json declares them in whole tiles and the generator
    // multiplies by unit. Scaled by tilePx/unit at solve time so a window and its own chrome
    // cannot drift apart when the tile size changes.
    //
    // Consequence: a frame with content = 2 tiles cannot be drawn narrower than 5 tiles on an
    // axis (2 fixed + 1 fill + 2 fixed) without crushing its own borders. The solver clamps up
    // to that minimum rather than letting the rings overlap.
    s16 borderL, borderT, borderR, borderB;
    s16 contentL, contentT, contentR, contentB;

    // How this frame fills space bigger than its art: SO2H_UI_GROW_RUN repeats the run band at
    // native size, SO2H_UI_GROW_STRETCH scales the middle cell. It lives on the SHEET and not
    // on the call site because it is a property of the art: these interiors are self-tiling, so
    // stretching them smears the texture. A descriptor row cannot get it wrong.
    u8 grow;

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
 * node it was called for. `mode` is a So2hUiDrawMode. `scale` is the caller's art scale -
 * pass the node's own style.scale, or 1.0f for canon size.
 */
Gfx* So2h_UiDraw_Slice(Gfx* gfx, u16 sheet, u16 slice, const So2hUiRect* rect, const So2hUiRect* clip, u8 mode, u8 r,
                       u8 g, u8 b, u8 a, f32 scale);

// ---------------------------------------------------------------------------------------
// Shared easing, exposed so content callbacks animate on the same curve as the chrome.
// ---------------------------------------------------------------------------------------
f32 So2h_Ui_SmoothStep(f32 t);

/** Gravity curve used by REVEAL_FALL: accelerate, land, one small bounce. */
f32 So2h_Ui_FallEase(f32 t);

/**
 * Deterministic integer scramble used for every per-node animation offset. Deliberately NOT
 * rand(): a frame may be solved any number of times, and the jitter a node was dealt this
 * open must not change underneath it.
 */
u32 So2h_UiHash(u32 x);

#ifdef __cplusplus
}
#endif

#endif // SO2H_UI_H
