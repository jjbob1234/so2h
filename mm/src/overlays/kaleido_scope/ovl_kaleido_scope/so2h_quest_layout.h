#ifndef SO2H_QUEST_LAYOUT_H
#define SO2H_QUEST_LAYOUT_H

#include "global.h"

/*
 * File: so2h_quest_layout.h
 * Overlay: ovl_kaleido_scope
 * Description: SO2H [Menu] resolution-aware layout engine for the merged OOT + MM quest bar.
 *
 * ---------------------------------------------------------------------------------------
 * COORDINATE SPACE
 * ---------------------------------------------------------------------------------------
 * Everything this file hands out is in *screen space*, which is the widescreen-extended N64
 * rect space that gDPFillWideRectangle / gSPWideTextureRectangle actually consume:
 *
 *     x in [ OTRGetRectDimensionFromLeftEdge(0), OTRGetRectDimensionFromRightEdge(320) ]
 *     y in [ 0, SCREEN_HEIGHT ]                                        (always 0..240)
 *
 * It is NOT framebuffer pixels. Fast3D scales the 320x240 rect space onto the framebuffer
 * itself; the only thing widescreen changes is that x runs past both edges (roughly -53..373
 * at 16:9, i.e. a 426-wide span). See mm/2s2h/framebuffer_effects.c:140, which pairs
 * OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) << 2 with SCREEN_HEIGHT << 2 for the rect
 * corners while passing OTRGetGameRenderWidth()/Height() only as the *texture* dimensions.
 *
 * Because the extended span at 16:9 is ~426 wide against a fixed 240 tall, the wide design
 * space below (426 x 240) is 1:1 with screen space at 16:9, and the 4:3 design space
 * (320 x 240) is 1:1 with screen space at 4:3. Any other aspect just rescales x.
 *
 * ---------------------------------------------------------------------------------------
 * TWO CANON LAYOUTS + MORPH
 * ---------------------------------------------------------------------------------------
 * Each region is authored twice, once per canon layout, in that layout's own design pixels.
 * Both tables are converted into screen space every frame and *then* lerped, so the morph
 * happens in the space things are drawn in and never skews mid-transition.
 *
 *   aspect >= 1.55  -> LAYOUT_WIDE  (426 x 240 design space)
 *   aspect <= 1.45  -> LAYOUT_43    (320 x 240 design space)
 *   1.45 .. 1.55    -> hysteresis band; whichever layout is already active stays active
 *
 * The morph eases in/out over SO2H_LAYOUT_MORPH_FRAMES frames and carries a small
 * squash/stretch overshoot about each region's own centre so the change reads as motion
 * rather than a jump cut.
 */

// ---------------------------------------------------------------------------------------
// Regions
// ---------------------------------------------------------------------------------------
typedef enum So2hLayoutRegion {
    /*  0 */ SO2H_REGION_RIGHT_ARM_PANEL,  // background panel, right arm of the L
    /*  1 */ SO2H_REGION_BOTTOM_ARM_PANEL, // background panel, bottom arm of the L
    /*  2 */ SO2H_REGION_HEART_WINDOW,     // heart containers + magic
    /*  3 */ SO2H_REGION_QUEST_GRID,       // 3x2 quest-item grid (5 live cells + 1 dead)
    /*  4 */ SO2H_REGION_NOTEBOOK,         // derived: overhangs the grid's dead cell
    /*  5 */ SO2H_REGION_REMAINS_WINDOW,   // radial medallions + boss remains
    /*  6 */ SO2H_REGION_DATA_SCREEN,      // save info / stat readout
    /*  7 */ SO2H_REGION_SONG_GRID,        // song selection strip
    /*  8 */ SO2H_REGION_SONG_PREVIEW,     // song preview: name, staff, button sequence
    /*  9 */ SO2H_REGION_CONTENT_AREA,     // placeholder content panel
    /* 10 */ SO2H_REGION_MENUBAR,          // placeholder toolbar / menubar
    /* 11 */ SO2H_REGION_MAX
} So2hLayoutRegion;

// ---------------------------------------------------------------------------------------
// A screen-space rect. Kept in f32 because the design->screen conversion and the morph both
// need sub-unit precision; callers round only at the point they emit a rect command.
// ---------------------------------------------------------------------------------------
typedef struct So2hRect {
    f32 x0;
    f32 y0;
    f32 x1;
    f32 y1;
} So2hRect;

typedef enum So2hLayoutId { SO2H_LAYOUT_43, SO2H_LAYOUT_WIDE, SO2H_LAYOUT_MAX } So2hLayoutId;

typedef struct So2hLayout {
    So2hRect region[SO2H_REGION_MAX];
    f32 regionAlpha[SO2H_REGION_MAX]; // 0..1, for regions that only exist in one layout
    f32 screenX0;                     // extended left edge, screen space
    f32 screenX1;                     // extended right edge, screen space
    f32 screenW;                      // screenX1 - screenX0
    f32 aspect;                       // screenW / SCREEN_HEIGHT
    f32 tile;                         // one GUI-sheet tile, screen units
    f32 morphT;                       // 0 = fully 4:3, 1 = fully wide
    s16 activeLayout;                 // So2hLayoutId currently being eased toward
    s16 songCols;
    s16 songRows;
    s16 dataCols;
} So2hLayout;

// Aspect thresholds and the dead band between them.
#define SO2H_LAYOUT_ASPECT_WIDE 1.55f
#define SO2H_LAYOUT_ASPECT_43 1.45f
#define SO2H_LAYOUT_MORPH_FRAMES 14
// Peak squash/stretch during a morph, as a fraction of the region's own size.
#define SO2H_LAYOUT_OVERSHOOT 0.06f

/**
 * Recomputes the whole layout for this frame. Call exactly once, at the top of
 * So2h_QuestBar_Draw, before anything reads a region rect.
 */
void So2h_Layout_Update(void);

/**
 * The layout computed by the last So2h_Layout_Update. Never NULL - the table is static and
 * is seeded with the 4:3 canon on first use, so an early read is stale rather than invalid.
 */
const So2hLayout* So2h_Layout_Get(void);

/**
 * One GUI-sheet tile in screen units, rounded and clamped. The sheet is authored on a 35 px
 * grid against a 988 px tall mock, so a tile is 240 * 35 / 988 ~= 8.5 screen units.
 */
s16 So2h_Layout_TilePx(void);

/**
 * Convenience accessors. Both clamp the region index.
 */
const So2hRect* So2h_Layout_Region(So2hLayoutRegion region);
f32 So2h_Layout_RegionAlpha(So2hLayoutRegion region);

/**
 * Splits `rect` into a cols x rows grid and writes cell (col, row) into `out`, inset by
 * `gap` screen units on every side. Out-of-range cells produce an empty rect.
 */
void So2h_Layout_GridCell(const So2hRect* rect, s16 cols, s16 rows, s16 col, s16 row, f32 gap, So2hRect* out);

/**
 * Shrinks a rect by `inset` on all four sides, clamping to a non-inverted result.
 */
void So2h_Layout_Inset(const So2hRect* rect, f32 inset, So2hRect* out);

#endif // SO2H_QUEST_LAYOUT_H
