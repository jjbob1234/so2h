/*
 * File: so2h_ui_draw.c
 * Description: SO2H [Menu] so2h_ui draw backend.
 *
 * ---------------------------------------------------------------------------------------
 * ONE PRIMITIVE
 * ---------------------------------------------------------------------------------------
 * Everything this module can put on screen goes through exactly one emitter,
 * So2h_UiDrawQuad(): a sub-rect of one sheet, drawn into one screen-space rect, clipped to
 * one clip rect, tinted by one colour and one alpha. STRETCH, NATIVE, NINESLICE, TILE and RUN
 * are five ways of deciding *which quads to ask for*; none of them is a second drawing path.
 *
 * That is the difference from the first pass. There, every piece of art had its own draw
 * function, its own texture define and its own hand-placed call, so 43 tiles produced 43
 * near-identical code paths and 26 of them were never reached. Here a new piece of art is a
 * row in sheets.json and a slice number, and it draws itself.
 *
 * ---------------------------------------------------------------------------------------
 * WHY CLIPPING IS DONE IN SOFTWARE
 * ---------------------------------------------------------------------------------------
 * The obvious answer is gDPSetScissor, and it does not work here. Its coordinate fields are
 * *unsigned* 12-bit 10.2 fixed point (mm/include/PR/gbi.h:3817) and LUS reads them back
 * unsigned too (libultraship/src/fast/interpreter.cpp:2416, GfxDpSetScissor takes uint32_t).
 * Our screen space is the widescreen-extended rect space, where x starts at
 * OTRGetRectDimensionFromLeftEdge(0) - about -53 at 16:9. A scissor at x = -53 does not clip
 * at -53, it wraps to +970 and the panel vanishes.
 *
 * So the clip is applied analytically instead: intersect the destination rect with the clip
 * rect, then shift the texture s/t start by the same fraction of the source rect. s/t are
 * S10.5, so the crop lands on 1/32 of a texel - fine enough that a scroll row pans smoothly
 * rather than stepping a texel at a time, which is exactly what cropping via the loaded tile
 * (uls/ult, integer texels) would have done.
 *
 * A hardware scissor IS still set around SO2H_UI_CUSTOM callbacks, because their output is
 * opaque to us. That one is clamped to >= 0 and is therefore only approximate on the
 * widescreen wings; content that needs an exact left-edge crop should be a CELL grid rather
 * than a CUSTOM callback.
 *
 * ---------------------------------------------------------------------------------------
 * NINESLICE vs RUN
 * ---------------------------------------------------------------------------------------
 * Both read the sheet's declared repeating range (runColLo..runColHi, runRowLo..runRowHi from
 * sheets.json) and both keep the cells outside that range at native size. They differ in one
 * line: NINESLICE *stretches* the middle cells to fill the slack, RUN *repeats* them at
 * native pitch and crops the last one.
 *
 * RUN is the primary panel drawer, not a special effect. Measuring the window sheets showed
 * every one of them has the top row A B B C D - the interior is a self-tiling cell and the
 * frames were authored to grow by repeating edge tiles, not by stretching them. Stretching
 * them is what makes a panel look smeared at 16:9.
 */

#include "so2h_ui_internal.h"
#include "global.h"

// ---------------------------------------------------------------------------------------
// Display-list budget guard.
//
// Carried over from so2h_quest_bar.c:423 unchanged in behaviour. This file writes straight
// into the overlay list with `gfx++`, which bypasses the THGA bounds, and overrunning does
// not fault - it walks into the neighbouring pool buffers that the master DL branches to, so
// Fast3D executes garbage and the process dies with no log line and no stack. Every emitter
// below checks first and simply declines to write, so a heavy frame loses its tail instead of
// killing the game.
// ---------------------------------------------------------------------------------------
#define SO2H_UI_GFX_RESERVE 64

// Words left untouched at the END of the overlay arena, on top of the per-emitter reserve.
//
// SO2H_UI_GFX_RESERVE only protects the emitter that is writing right now. It does nothing for
// the code that writes into the same arena AFTER the menu returns - vanilla kaleido, the HUD,
// the debug text - none of which bounds-check, because before this menu existed the overlay
// arena was never close to full. Arming the budget short of the real end gives them a floor,
// so a heavy page loses its own tail (visible, reportable) instead of pushing somebody else
// off the end of overlayBuffer into the neighbouring pool, which does not fault: Fast3D just
// executes garbage and the process wedges with no log line. See SO2H_FREEZE_ROOTCAUSE.md.
#define SO2H_UI_GFX_TAIL_RESERVE 2048

// Gfx words per emitted piece, used by the budget check. gDPLoadTextureTile expands to six
// commands and gSPWideTextureRectangle to three; the rest is headroom for the sync.
#define SO2H_UI_GFX_PER_QUAD 12
#define SO2H_UI_GFX_PER_MODE 3

// Cells below this are not worth a draw call and are usually a degenerate solve.
#define SO2H_UI_MIN_QUAD_PX 0.4f

// A RUN axis will not emit more repeats than this, however wide the panel gets. It is a
// runaway guard for a bad pitch, not a hardware limit - there are none here.
#define SO2H_UI_MAX_REPEATS 64

static Gfx* sGfxBudgetEnd = NULL;

// ---------------------------------------------------------------------------------------
// Freeze diagnostics
//
// The pause hang leaves no log line and no stack, so the engine has to narrate itself. Two
// hooks, both off unless a CVar turns them on:
//
//   sTraceFrames > 0   every node logs BEFORE it draws, flushed, so the last line in the log
//                      is the node that wedged - and whether it wedged in its own slice art
//                      or in a bound content callback.
//   sDrawMax >= 0      draw only the first N nodes of the walk. That turns "which node" into
//                      a live bisection the player can run from the console without a rebuild:
//                      0 freezes nothing, raise it until the hang comes back.
// ---------------------------------------------------------------------------------------
static s32 sTraceFrames = 0;
static s32 sDrawMax = -1;

void So2h_UiTrace(const char* tag, s32 a, s32 b);

void So2h_Ui_SetTraceFrames(s32 frames) {
    sTraceFrames = frames;
}

void So2h_Ui_SetDrawMax(s32 max) {
    sDrawMax = max;
}

void So2h_Ui_SetGfxBudget(Gfx* end) {
    sGfxBudgetEnd = end;
}

Gfx* So2h_Ui_GetGfxBudget(void) {
    return sGfxBudgetEnd;
}

s32 So2h_Ui_GfxTailReserve(void) {
    return SO2H_UI_GFX_TAIL_RESERVE;
}

static s32 So2h_UiGfxRoom(Gfx* gfx, s32 need) {
    return (sGfxBudgetEnd == NULL) || ((gfx + need + SO2H_UI_GFX_RESERVE) < sGfxBudgetEnd);
}

/**
 * Rounds a screen-space float to the integer the rect commands take. All layout maths stays
 * in f32 and collapses to integers here and nowhere else, so a container and the art inside
 * it can never disagree by a pixel because they rounded at different times.
 */
static s16 So2h_UiRnd(f32 v) {
    return (s16)((v < 0.0f) ? (v - 0.5f) : (v + 0.5f));
}

static f32 So2h_UiMaxF(f32 a, f32 b) {
    return (a > b) ? a : b;
}

static f32 So2h_UiMinF(f32 a, f32 b) {
    return (a < b) ? a : b;
}

static s32 So2h_UiRectEmpty(const So2hUiRect* r) {
    return ((r->x1 - r->x0) < SO2H_UI_MIN_QUAD_PX) || ((r->y1 - r->y0) < SO2H_UI_MIN_QUAD_PX);
}

// ---------------------------------------------------------------------------------------
// Combiner setup
//
// One mode per texture format. Tint and alpha both ride the prim colour, so a disabled cell,
// a fading panel and a coloured medallion are the same draw with different numbers - not
// three code paths.
// ---------------------------------------------------------------------------------------
static Gfx* So2h_UiSetupMode(Gfx* gfx, u8 fmt, u8 r, u8 g, u8 b, u8 a) {
    if (!So2h_UiGfxRoom(gfx, SO2H_UI_GFX_PER_MODE)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    if (fmt == SO2H_UI_FMT_IA8) {
        gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    } else {
        gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    }
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);

    return gfx;
}

/**
 * Restores the plain XLU 1-cycle state the menu draws in. The texture-rect helpers change the
 * cycle type and render mode, so anything that follows a quad needs this before it can
 * assume the default state again.
 */
static Gfx* So2h_UiRestoreBlend(Gfx* gfx) {
    if (!So2h_UiGfxRoom(gfx, SO2H_UI_GFX_PER_MODE)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    return gfx;
}

/**
 * Hardware scissor, for SO2H_UI_CUSTOM callbacks only. Clamped into the unsigned 12-bit 10.2
 * field the command actually has; see the header comment for why this is not the general
 * clipping mechanism.
 */
static Gfx* So2h_UiScissor(Gfx* gfx, const So2hUiRect* r) {
    s32 x0 = So2h_UiRnd(r->x0);
    s32 y0 = So2h_UiRnd(r->y0);
    s32 x1 = So2h_UiRnd(r->x1);
    s32 y1 = So2h_UiRnd(r->y1);

    if (!So2h_UiGfxRoom(gfx, 2)) {
        return gfx;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > 1023) {
        x1 = 1023;
    }
    if (y1 > 1023) {
        y1 = 1023;
    }
    if (x1 < x0) {
        x1 = x0;
    }
    if (y1 < y0) {
        y1 = y0;
    }
    gDPPipeSync(gfx++);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, x0, y0, x1, y1);

    return gfx;
}

static Gfx* So2h_UiScissorFull(Gfx* gfx) {
    if (!So2h_UiGfxRoom(gfx, 2)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    return gfx;
}

// ---------------------------------------------------------------------------------------
// The one emitter
// ---------------------------------------------------------------------------------------

/**
 * Draws source texels (sx, sy, sw, sh) of `sheet` into screen rect `dst`, cropped to `clip`.
 *
 * The crop is analytic: the destination is intersected first, then s/t are advanced by the
 * same fraction of the source rect that was cut off the destination. dsdx / dtdy are computed
 * from the *uncropped* destination width, which is what keeps the scale constant as a cell
 * slides out of its clip window - compute them from the cropped width and the art visibly
 * squashes as it leaves.
 *
 * Returns `gfx` untouched if the quad is empty, fully clipped, or would not fit in the
 * remaining display list.
 */
static Gfx* So2h_UiDrawQuad(Gfx* gfx, const So2hUiSheetDef* sheet, f32 sx, f32 sy, f32 sw, f32 sh,
                            const So2hUiRect* dst, const So2hUiRect* clip) {
    So2hUiRect vis;
    f32 dw = dst->x1 - dst->x0;
    f32 dh = dst->y1 - dst->y0;
    f32 texPerPxX;
    f32 texPerPxY;
    f32 s;
    f32 t;
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;
    u16 dsdx;
    u16 dtdy;
    s32 uls;
    s32 ult;
    s32 lrs;
    s32 lrt;

    if ((sheet == NULL) || (sheet->texture == NULL)) {
        return gfx;
    }
    // A source rect thinner than one texel is not just invisible, it is fatal. gDPLoadTextureTile
    // wants an INCLUSIVE lower-right texel, so a sub-texel width makes lrs < uls, and LUS computes
    // `uint32_t tile_width = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1` (libultraship
    // fast/interpreter.cpp GfxDpLoadTile) - that subtraction wraps to ~4 billion, size_bytes goes
    // with it, and ImportTextureRgba32's unbounded copy loop hangs the process. Never emit one.
    if ((dw < SO2H_UI_MIN_QUAD_PX) || (dh < SO2H_UI_MIN_QUAD_PX) || (sw < 1.0f) || (sh < 1.0f)) {
        return gfx;
    }

    vis.x0 = So2h_UiMaxF(dst->x0, clip->x0);
    vis.y0 = So2h_UiMaxF(dst->y0, clip->y0);
    vis.x1 = So2h_UiMinF(dst->x1, clip->x1);
    vis.y1 = So2h_UiMinF(dst->y1, clip->y1);
    if (So2h_UiRectEmpty(&vis)) {
        return gfx;
    }

    texPerPxX = sw / dw;
    texPerPxY = sh / dh;

    s = sx + ((vis.x0 - dst->x0) * texPerPxX);
    t = sy + ((vis.y0 - dst->y0) * texPerPxY);

    x0 = So2h_UiRnd(vis.x0);
    y0 = So2h_UiRnd(vis.y0);
    x1 = So2h_UiRnd(vis.x1);
    y1 = So2h_UiRnd(vis.y1);
    if ((x1 <= x0) || (y1 <= y0)) {
        return gfx;
    }

    if (!So2h_UiGfxRoom(gfx, SO2H_UI_GFX_PER_QUAD)) {
        return gfx;
    }

    // 5.10 fixed point, from the uncropped destination size.
    dsdx = (u16)(texPerPxX * 1024.0f);
    dtdy = (u16)(texPerPxY * 1024.0f);
    if (dsdx == 0) {
        dsdx = 1;
    }
    if (dtdy == 0) {
        dtdy = 1;
    }

    // Sub-rect sampling. This has been in the tree the whole time (mm/include/PR/gbi.h:3529,
    // used by PreRender.c:186 and z_fbdemo.c:86); the first pass never reached for it, which
    // is the entire reason every tile needed to be its own file.
    // Belt and braces on top of the sub-texel reject above: build the inclusive tile bounds
    // explicitly, clamp them into the texture, and keep lower-right >= upper-left so the
    // unsigned width/height LUS derives from them can never wrap.
    uls = (s32)sx;
    ult = (s32)sy;
    lrs = (s32)(sx + sw) - 1;
    lrt = (s32)(sy + sh) - 1;
    if (uls < 0) {
        uls = 0;
    }
    if (ult < 0) {
        ult = 0;
    }
    if (lrs > (sheet->width - 1)) {
        lrs = sheet->width - 1;
    }
    if (lrt > (sheet->height - 1)) {
        lrt = sheet->height - 1;
    }
    if ((lrs < uls) || (lrt < ult)) {
        return gfx;
    }

    if (sheet->fmt == SO2H_UI_FMT_IA8) {
        gDPLoadTextureTile(gfx++, sheet->texture, G_IM_FMT_IA, G_IM_SIZ_8b, sheet->width, sheet->height, uls, ult, lrs,
                           lrt, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                           G_TX_NOLOD, G_TX_NOLOD);
    } else {
        gDPLoadTextureTile(gfx++, sheet->texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, sheet->width, sheet->height, uls, ult,
                           lrs, lrt, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                           G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    }
    // s / t are S10.5 absolute texture-image coordinates, which is what gDPLoadTextureTile's
    // gDPSetTileSize leaves the tile expecting.
    gSPWideTextureRectangle(gfx++, x0 << 2, y0 << 2, x1 << 2, y1 << 2, G_TX_RENDERTILE, (s16)(s * 32.0f),
                            (s16)(t * 32.0f), dsdx, dtdy);

    return gfx;
}

// ---------------------------------------------------------------------------------------
// Cell geometry inside a sheet
// ---------------------------------------------------------------------------------------
static void So2h_UiCellSrc(const So2hUiSheetDef* sheet, s16 col, s16 row, f32* sx, f32* sy, f32* sw, f32* sh) {
    *sx = (f32)(col * sheet->unit);
    *sy = (f32)(row * sheet->unit);
    *sw = (f32)sheet->unit;
    *sh = (f32)sheet->unit;

    // Clamp to the texture. Square cells are the rule, but the 43 legacy so2h_menu pieces are
    // single-cell sheets of their own odd sizes (52x35, 35x52, 210x210 ...), so a cell there
    // is "the whole texture" and must not read past it.
    if ((*sx + *sw) > (f32)sheet->width) {
        *sw = (f32)sheet->width - *sx;
    }
    if ((*sy + *sh) > (f32)sheet->height) {
        *sh = (f32)sheet->height - *sy;
    }
    if (*sw < 0.0f) {
        *sw = 0.0f;
    }
    if (*sh < 0.0f) {
        *sh = 0.0f;
    }
}

/**
 * Screen size of one sheet cell at native scale. The sheets are authored on a 32 px grid
 * against a 988 px tall mock, and ctx->tile is what one of those 32 px units measures on
 * screen - so a 64 px cell is two of them. Nothing here reads a hardcoded pixel size.
 */
static f32 So2h_UiCellScreen(const So2hUiSheetDef* sheet, f32 scale) {
    So2hUiCtx* ctx = So2h_UiCtx();

    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    return ((f32)sheet->unit / 32.0f) * ctx->tile * scale;
}

/**
 * Screen size of one cell of `sheetId` at `scale`, for callers outside this file.
 *
 * A content callback that wants to sit art against a frame's border needs to know how thick
 * that border is, and the only honest answer is the cell size the frame itself was drawn at.
 * Exposing this is what keeps such a callback from hardcoding a unit count that silently
 * lies the moment the node's style.scale or the sheet's unit changes.
 *
 * Returns 0 for an unknown sheet.
 */
f32 So2h_Ui_CellSize(u16 sheetId, f32 scale) {
    const So2hUiSheetDef* sheet = So2h_UiSheet_Def(sheetId);

    if (sheet == NULL) {
        return 0.0f;
    }
    return So2h_UiCellScreen(sheet, scale);
}

// ---------------------------------------------------------------------------------------
// Frame drawers
//
// So2h_UiDrawFrame covers NINESLICE and RUN with one axis solver and a single `repeat` flag,
// because the two only disagree about what happens to the cells inside the declared
// repeating range.
// ---------------------------------------------------------------------------------------

/**
 * Splits one axis of a frame into per-cell screen extents.
 *
 * `edge` cells (outside the repeating range) keep native size. The repeating range absorbs
 * everything left over: stretched evenly when `repeat` is 0, and emitted as whole native
 * cells plus one cropped remainder when it is 1.
 *
 * When the frame is asked to be narrower than its own fixed edges, the edges shrink
 * proportionally rather than overlapping - a squeezed panel reads as small, an overlapping
 * one reads as broken.
 *
 * Writes `n` starts into `pos` and `n` sizes into `size`, and returns the number of cells
 * actually produced (which exceeds `n` when repeating).
 */
static s32 So2h_UiSolveFrameAxis(s32 n, s32 lo, s32 hi, f32 cell, f32 span, s32 repeat, f32* pos, f32* size,
                                 s16* cellIndex, s32 maxOut) {
    f32 fixed;
    f32 slack;
    f32 scale = 1.0f;
    f32 cursor = 0.0f;
    s32 fixedCount;
    s32 midCount;
    s32 out = 0;
    s32 i;

    if ((n <= 0) || (cell <= 0.0f) || (maxOut <= 0)) {
        return 0;
    }
    if ((lo < 0) || (hi < lo) || (hi >= n)) {
        // No declared repeating range: the whole axis is fixed cells, scaled to fit.
        lo = -1;
        hi = -2;
    }

    midCount = (hi >= lo) ? ((hi - lo) + 1) : 0;
    fixedCount = n - midCount;
    fixed = (f32)fixedCount * cell;

    if ((fixed > span) && (fixed > 0.0f)) {
        scale = span / fixed;
        fixed = span;
    }
    slack = span - fixed;
    if (slack < 0.0f) {
        slack = 0.0f;
    }

    for (i = 0; i < n; i++) {
        s32 isMid = (i >= lo) && (i <= hi);

        if (!isMid) {
            if (out >= maxOut) {
                break;
            }
            pos[out] = cursor;
            size[out] = cell * scale;
            cellIndex[out] = (s16)i;
            cursor += size[out];
            out++;
            continue;
        }

        // Only the first cell of the repeating range consumes the slack; the rest of the
        // range is what gets repeated across it. That is what makes A B B C D grow by adding
        // more B, which is how the sheets were drawn.
        if (i != lo) {
            continue;
        }

        if (!repeat) {
            f32 each = slack / (f32)midCount;
            s32 k;

            for (k = 0; k < midCount; k++) {
                if (out >= maxOut) {
                    break;
                }
                pos[out] = cursor;
                size[out] = each;
                cellIndex[out] = (s16)(lo + k);
                cursor += each;
                out++;
            }
        } else {
            f32 remaining = slack;
            s32 k = 0;

            while ((remaining > SO2H_UI_MIN_QUAD_PX) && (out < maxOut) && (k < SO2H_UI_MAX_REPEATS)) {
                f32 w = (remaining < cell) ? remaining : cell;

                pos[out] = cursor;
                size[out] = w;
                cellIndex[out] = (s16)(lo + (k % midCount));
                cursor += w;
                remaining -= w;
                out++;
                k++;
            }
        }
        i = hi; // the whole range has now been emitted
    }

    return out;
}

// A frame axis can produce many cells once it starts repeating. Static, because this module
// does not allocate.
#define SO2H_UI_FRAME_MAX_CELLS 96

static Gfx* So2h_UiDrawFrame(Gfx* gfx, const So2hUiSheetDef* sheet, const So2hUiRect* dst, const So2hUiRect* clip,
                             s32 repeat, s32 hollow, f32 scale) {
    f32 xPos[SO2H_UI_FRAME_MAX_CELLS];
    f32 xSize[SO2H_UI_FRAME_MAX_CELLS];
    s16 xCell[SO2H_UI_FRAME_MAX_CELLS];
    f32 yPos[SO2H_UI_FRAME_MAX_CELLS];
    f32 ySize[SO2H_UI_FRAME_MAX_CELLS];
    s16 yCell[SO2H_UI_FRAME_MAX_CELLS];
    f32 cell = So2h_UiCellScreen(sheet, scale);
    s32 nx;
    s32 ny;
    s32 ix;
    s32 iy;
    // The hole, in whole CELL INDICES. The rings are tile-aligned by construction, so this is
    // an exact integer count of tiles and never a pixel scan.
    // Measured from the CONTENT ring, not the border ring: only the fill cells are dropped, so
    // every decorative ring the art carries still draws. On a 5x5 @64 frame (content = 2 tiles)
    // that leaves exactly the one centre cell unrendered.
    s32 holeL = (hollow && (sheet->unit > 0)) ? (sheet->contentL / sheet->unit) : 0;
    s32 holeT = (hollow && (sheet->unit > 0)) ? (sheet->contentT / sheet->unit) : 0;
    s32 holeR = (hollow && (sheet->unit > 0)) ? (sheet->cols - 1 - (sheet->contentR / sheet->unit)) : -1;
    s32 holeB = (hollow && (sheet->unit > 0)) ? (sheet->rows - 1 - (sheet->contentB / sheet->unit)) : -1;

    nx = So2h_UiSolveFrameAxis(sheet->cols, sheet->runColLo, sheet->runColHi, cell, dst->x1 - dst->x0, repeat, xPos,
                               xSize, xCell, SO2H_UI_FRAME_MAX_CELLS);
    ny = So2h_UiSolveFrameAxis(sheet->rows, sheet->runRowLo, sheet->runRowHi, cell, dst->y1 - dst->y0, repeat, yPos,
                               ySize, yCell, SO2H_UI_FRAME_MAX_CELLS);

    for (iy = 0; iy < ny; iy++) {
        for (ix = 0; ix < nx; ix++) {
            So2hUiRect cellRect;
            f32 sx;
            f32 sy;
            f32 sw;
            f32 sh;

            // Inside the border ring on BOTH axes: this is the fill, so emit nothing and let
            // whatever is behind the frame show through.
            if (hollow && (xCell[ix] >= holeL) && (xCell[ix] <= holeR) && (yCell[iy] >= holeT) &&
                (yCell[iy] <= holeB)) {
                continue;
            }

            cellRect.x0 = dst->x0 + xPos[ix];
            cellRect.x1 = cellRect.x0 + xSize[ix];
            cellRect.y0 = dst->y0 + yPos[iy];
            cellRect.y1 = cellRect.y0 + ySize[iy];

            // Cheap reject before the source lookup - most cells of a clipped panel are out.
            if ((cellRect.x1 < clip->x0) || (cellRect.x0 > clip->x1) || (cellRect.y1 < clip->y0) ||
                (cellRect.y0 > clip->y1)) {
                continue;
            }

            So2h_UiCellSrc(sheet, xCell[ix], yCell[iy], &sx, &sy, &sw, &sh);

            // A cropped repeat shows only the leading part of its cell, so the source rect has
            // to be cropped by the same fraction or the seam shifts.
            if (xSize[ix] < (cell - 0.01f)) {
                sw *= (xSize[ix] / cell);
            }
            if (ySize[iy] < (cell - 0.01f)) {
                sh *= (ySize[iy] / cell);
            }
            if ((sw < 1.0f) || (sh < 1.0f)) {
                continue;
            }

            gfx = So2h_UiDrawQuad(gfx, sheet, sx, sy, sw, sh, &cellRect, clip);
        }
    }

    return gfx;
}

/**
 * TILE: one slice repeated at native pitch across the whole rect, edges cropped. Used for
 * backdrops and for the extendable strips (SongPreviewBackDrop's middle is exactly this).
 */
static Gfx* So2h_UiDrawTiled(Gfx* gfx, const So2hUiSheetDef* sheet, u16 slice, const So2hUiRect* dst,
                             const So2hUiRect* clip, f32 scale) {
    f32 cell = So2h_UiCellScreen(sheet, scale);
    f32 sx;
    f32 sy;
    f32 sw;
    f32 sh;
    f32 y;
    s32 guardY = 0;

    if ((cell <= 0.0f) || (sheet->cols <= 0)) {
        return gfx;
    }
    So2h_UiCellSrc(sheet, (s16)(slice % sheet->cols), (s16)(slice / sheet->cols), &sx, &sy, &sw, &sh);

    for (y = dst->y0; (y < dst->y1) && (guardY < SO2H_UI_MAX_REPEATS); y += cell, guardY++) {
        f32 x;
        s32 guardX = 0;
        f32 rowH = So2h_UiMinF(cell, dst->y1 - y);

        if ((y + cell) < clip->y0) {
            continue;
        }
        if (y > clip->y1) {
            break;
        }

        for (x = dst->x0; (x < dst->x1) && (guardX < SO2H_UI_MAX_REPEATS); x += cell, guardX++) {
            So2hUiRect cellRect;
            f32 colW = So2h_UiMinF(cell, dst->x1 - x);
            f32 qsw = sw * (colW / cell);
            f32 qsh = sh * (rowH / cell);

            if ((x + cell) < clip->x0) {
                continue;
            }
            if (x > clip->x1) {
                break;
            }
            // The trailing tile of a row or column is cropped, and a rect whose width is not a
            // whole number of tiles leaves a sliver behind. Cropping the source by the same
            // fraction can put it under one texel, which is a degenerate tile - drop it instead.
            if ((qsw < 1.0f) || (qsh < 1.0f)) {
                continue;
            }
            cellRect.x0 = x;
            cellRect.x1 = x + colW;
            cellRect.y0 = y;
            cellRect.y1 = y + rowH;

            gfx = So2h_UiDrawQuad(gfx, sheet, sx, sy, qsw, qsh, &cellRect, clip);
        }
    }

    return gfx;
}

// ---------------------------------------------------------------------------------------
// Public slice drawer
//
// This is what a content callback calls when it wants one piece of art in one place, and it
// is the same function the tree walk uses for every styled node. There is no second path a
// callback could take that the walk does not also take.
// ---------------------------------------------------------------------------------------
Gfx* So2h_UiDraw_Slice(Gfx* gfx, u16 sheetId, u16 slice, const So2hUiRect* rect, const So2hUiRect* clip, u8 mode,
                       u8 r, u8 g, u8 b, u8 a, f32 scale) {
    const So2hUiSheetDef* sheet = So2h_UiSheet_Def(sheetId);
    So2hUiRect fullClip;
    f32 sx;
    f32 sy;
    f32 sw;
    f32 sh;

    if ((sheet == NULL) || (rect == NULL) || (mode == SO2H_UI_DRAW_NONE) || (a == 0)) {
        return gfx;
    }
    if (clip == NULL) {
        fullClip = So2h_UiCtx()->node[0].rect;
        clip = &fullClip;
    }

    gfx = So2h_UiSetupMode(gfx, sheet->fmt, r, g, b, a);

    switch (mode) {
        // NINESLICE and RUN share one solver and differ in exactly one thing: what happens to
        // the run band when the frame is bigger than its art.
        //
        //   NINESLICE stretches it - one quad for the band, whatever the span.
        //   RUN repeats it at native pitch - one quad per whole tile plus a cropped remainder.
        //
        // This used to key off sheet->grow for both modes, which made every NINESLICE node in
        // the scene tile natively, because every sheet in the pack is authored grow = run. The
        // cost was not cosmetic: the 130x10 context rows emitted 117 quads each (a 64-texel
        // tile at scale 0.2175 is a 3.38 px cell, repeated 37 times), the walls emitted 252,
        // and the settled scene wanted ~39,000 Gfx words against a 16,384-word overlayBuffer.
        // Our own emitters decline once the arena is nearly full, but the vanilla writers
        // behind us in the frame do not check, so they ran off the end of the buffer and
        // Fast3D executed garbage - the pause hang, with no log line and no stack. See
        // SO2H_FREEZE_ROOTCAUSE.md.
        //
        // So the mode decides, and the sheet only gets a veto: a STRETCH-grow sheet is never
        // tiled even when a descriptor asks for RUN, because stretching is what that art is
        // drawn for. A panel that genuinely wants whole native tiles asks for RUN.
        case SO2H_UI_DRAW_NINESLICE:
            gfx = So2h_UiDrawFrame(gfx, sheet, rect, clip, false, false, scale);
            break;

        case SO2H_UI_DRAW_RUN:
            gfx = So2h_UiDrawFrame(gfx, sheet, rect, clip, sheet->grow != SO2H_UI_GROW_STRETCH, false, scale);
            break;

        // The frame's chrome only - the fill cells are skipped, so the rect has a real hole in
        // it. Same solver, same seams, one flag.
        case SO2H_UI_DRAW_RING:
            gfx = So2h_UiDrawFrame(gfx, sheet, rect, clip, sheet->grow != SO2H_UI_GROW_STRETCH, true, scale);
            break;

        case SO2H_UI_DRAW_TILE:
            gfx = So2h_UiDrawTiled(gfx, sheet, slice, rect, clip, scale);
            break;

        case SO2H_UI_DRAW_NATIVE: {
            So2hUiRect nat;
            f32 cell = So2h_UiCellScreen(sheet, scale);
            f32 cx = (rect->x0 + rect->x1) * 0.5f;
            f32 cy = (rect->y0 + rect->y1) * 0.5f;

            if (!So2h_UiSheet_SliceRectF(sheetId, slice, &sx, &sy, &sw, &sh)) {
                break;
            }
            nat.x0 = cx - (cell * 0.5f);
            nat.x1 = cx + (cell * 0.5f);
            nat.y0 = cy - (cell * 0.5f);
            nat.y1 = cy + (cell * 0.5f);
            gfx = So2h_UiDrawQuad(gfx, sheet, sx, sy, sw, sh, &nat, clip);
            break;
        }

        case SO2H_UI_DRAW_STRETCH:
        default:
            if (!So2h_UiSheet_SliceRectF(sheetId, slice, &sx, &sy, &sw, &sh)) {
                break;
            }
            gfx = So2h_UiDrawQuad(gfx, sheet, sx, sy, sw, sh, rect, clip);
            break;
    }

    return So2h_UiRestoreBlend(gfx);
}

// ---------------------------------------------------------------------------------------
// The tree walk
// ---------------------------------------------------------------------------------------

/**
 * SLIDE reveal, applied at draw time rather than in the solver so that the solved rect a
 * callback reads stays the settled one - content should never have to know an entrance
 * animation is in progress.
 */
static void So2h_UiApplyReveal(const So2hUiDesc* d, const So2hUiNode* n, So2hUiRect* rect) {
    *rect = n->rect;

    if (d->style.reveal == SO2H_UI_REVEAL_SLIDE) {
        f32 t = So2h_Ui_SmoothStep(n->revealT);
        f32 dy = (1.0f - t) * (rect->y1 - rect->y0) * 0.35f;

        rect->y0 += dy;
        rect->y1 += dy;
    }
}

/**
 * UNROLL is the RUN solver played forwards: the frame is drawn to a fraction of its width and
 * grows to full, so a panel appears to build itself out of its own tiles.
 */
static void So2h_UiApplyUnroll(const So2hUiDesc* d, const So2hUiNode* n, So2hUiRect* rect) {
    f32 t;

    if (d->style.reveal != SO2H_UI_REVEAL_UNROLL) {
        return;
    }
    t = So2h_Ui_SmoothStep(n->revealT);
    if (t >= 1.0f) {
        return;
    }
    if (d->growEnd == 1) {
        rect->x0 = rect->x1 - ((rect->x1 - rect->x0) * t);
    } else {
        rect->x1 = rect->x0 + ((rect->x1 - rect->x0) * t);
    }
}

// ---------------------------------------------------------------------------------------
// Dev outlines
//
// One 1px box per visible node, drawn over everything after the layer walk. The colour is
// keyed off the node index so two adjacent windows never come out the same shade, which is
// most of what makes this useful when a rect lands somewhere unexpected.
// ---------------------------------------------------------------------------------------
static s32 sOutlines = 0;

void So2h_Ui_SetOutlines(s32 on) {
    sOutlines = on;
}

static u32 So2h_UiOutlineMix(u32 v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;

    return v;
}

static Gfx* So2h_UiOutlineEdge(Gfx* gfx, s16 x0, s16 y0, s16 x1, s16 y1, u8 r, u8 g, u8 b) {
    if ((x1 <= x0) || (y1 <= y0) || !So2h_UiGfxRoom(gfx, 4)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, 200);
    gDPFillWideRectangle(gfx++, x0, y0, x1, y1);

    return gfx;
}

static Gfx* So2h_UiDrawOutlines(Gfx* gfx, So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        const So2hUiNode* n = &ctx->node[i];
        u32 h = So2h_UiOutlineMix((u32)i + 1u);
        u8 r = (u8)(96 + (h & 0x7F));
        u8 g = (u8)(96 + ((h >> 8) & 0x7F));
        u8 b = (u8)(96 + ((h >> 16) & 0x7F));
        s16 x0;
        s16 y0;
        s16 x1;
        s16 y1;

        if (!n->visible || (n->alpha <= 0.0f)) {
            continue;
        }
        x0 = So2h_UiRnd(n->rect.x0);
        y0 = So2h_UiRnd(n->rect.y0);
        x1 = So2h_UiRnd(n->rect.x1);
        y1 = So2h_UiRnd(n->rect.y1);

        gfx = So2h_UiOutlineEdge(gfx, x0, y0, x1, (s16)(y0 + 1), r, g, b);
        gfx = So2h_UiOutlineEdge(gfx, x0, (s16)(y1 - 1), x1, y1, r, g, b);
        gfx = So2h_UiOutlineEdge(gfx, x0, y0, (s16)(x0 + 1), y1, r, g, b);
        gfx = So2h_UiOutlineEdge(gfx, (s16)(x1 - 1), y0, x1, y1, r, g, b);
    }

    return gfx;
}

Gfx* So2h_UiDraw_Tree(Gfx* gfx) {
    So2hUiCtx* ctx = So2h_UiCtx();
    s32 layer;
    s32 scissored = 0;
    s32 drawn = 0;
    s32 trace = (sTraceFrames > 0);

    if (!ctx->initialised) {
        return gfx;
    }

    if (trace) {
        So2h_UiTrace("tree.begin", ctx->descCount, sDrawMax);
        sTraceFrames--;
    }

    // The pause pages render through a shrunken viewport, which leaves the scissor clipped to
    // the window rect (View_ApplyLetterbox scissors to view->viewport). Restore the full
    // screen before anything outside that window is drawn.
    gfx = So2h_UiScissorFull(gfx);

    // Layer order first, table order within a layer. Two passes over a 192-entry table is
    // nothing, and it means a node's draw order is a number in its style rather than its
    // position in a hand-maintained call sequence - which is what the first pass had, and
    // what made every insertion a merge conflict.
    for (layer = 0; layer < SO2H_UI_MAX_LAYERS; layer++) {
        s32 i;

        for (i = 0; i < ctx->descCount; i++) {
            const So2hUiDesc* d = &ctx->desc[i];
            const So2hUiNode* n = &ctx->node[i];
            So2hUiRect rect;
            So2hUiDrawFn drawFn;
            void* drawArg;
            u8 alpha;

            if (!n->visible || (d->style.layer != layer)) {
                continue;
            }
            if (n->alpha <= 0.0f) {
                continue;
            }

            alpha = (u8)(n->alpha * 255.0f);
            if (alpha == 0) {
                continue;
            }

            // Live bisection stop. Counted in nodes actually drawn, not table index, so the
            // number the player lands on is the same number this log prints.
            if ((sDrawMax >= 0) && (drawn >= sDrawMax)) {
                if (trace) {
                    So2h_UiTrace("tree.limit", i, drawn);
                }
                return So2h_UiRestoreBlend(gfx);
            }
            drawn++;

            So2h_UiApplyReveal(d, n, &rect);
            So2h_UiApplyUnroll(d, n, &rect);

            if (trace) {
                // Sheet and solved size as well as the index: a hang caused by a degenerate or
                // enormous rect is obvious from the numbers, without another build.
                So2h_UiTrace("node", i, (s32)d->style.sheet);
                So2h_UiTrace("node.size", (s32)(rect.x1 - rect.x0), (s32)(rect.y1 - rect.y0));
            }

            if ((d->style.drawMode != SO2H_UI_DRAW_NONE) && (d->style.sheet != SO2H_UI_INVALID)) {
                u8 cr = (d->style.r | d->style.g | d->style.b) ? d->style.r : 255;
                u8 cg = (d->style.r | d->style.g | d->style.b) ? d->style.g : 255;
                u8 cb = (d->style.r | d->style.g | d->style.b) ? d->style.b : 255;

                gfx = So2h_UiDraw_Slice(gfx, d->style.sheet, d->style.slice, &rect, &n->clipRect, d->style.drawMode,
                                        cr, cg, cb, alpha, d->style.scale);
            }

            // A runtime binding wins over the descriptor's own callback; see So2h_Ui_BindDraw.
            drawFn = (ctx->drawFn[i] != NULL) ? ctx->drawFn[i] : d->draw;
            drawArg = (ctx->drawFn[i] != NULL) ? ctx->drawArg[i] : NULL;

            if (trace) {
                So2h_UiTrace("node.art.done", i, drawn);
            }

            if (drawFn != NULL) {
                // A callback gets a hardware scissor because we cannot crop what it emits.
                // It is approximate on the widescreen wings; see the file header.
                if (d->clip != SO2H_UI_CLIP_SPILL) {
                    gfx = So2h_UiScissor(gfx, &n->clipRect);
                    scissored = 1;
                } else if (scissored) {
                    gfx = So2h_UiScissorFull(gfx);
                    scissored = 0;
                }

                gfx = drawFn(gfx, (So2hUiId)i, &rect, drawArg);

                if (scissored) {
                    gfx = So2h_UiScissorFull(gfx);
                    scissored = 0;
                }
            }
        }
    }

    if (trace) {
        So2h_UiTrace("tree.end", drawn, 0);
    }

    if (sOutlines) {
        gfx = So2h_UiScissorFull(gfx);
        gfx = So2h_UiDrawOutlines(gfx, ctx);
    }

    return So2h_UiRestoreBlend(gfx);
}
