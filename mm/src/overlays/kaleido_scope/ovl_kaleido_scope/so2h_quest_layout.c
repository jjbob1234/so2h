/*
 * File: so2h_quest_layout.c
 * Overlay: ovl_kaleido_scope
 * Description: SO2H [Menu] resolution-aware layout engine for the merged quest bar.
 *              See so2h_quest_layout.h for the coordinate-space contract.
 */

#include "so2h_quest_layout.h"
#include "BenPort.h"

// ---------------------------------------------------------------------------------------
// Canon layouts
//
// Each table is authored in its own design space, measured off the hand-built mock
// (image_ibKrTo.png, 1917x988) and normalised: the wide table against a 426x240 space (the
// widescreen-extended N64 rect span at 16:9) and the 4:3 table against plain 320x240.
//
// Both are converted to screen space every frame before anything is blended, so a region's
// design x is always relative to *its own* layout width, never to 320.
// ---------------------------------------------------------------------------------------
typedef struct So2hLayoutDef {
    f32 designW;
    So2hRect region[SO2H_REGION_MAX];
    s16 songCols;
    s16 songRows;
    s16 dataCols;
} So2hLayoutDef;

static const So2hLayoutDef sLayoutDefs[SO2H_LAYOUT_MAX] = {
    // ----------------------------------------------------------------------- 4:3, 320x240
    {
        320.0f,
        {
            /* RIGHT_ARM_PANEL  */ { 192.0f, 0.0f, 320.0f, 168.0f },
            /* BOTTOM_ARM_PANEL */ { 0.0f, 168.0f, 320.0f, 240.0f },
            /* HEART_WINDOW     */ { 197.0f, 8.0f, 245.0f, 58.0f },
            /* QUEST_GRID       */ { 199.0f, 60.0f, 241.0f, 100.0f },
            /* NOTEBOOK         */ { 0.0f, 0.0f, 0.0f, 0.0f }, // derived from QUEST_GRID
            /* REMAINS_WINDOW   */ { 250.0f, 8.0f, 316.0f, 86.0f },
            /* DATA_SCREEN      */ { 194.0f, 102.0f, 318.0f, 164.0f },
            /* SONG_GRID        */ { 6.0f, 172.0f, 104.0f, 226.0f },
            /* SONG_PREVIEW     */ { 108.0f, 172.0f, 318.0f, 200.0f },
            /* CONTENT_AREA     */ { 108.0f, 202.0f, 318.0f, 226.0f },
            /* MENUBAR          */ { 4.0f, 228.0f, 316.0f, 238.0f },
        },
        6,
        4,
        2,
    },
    // ---------------------------------------------------------------------- wide, 426x240
    //
    // The mock puts the bottom arm's top edge at y~164, but So2h_PauseWindow shrinks the
    // pause viewport to SO2H_WINDOW_BOTTOM_Y (168) at every aspect, so an arm starting above
    // that would sit on top of live page content. The whole bottom band is compressed into
    // 168..240 instead; if the window split ever moves, these move with it.
    {
        426.0f,
        {
            /* RIGHT_ARM_PANEL  */ { 256.0f, 0.0f, 426.0f, 168.0f },
            /* BOTTOM_ARM_PANEL */ { 0.0f, 168.0f, 426.0f, 240.0f },
            /* HEART_WINDOW     */ { 271.0f, 13.0f, 337.0f, 66.0f },
            /* QUEST_GRID       */ { 273.0f, 68.0f, 326.0f, 106.0f },
            /* NOTEBOOK         */ { 0.0f, 0.0f, 0.0f, 0.0f }, // derived from QUEST_GRID
            /* REMAINS_WINDOW   */ { 353.0f, 14.0f, 423.0f, 93.0f },
            /* DATA_SCREEN      */ { 256.0f, 108.0f, 423.0f, 164.0f },
            /* SONG_GRID        */ { 12.0f, 172.0f, 99.0f, 189.0f },
            /* SONG_PREVIEW     */ { 7.0f, 190.0f, 113.0f, 238.0f },
            /* CONTENT_AREA     */ { 121.0f, 168.0f, 422.0f, 221.0f },
            /* MENUBAR          */ { 120.0f, 223.0f, 421.0f, 239.0f },
        },
        11,
        2,
        3,
    },
};

// The Bomber's Notebook is not authored as a rect: it is always 1.333 cells square, centred
// on the quest grid's dead cell (bottom-right of the 3x2), so it overhangs the grid frame by
// design and stays correct no matter how the grid rect moves.
#define SO2H_QUEST_GRID_COLS 3
#define SO2H_QUEST_GRID_ROWS 2
#define SO2H_NOTEBOOK_CELL_SCALE 1.333f

// The GUI sheet is authored on a 35 px grid against a 988 px tall mock.
#define SO2H_SHEET_TILE_SRC 35.0f
#define SO2H_SHEET_MOCK_H 988.0f
#define SO2H_TILE_MIN 4

static So2hLayout sLayout;
static s16 sActiveLayout = SO2H_LAYOUT_43;
static f32 sBlendRaw = 0.0f; // linear 0..1 progress toward sActiveLayout
static s32 sSeeded = false;

// ---------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------
static f32 So2h_Lerp(f32 a, f32 b, f32 t) {
    return a + ((b - a) * t);
}

/**
 * Hermite ease-in-out. Cheap, monotonic, zero derivative at both ends, and needs no libm.
 */
static f32 So2h_SmoothStep(f32 t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    return t * t * (3.0f - (2.0f * t));
}

/**
 * Parabolic pulse: 0 at both ends, 1 at the midpoint. Drives the squash/stretch overshoot
 * without dragging sinf into the overlay.
 */
static f32 So2h_Pulse(f32 t) {
    if ((t <= 0.0f) || (t >= 1.0f)) {
        return 0.0f;
    }
    return 4.0f * t * (1.0f - t);
}

static s32 So2h_RectHasArea(const So2hRect* rect) {
    return (rect->x1 > rect->x0) && (rect->y1 > rect->y0);
}

/**
 * design px -> screen space. y is 1:1 (both design spaces are 240 tall and so is screen
 * space); x is fanned out across the widescreen-extended span, which is the exact same
 * mapping FB_DrawFromFramebufferRect applies to the pause background, so the bar and the
 * shrunken background stay locked together at any aspect.
 */
static void So2h_DesignToScreen(const So2hRect* in, f32 designW, f32 x0, f32 span, So2hRect* out) {
    f32 sx = span / designW;

    out->x0 = x0 + (in->x0 * sx);
    out->x1 = x0 + (in->x1 * sx);
    out->y0 = in->y0;
    out->y1 = in->y1;
}

/**
 * Derives the notebook rect from an already-converted quest grid rect.
 */
static void So2h_DeriveNotebook(const So2hRect* grid, So2hRect* out) {
    f32 cellW;
    f32 cellH;
    f32 cx;
    f32 cy;
    f32 halfW;
    f32 halfH;

    if (!So2h_RectHasArea(grid)) {
        out->x0 = out->y0 = out->x1 = out->y1 = 0.0f;
        return;
    }

    cellW = (grid->x1 - grid->x0) / (f32)SO2H_QUEST_GRID_COLS;
    cellH = (grid->y1 - grid->y0) / (f32)SO2H_QUEST_GRID_ROWS;

    // Dead cell: last column, last row.
    cx = grid->x0 + (cellW * ((f32)SO2H_QUEST_GRID_COLS - 0.5f));
    cy = grid->y0 + (cellH * ((f32)SO2H_QUEST_GRID_ROWS - 0.5f));

    halfW = (cellW * SO2H_NOTEBOOK_CELL_SCALE) * 0.5f;
    halfH = (cellH * SO2H_NOTEBOOK_CELL_SCALE) * 0.5f;

    out->x0 = cx - halfW;
    out->x1 = cx + halfW;
    out->y0 = cy - halfH;
    out->y1 = cy + halfH;
}

/**
 * Scales a rect about its own centre. Used for the morph overshoot.
 */
static void So2h_ScaleAboutCentre(So2hRect* rect, f32 scale) {
    f32 cx = (rect->x0 + rect->x1) * 0.5f;
    f32 cy = (rect->y0 + rect->y1) * 0.5f;
    f32 halfW = ((rect->x1 - rect->x0) * 0.5f) * scale;
    f32 halfH = ((rect->y1 - rect->y0) * 0.5f) * scale;

    rect->x0 = cx - halfW;
    rect->x1 = cx + halfW;
    rect->y0 = cy - halfH;
    rect->y1 = cy + halfH;
}

/**
 * Builds one canon layout, fully converted to screen space, including the derived notebook.
 */
static void So2h_BuildCanon(s16 layoutId, f32 x0, f32 span, So2hRect* out) {
    const So2hLayoutDef* def = &sLayoutDefs[layoutId];
    s16 i;

    for (i = 0; i < SO2H_REGION_MAX; i++) {
        So2h_DesignToScreen(&def->region[i], def->designW, x0, span, &out[i]);
    }
    So2h_DeriveNotebook(&out[SO2H_REGION_QUEST_GRID], &out[SO2H_REGION_NOTEBOOK]);
}

// ---------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------
void So2h_Layout_Update(void) {
    So2hRect canon[SO2H_LAYOUT_MAX][SO2H_REGION_MAX];
    f32 screenX0 = (f32)OTRGetRectDimensionFromLeftEdge(0.0f);
    f32 screenX1 = (f32)OTRGetRectDimensionFromRightEdge((f32)SCREEN_WIDTH);
    f32 span = screenX1 - screenX0;
    f32 aspect;
    f32 target;
    f32 step = 1.0f / (f32)SO2H_LAYOUT_MORPH_FRAMES;
    f32 t;
    f32 overshoot;
    s16 i;

    if (span < 1.0f) {
        // Degenerate viewport (can happen for a frame while the window is being resized).
        span = (f32)SCREEN_WIDTH;
        screenX0 = 0.0f;
        screenX1 = span;
    }
    aspect = span / (f32)SCREEN_HEIGHT;

    // --- Active layout, with a dead band so an aspect sitting exactly on the boundary can
    //     never oscillate between the two canons frame to frame. ---
    if (aspect >= SO2H_LAYOUT_ASPECT_WIDE) {
        sActiveLayout = SO2H_LAYOUT_WIDE;
    } else if (aspect <= SO2H_LAYOUT_ASPECT_43) {
        sActiveLayout = SO2H_LAYOUT_43;
    }

    if (!sSeeded) {
        // First frame: snap, don't morph in from the wrong canon.
        sBlendRaw = (sActiveLayout == SO2H_LAYOUT_WIDE) ? 1.0f : 0.0f;
        sSeeded = true;
    }

    target = (sActiveLayout == SO2H_LAYOUT_WIDE) ? 1.0f : 0.0f;
    if (sBlendRaw < target) {
        sBlendRaw += step;
        if (sBlendRaw > target) {
            sBlendRaw = target;
        }
    } else if (sBlendRaw > target) {
        sBlendRaw -= step;
        if (sBlendRaw < target) {
            sBlendRaw = target;
        }
    }

    t = So2h_SmoothStep(sBlendRaw);
    overshoot = 1.0f + (SO2H_LAYOUT_OVERSHOOT * So2h_Pulse(sBlendRaw));

    So2h_BuildCanon(SO2H_LAYOUT_43, screenX0, span, canon[SO2H_LAYOUT_43]);
    So2h_BuildCanon(SO2H_LAYOUT_WIDE, screenX0, span, canon[SO2H_LAYOUT_WIDE]);

    for (i = 0; i < SO2H_REGION_MAX; i++) {
        const So2hRect* a = &canon[SO2H_LAYOUT_43][i];
        const So2hRect* b = &canon[SO2H_LAYOUT_WIDE][i];
        s32 hasA = So2h_RectHasArea(a);
        s32 hasB = So2h_RectHasArea(b);

        // Mask-under: a region that only exists in one canon keeps that canon's rect and
        // fades instead of collapsing to a degenerate rect and popping.
        if (hasA && !hasB) {
            sLayout.region[i] = *a;
            sLayout.regionAlpha[i] = 1.0f - t;
        } else if (!hasA && hasB) {
            sLayout.region[i] = *b;
            sLayout.regionAlpha[i] = t;
        } else if (!hasA && !hasB) {
            sLayout.region[i] = *a;
            sLayout.regionAlpha[i] = 0.0f;
        } else {
            sLayout.region[i].x0 = So2h_Lerp(a->x0, b->x0, t);
            sLayout.region[i].y0 = So2h_Lerp(a->y0, b->y0, t);
            sLayout.region[i].x1 = So2h_Lerp(a->x1, b->x1, t);
            sLayout.region[i].y1 = So2h_Lerp(a->y1, b->y1, t);
            sLayout.regionAlpha[i] = 1.0f;
        }

        if (overshoot != 1.0f) {
            So2h_ScaleAboutCentre(&sLayout.region[i], overshoot);
        }
    }

    // The two background panels must stay flush with the screen edges through the morph, so
    // they are re-pinned after the overshoot rather than being allowed to breathe.
    sLayout.region[SO2H_REGION_RIGHT_ARM_PANEL].x1 = screenX1;
    sLayout.region[SO2H_REGION_RIGHT_ARM_PANEL].y0 = 0.0f;
    sLayout.region[SO2H_REGION_BOTTOM_ARM_PANEL].x0 = screenX0;
    sLayout.region[SO2H_REGION_BOTTOM_ARM_PANEL].x1 = screenX1;
    sLayout.region[SO2H_REGION_BOTTOM_ARM_PANEL].y1 = (f32)SCREEN_HEIGHT;
    // The right arm's bottom edge and the bottom arm's top edge are the same line.
    sLayout.region[SO2H_REGION_RIGHT_ARM_PANEL].y1 = sLayout.region[SO2H_REGION_BOTTOM_ARM_PANEL].y0;

    sLayout.screenX0 = screenX0;
    sLayout.screenX1 = screenX1;
    sLayout.screenW = span;
    sLayout.aspect = aspect;
    sLayout.morphT = t;
    sLayout.activeLayout = sActiveLayout;
    sLayout.tile = ((f32)SCREEN_HEIGHT * SO2H_SHEET_TILE_SRC) / SO2H_SHEET_MOCK_H;

    // Grid shapes do not interpolate - a cell count has to be an integer - so they snap at
    // the halfway point of the morph, which is exactly when the overshoot peaks and the
    // change is least noticeable.
    {
        const So2hLayoutDef* def = &sLayoutDefs[(t >= 0.5f) ? SO2H_LAYOUT_WIDE : SO2H_LAYOUT_43];

        sLayout.songCols = def->songCols;
        sLayout.songRows = def->songRows;
        sLayout.dataCols = def->dataCols;
    }
}

const So2hLayout* So2h_Layout_Get(void) {
    if (!sSeeded) {
        So2h_Layout_Update();
    }
    return &sLayout;
}

s16 So2h_Layout_TilePx(void) {
    s16 tile = (s16)(So2h_Layout_Get()->tile + 0.5f);

    if (tile < SO2H_TILE_MIN) {
        tile = SO2H_TILE_MIN;
    }
    return tile;
}

const So2hRect* So2h_Layout_Region(So2hLayoutRegion region) {
    const So2hLayout* layout = So2h_Layout_Get();
    s32 index = (s32)region;

    if ((index < 0) || (index >= SO2H_REGION_MAX)) {
        index = SO2H_REGION_RIGHT_ARM_PANEL;
    }
    return &layout->region[index];
}

f32 So2h_Layout_RegionAlpha(So2hLayoutRegion region) {
    const So2hLayout* layout = So2h_Layout_Get();
    s32 index = (s32)region;

    if ((index < 0) || (index >= SO2H_REGION_MAX)) {
        return 0.0f;
    }
    return layout->regionAlpha[index];
}

void So2h_Layout_GridCell(const So2hRect* rect, s16 cols, s16 rows, s16 col, s16 row, f32 gap, So2hRect* out) {
    f32 cellW;
    f32 cellH;

    if ((cols <= 0) || (rows <= 0) || (col < 0) || (row < 0) || (col >= cols) || (row >= rows)) {
        out->x0 = out->y0 = out->x1 = out->y1 = 0.0f;
        return;
    }

    cellW = (rect->x1 - rect->x0) / (f32)cols;
    cellH = (rect->y1 - rect->y0) / (f32)rows;

    out->x0 = rect->x0 + (cellW * (f32)col) + gap;
    out->y0 = rect->y0 + (cellH * (f32)row) + gap;
    out->x1 = out->x0 + cellW - (gap * 2.0f);
    out->y1 = out->y0 + cellH - (gap * 2.0f);

    if (out->x1 < out->x0) {
        out->x1 = out->x0;
    }
    if (out->y1 < out->y0) {
        out->y1 = out->y0;
    }
}

void So2h_Layout_Inset(const So2hRect* rect, f32 inset, So2hRect* out) {
    out->x0 = rect->x0 + inset;
    out->y0 = rect->y0 + inset;
    out->x1 = rect->x1 - inset;
    out->y1 = rect->y1 - inset;

    if (out->x1 < out->x0) {
        out->x0 = out->x1 = (rect->x0 + rect->x1) * 0.5f;
    }
    if (out->y1 < out->y0) {
        out->y0 = out->y1 = (rect->y0 + rect->y1) * 0.5f;
    }
}
