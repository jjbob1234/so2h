/*
 * File: so2h_ui_layout.c
 * Description: SO2H [Menu] so2h_ui constraint solver.
 *
 * ---------------------------------------------------------------------------------------
 * HOW A FRAME IS SOLVED
 * ---------------------------------------------------------------------------------------
 *   1. Read the screen span and derive the aspect (hysteresis picks the active canon).
 *   2. Walk the descriptor table parents-first, ONCE PER CANON, solving every node in that
 *      canon's design space and converting the result to screen space as it goes.
 *   3. Lerp the two screen-space results by the eased morph factor, with a small squash /
 *      stretch overshoot about each node's own centre.
 *
 * Solving in design space and converting per canon - rather than lerping design rects and
 * converting once - is what stops a morph from skewing. That rule is inherited from
 * so2h_quest_layout.c, along with the frame count, the overshoot and the hysteresis band.
 *
 * Everything below is pure arithmetic. No graphics, no input, no allocation.
 */

#include "so2h_ui_internal.h"
#include "BenPort.h"

#include <string.h>

#define SO2H_UI_TILE_SRC 32.0f  // sheets are authored on a 32 px grid ...
#define SO2H_UI_MOCK_H 988.0f   // ... against a 988 px tall mock
#define SO2H_UI_TILE_MIN 4.0f

// RUN collapse tuning. `EASE` is the fraction of remaining travel consumed per frame and
// `STAGGER` is how far tile k+1 must have travelled before tile k starts moving - together
// they are what makes a shortening run read as a cascade instead of a squeeze.
#define SO2H_UI_RUN_EASE 0.125f
#define SO2H_UI_RUN_MIN_STEP 0.15f
#define SO2H_UI_RUN_STAGGER 0.6f
#define SO2H_UI_RUN_MAX_OVERLAP 0.5f // a tile may slide at most half under its neighbour

#define SO2H_UI_SCROLL_EASE 0.2f
#define SO2H_UI_SCROLL_MIN_STEP 0.01f

static f32 So2h_UiLerp(f32 a, f32 b, f32 t) {
    return a + ((b - a) * t);
}

/**
 * Parabolic pulse: 0 at both ends, 1 at the midpoint. Drives the squash/stretch overshoot
 * without pulling sinf into the overlay.
 */
static f32 So2h_UiPulse(f32 t) {
    if ((t <= 0.0f) || (t >= 1.0f)) {
        return 0.0f;
    }
    return 4.0f * t * (1.0f - t);
}

static f32 So2h_UiClampF(f32 v, f32 lo, f32 hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

// ---------------------------------------------------------------------------------------
// Axis constraint. Two floats and a mode cover every placement the old dual canon tables
// expressed, plus stretch-both-edges and centre-with-offset, which they could not.
// ---------------------------------------------------------------------------------------
static void So2h_UiSolveAxis(const So2hUiAxis* axis, f32 parentPos, f32 parentSize, f32* outPos, f32* outSize) {
    f32 pos;
    f32 size;

    switch (axis->mode) {
        case SO2H_UI_AXIS_END:
            size = axis->b;
            pos = parentSize - axis->a - size;
            break;

        case SO2H_UI_AXIS_STRETCH:
            pos = axis->a;
            size = parentSize - axis->a - axis->b;
            break;

        case SO2H_UI_AXIS_CENTER:
            size = axis->b;
            pos = ((parentSize - size) * 0.5f) + axis->a;
            break;

        case SO2H_UI_AXIS_FILL:
            pos = 0.0f;
            size = parentSize;
            break;

        case SO2H_UI_AXIS_START:
        default:
            pos = axis->a;
            size = axis->b;
            break;
    }

    if (size < 0.0f) {
        size = 0.0f;
    }
    *outPos = parentPos + pos;
    *outSize = size;
}

/**
 * Sparse per-canon override lookup. Only axes that genuinely differ between 4:3 and wide get
 * a row, so a node whose geometry is aspect-independent is authored exactly once.
 */
static const So2hUiVariant* So2h_UiFindVariant(const So2hUiCtx* ctx, So2hUiId id, s32 canon) {
    s32 i;

    for (i = 0; i < ctx->variantCount; i++) {
        if ((ctx->variants[i].node == id) && ((s32)ctx->variants[i].canon == canon)) {
            return &ctx->variants[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------
// RUN: integer tiles at native pitch, shortening by sliding the far tile under its neighbour
//
// Contract, as specified:
//   - tile count stays an integer and tiles keep their native size
//   - a deficit is absorbed from the collapse end, one tile at a time, staggered
//   - the anchored end never moves at any aspect
// ---------------------------------------------------------------------------------------
static void So2h_UiSolveRun(const So2hUiDesc* d, f32 span, f32 pitch, So2hUiRunState* st) {
    s32 maxTiles = (s32)(sizeof(st->gap) / sizeof(st->gap[0]));
    s32 want;
    f32 deficit;
    s32 k;

    if ((pitch <= 0.0f) || (st == NULL)) {
        return;
    }

    want = d->cols;
    if (want <= 0) {
        want = (s32)(span / pitch);
    }
    if (want > maxTiles) {
        want = maxTiles;
    }
    if (want < 1) {
        want = 1;
    }

    if (!st->seeded) {
        // Snap on first sight; animating in from nothing on pause-open reads as a glitch.
        st->count = (s16)want;
        st->targetCount = (s16)want;
        for (k = 0; k < maxTiles; k++) {
            st->gap[k] = 0.0f;
        }
        st->fade = 1.0f;
        st->seeded = 1;
    }

    st->targetCount = (s16)want;
    if (st->count < st->targetCount) {
        st->count = st->targetCount; // growing is instant; only shrinking cascades
        st->fade = 1.0f;
    }

    deficit = ((f32)st->count * pitch) - span;
    if (deficit < 0.0f) {
        deficit = 0.0f;
    }

    // Distribute the deficit greedily from the collapse end. Tile k takes up to half a pitch;
    // tiles nearest the anchor keep exact native spacing, which is what reads as a cascade.
    {
        f32 remaining = deficit;
        f32 maxOverlap = pitch * SO2H_UI_RUN_MAX_OVERLAP;

        for (k = st->count - 1; k >= 0; k--) {
            f32 target = (remaining > maxOverlap) ? maxOverlap : remaining;
            f32 delta;
            f32 step;

            // Stagger: this tile does not begin closing until the one outside it has covered
            // SO2H_UI_RUN_STAGGER of its own travel.
            if ((k < (st->count - 1)) && (maxOverlap > 0.0f)) {
                f32 outerProgress = st->gap[k + 1] / maxOverlap;

                if (outerProgress < SO2H_UI_RUN_STAGGER) {
                    target = st->gap[k];
                }
            }

            delta = target - st->gap[k];
            step = delta * SO2H_UI_RUN_EASE;

            if ((step < SO2H_UI_RUN_MIN_STEP) && (step > -SO2H_UI_RUN_MIN_STEP)) {
                step = delta; // guarantees termination rather than an asymptote
            }
            st->gap[k] += step;
            st->gap[k] = So2h_UiClampF(st->gap[k], 0.0f, maxOverlap);

            remaining -= st->gap[k];
            if (remaining < 0.0f) {
                remaining = 0.0f;
            }
        }

        // Every gap maxed out and it is still short: drop a tile. The dropped tile finishes
        // its slide to full overlap and fades over the last quarter of that travel.
        if ((remaining > 0.0f) && (st->count > 1)) {
            st->fade -= 0.25f;
            if (st->fade <= 0.0f) {
                st->count--;
                st->fade = 1.0f;
            }
        } else {
            st->fade = 1.0f;
        }
    }
}

/**
 * Writes tile `k`'s rect within a solved RUN container. Walked collapse-end -> anchor by the
 * draw code, so a sliding tile is emitted before the tile it slides beneath and therefore
 * ends up underneath it.
 */
static void So2h_UiRunTileRect(const So2hUiRunState* st, const So2hUiRect* rect, f32 pitch, s32 fromEnd, s32 k,
                               So2hUiRect* out) {
    f32 offset = 0.0f;
    s32 i;

    for (i = 0; i < k; i++) {
        offset += pitch - st->gap[i];
    }

    if (fromEnd) {
        out->x1 = rect->x1 - offset;
        out->x0 = out->x1 - pitch;
    } else {
        out->x0 = rect->x0 + offset;
        out->x1 = out->x0 + pitch;
    }
    out->y0 = rect->y0;
    out->y1 = rect->y1;
}

// ---------------------------------------------------------------------------------------
// SCROLL_ROW: fixed pitch, content panned past a clip window
//
// The deliberate opposite of RUN. RUN absorbs a width deficit by overlapping tiles;
// SCROLL_ROW keeps pitch exact and moves the content instead. A container is one or the
// other, never both, so a row of equipment never silently starts overlapping its own items.
// ---------------------------------------------------------------------------------------
static void So2h_UiSolveScroll(const So2hUiDesc* d, f32 span, f32 pitch, So2hUiScrollState* st) {
    f32 visible;
    f32 maxOffset;
    f32 delta;

    if ((pitch <= 0.0f) || (st == NULL)) {
        return;
    }
    visible = span / pitch;
    maxOffset = (f32)d->count - visible;
    if (maxOffset < 0.0f) {
        maxOffset = 0.0f; // shorter than the window: centred, never scrolls
    }

    // Keep the focused cell inside the window, scrolling by the minimum needed.
    {
        f32 idx = (f32)st->index;

        if (idx < st->target) {
            st->target = idx;
        } else if (idx > ((st->target + visible) - 1.0f)) {
            st->target = (idx - visible) + 1.0f;
        }
    }
    st->target = So2h_UiClampF(st->target, 0.0f, maxOffset);

    if (!st->seeded) {
        st->offset = st->target;
        st->seeded = 1;
        return;
    }

    delta = st->target - st->offset;
    if ((delta < SO2H_UI_SCROLL_MIN_STEP) && (delta > -SO2H_UI_SCROLL_MIN_STEP)) {
        st->offset = st->target;
    } else {
        st->offset += delta * SO2H_UI_SCROLL_EASE;
    }
}

// ---------------------------------------------------------------------------------------
// Cell rects
// ---------------------------------------------------------------------------------------
static void So2h_UiGridCell(const So2hUiRect* rect, s16 cols, s16 rows, s16 col, s16 row, f32 gap, So2hUiRect* out) {
    f32 cw;
    f32 ch;

    if ((cols <= 0) || (rows <= 0) || (col < 0) || (row < 0) || (col >= cols) || (row >= rows)) {
        out->x0 = out->y0 = out->x1 = out->y1 = 0.0f;
        return;
    }
    cw = (rect->x1 - rect->x0) / (f32)cols;
    ch = (rect->y1 - rect->y0) / (f32)rows;

    out->x0 = rect->x0 + (cw * (f32)col) + gap;
    out->y0 = rect->y0 + (ch * (f32)row) + gap;
    out->x1 = (rect->x0 + (cw * (f32)(col + 1))) - gap;
    out->y1 = (rect->y0 + (ch * (f32)(row + 1))) - gap;

    if (out->x1 < out->x0) {
        out->x1 = out->x0;
    }
    if (out->y1 < out->y0) {
        out->y1 = out->y0;
    }
}

void So2h_Ui_GetCellRect(So2hUiId parent, s16 index, So2hUiRect* out) {
    So2hUiCtx* ctx = So2h_UiCtx();
    const So2hUiDesc* d = So2h_UiNode_Desc(parent);
    const So2hUiRect* rect;

    if (out == NULL) {
        return;
    }
    out->x0 = out->y0 = out->x1 = out->y1 = 0.0f;

    if (d == NULL) {
        return;
    }
    rect = &ctx->node[parent].rect;

    if (d->layout == SO2H_UI_LAYOUT_GRID) {
        if (d->cols <= 0) {
            return;
        }
        So2h_UiGridCell(rect, d->cols, d->rows, (s16)(index % d->cols), (s16)(index / d->cols),
                        d->gap * (ctx->tile / SO2H_UI_TILE_SRC), out);
        return;
    }

    if ((d->layout == SO2H_UI_LAYOUT_SCROLL_ROW) || (d->layout == SO2H_UI_LAYOUT_SCROLL_COL)) {
        So2hUiScrollState* st = So2h_UiNode_Scroll(parent);
        f32 pitch = d->pitch * (ctx->tile / SO2H_UI_TILE_SRC);
        s32 vertical = (d->layout == SO2H_UI_LAYOUT_SCROLL_COL);
        f32 lead;

        if ((st == NULL) || (pitch <= 0.0f) || (index < 0) || (index >= d->count)) {
            return;
        }

        // One offset equation for both axes. Items append to whichever end the descriptor
        // names, so a strip that grows leftward/upward and one that grows rightward/downward
        // are the same code and a one-field difference.
        if (d->growEnd == 1) {
            lead = ((st->offset - (f32)index) * pitch);
        } else {
            lead = (((f32)index - st->offset) * pitch);
        }

        if (vertical) {
            out->x0 = rect->x0;
            out->x1 = rect->x1;
            if (d->growEnd == 1) {
                out->y1 = rect->y1 + lead;
                out->y0 = out->y1 - pitch;
            } else {
                out->y0 = rect->y0 + lead;
                out->y1 = out->y0 + pitch;
            }
        } else {
            out->y0 = rect->y0;
            out->y1 = rect->y1;
            if (d->growEnd == 1) {
                out->x1 = rect->x1 + lead;
                out->x0 = out->x1 - pitch;
            } else {
                out->x0 = rect->x0 + lead;
                out->x1 = out->x0 + pitch;
            }
        }
        return;
    }

    if (d->layout == SO2H_UI_LAYOUT_RUN) {
        So2hUiRunState* st = So2h_UiNode_Run(parent);
        f32 pitch = d->pitch * (ctx->tile / SO2H_UI_TILE_SRC);

        if ((st == NULL) || (pitch <= 0.0f) || (index < 0) || (index >= st->count)) {
            return;
        }
        So2h_UiRunTileRect(st, rect, pitch, (d->growEnd == 1), index, out);
    }
}

// ---------------------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------------------
static void So2h_UiSolveCanon(So2hUiCtx* ctx, s32 canon) {
    f32 designW = (canon == SO2H_UI_CANON_WIDE) ? SO2H_UI_DESIGN_W_WIDE : SO2H_UI_DESIGN_W_43;
    f32 sx = ctx->screenW / designW;
    f32 sy = 1.0f; // design height is always 240, and so is screen height
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        const So2hUiDesc* d = &ctx->desc[i];
        const So2hUiVariant* v = So2h_UiFindVariant(ctx, (So2hUiId)i, canon);
        const So2hUiAxis* ax = ((v != NULL) && v->hasX) ? &v->x : &d->x;
        const So2hUiAxis* ay = ((v != NULL) && v->hasY) ? &v->y : &d->y;
        So2hUiRect* out = &ctx->node[i].canonRect[canon];
        f32 px, py, pw, ph;
        f32 x, y, w, h;

        if (i == 0) {
            // Root is the whole screen, in screen space directly.
            out->x0 = ctx->screenX0;
            out->y0 = 0.0f;
            out->x1 = ctx->screenX1;
            out->y1 = SO2H_UI_DESIGN_H;
            continue;
        }

        if (ctx->node[i].state == SO2H_UI_HIDDEN) {
            out->x0 = out->y0 = out->x1 = out->y1 = 0.0f;
            continue;
        }

        {
            const So2hUiRect* pr = &ctx->node[d->parent].canonRect[canon];

            px = pr->x0;
            py = pr->y0;
            pw = pr->x1 - pr->x0;
            ph = pr->y1 - pr->y0;
        }

        // Constraints are authored in design units, so convert the parent to design space,
        // solve there, and convert back. Doing it in this order is what lets one descriptor
        // row be correct at both canons.
        So2h_UiSolveAxis(ax, px / sx, pw / sx, &x, &w);
        So2h_UiSolveAxis(ay, py / sy, ph / sy, &y, &h);

        out->x0 = x * sx;
        out->y0 = y * sy;
        out->x1 = (x + w) * sx;
        out->y1 = (y + h) * sy;
    }
}

/**
 * Resolves state top-down: anything under a HIDDEN ancestor, or under a PAGE that is not its
 * PAGEGROUP's active page, is itself hidden. Runs before the geometry walk so hidden subtrees
 * cost nothing to solve.
 */
static void So2h_UiResolveStates(So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        const So2hUiDesc* d = &ctx->desc[i];
        u8 state = So2h_UiNode_ResolvedState((So2hUiId)i);

        if (i > 0) {
            const So2hUiDesc* pd = &ctx->desc[d->parent];
            u8 parentState = ctx->node[d->parent].state;

            if (parentState == SO2H_UI_HIDDEN) {
                state = SO2H_UI_HIDDEN;
            } else if (parentState == SO2H_UI_DISABLED) {
                state = SO2H_UI_DISABLED;
            }

            // A page is only live when its group says so.
            if ((d->kind == SO2H_UI_PAGE) && (pd->kind == SO2H_UI_PAGEGROUP)) {
                So2hUiPageState* ps = So2h_UiNode_Page(d->parent);

                if ((ps == NULL) || (ps->active != (So2hUiId)i)) {
                    state = SO2H_UI_HIDDEN;
                }
            }
        }

        ctx->node[i].state = state;
        ctx->node[i].visible = (state != SO2H_UI_HIDDEN);
        ctx->node[i].focusable = (state == SO2H_UI_ENABLED) && (d->kind != SO2H_UI_DECOR) &&
                                 (d->kind != SO2H_UI_GROUP) && (d->kind != SO2H_UI_PAGEGROUP);
    }
}

/**
 * Clip rects are resolved after geometry, top-down: CLIP_SELF intersects the inherited
 * scissor with the node's own rect, CLIP_SPILL discards it and takes the root, and
 * CLIP_INHERIT passes the parent's through untouched. That is what lets the notebook overhang
 * its grid and the song-preview window sit off-grid without either of them being a special
 * case in the drawer.
 */
static void So2h_UiResolveClips(So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        const So2hUiDesc* d = &ctx->desc[i];
        So2hUiNode* n = &ctx->node[i];
        So2hUiRect inherited;

        if (i == 0) {
            n->clipRect = n->rect;
            continue;
        }
        inherited = ctx->node[d->parent].clipRect;

        switch (d->clip) {
            case SO2H_UI_CLIP_SELF:
                n->clipRect.x0 = (n->rect.x0 > inherited.x0) ? n->rect.x0 : inherited.x0;
                n->clipRect.y0 = (n->rect.y0 > inherited.y0) ? n->rect.y0 : inherited.y0;
                n->clipRect.x1 = (n->rect.x1 < inherited.x1) ? n->rect.x1 : inherited.x1;
                n->clipRect.y1 = (n->rect.y1 < inherited.y1) ? n->rect.y1 : inherited.y1;
                break;

            case SO2H_UI_CLIP_SPILL:
                n->clipRect = ctx->node[0].rect;
                break;

            case SO2H_UI_CLIP_INHERIT:
            default:
                n->clipRect = inherited;
                break;
        }

        if (n->clipRect.x1 < n->clipRect.x0) {
            n->clipRect.x1 = n->clipRect.x0;
        }
        if (n->clipRect.y1 < n->clipRect.y0) {
            n->clipRect.y1 = n->clipRect.y0;
        }
    }
}

/**
 * Reveal is one shared 0..1 per node on the same curve as everything else, so FADE, SLIDE and
 * UNROLL are three readings of one number rather than three animation systems.
 */
static void So2h_UiAdvanceReveals(So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        So2hUiNode* n = &ctx->node[i];
        const So2hUiDesc* d = &ctx->desc[i];
        f32 base = (d->style.alpha != 0) ? ((f32)d->style.alpha / 255.0f) : 1.0f;

        if (!n->visible) {
            n->revealT = 0.0f;
            n->alpha = 0.0f;
            continue;
        }
        if (d->style.reveal == SO2H_UI_REVEAL_NONE) {
            n->revealT = 1.0f;
        } else if (n->revealT < 1.0f) {
            n->revealT += 1.0f / (f32)SO2H_UI_MORPH_FRAMES;
            if (n->revealT > 1.0f) {
                n->revealT = 1.0f;
            }
        }

        n->alpha = base;
        if (d->style.reveal == SO2H_UI_REVEAL_FADE) {
            n->alpha *= So2h_Ui_SmoothStep(n->revealT);
        }
        if (n->state == SO2H_UI_DISABLED) {
            n->alpha *= 0.45f;
        }
    }
}

void So2h_UiLayout_Update(void) {
    So2hUiCtx* ctx = So2h_UiCtx();
    f32 t;
    f32 pulse;
    s32 i;

    ctx->screenX0 = (f32)OTRGetRectDimensionFromLeftEdge(0);
    ctx->screenX1 = (f32)OTRGetRectDimensionFromRightEdge(320);
    ctx->screenW = ctx->screenX1 - ctx->screenX0;
    if (ctx->screenW < 1.0f) {
        ctx->screenW = SO2H_UI_DESIGN_W_43;
        ctx->screenX0 = 0.0f;
        ctx->screenX1 = SO2H_UI_DESIGN_W_43;
    }
    ctx->aspect = ctx->screenW / SO2H_UI_DESIGN_H;

    ctx->tile = (SO2H_UI_DESIGN_H * SO2H_UI_TILE_SRC) / SO2H_UI_MOCK_H;
    if (ctx->tile < SO2H_UI_TILE_MIN) {
        ctx->tile = SO2H_UI_TILE_MIN;
    }

    // Hysteresis: inside the dead band whichever canon is already active stays active, so an
    // aspect sitting exactly on the boundary cannot oscillate.
    if (ctx->aspect >= SO2H_UI_ASPECT_WIDE) {
        ctx->activeCanon = SO2H_UI_CANON_WIDE;
    } else if (ctx->aspect <= SO2H_UI_ASPECT_43) {
        ctx->activeCanon = SO2H_UI_CANON_43;
    }

    if (ctx->activeCanon == SO2H_UI_CANON_WIDE) {
        if (ctx->morphFrames < SO2H_UI_MORPH_FRAMES) {
            ctx->morphFrames++;
        }
    } else {
        if (ctx->morphFrames > 0) {
            ctx->morphFrames--;
        }
    }
    t = (f32)ctx->morphFrames / (f32)SO2H_UI_MORPH_FRAMES;
    ctx->morphT = So2h_Ui_SmoothStep(t);
    pulse = So2h_UiPulse(t) * SO2H_UI_OVERSHOOT;

    So2h_UiResolveStates(ctx);
    So2h_UiSolveCanon(ctx, SO2H_UI_CANON_43);
    So2h_UiSolveCanon(ctx, SO2H_UI_CANON_WIDE);

    // Blend in screen space, then apply the overshoot about each node's own centre so the
    // change reads as motion rather than a jump cut.
    for (i = 0; i < ctx->descCount; i++) {
        So2hUiNode* n = &ctx->node[i];
        const So2hUiRect* a = &n->canonRect[SO2H_UI_CANON_43];
        const So2hUiRect* b = &n->canonRect[SO2H_UI_CANON_WIDE];

        n->rect.x0 = So2h_UiLerp(a->x0, b->x0, ctx->morphT);
        n->rect.y0 = So2h_UiLerp(a->y0, b->y0, ctx->morphT);
        n->rect.x1 = So2h_UiLerp(a->x1, b->x1, ctx->morphT);
        n->rect.y1 = So2h_UiLerp(a->y1, b->y1, ctx->morphT);

        if (pulse > 0.0f) {
            f32 cx = (n->rect.x0 + n->rect.x1) * 0.5f;
            f32 cy = (n->rect.y0 + n->rect.y1) * 0.5f;
            f32 hw = (n->rect.x1 - n->rect.x0) * 0.5f * (1.0f + pulse);
            f32 hh = (n->rect.y1 - n->rect.y0) * 0.5f * (1.0f - pulse);

            n->rect.x0 = cx - hw;
            n->rect.x1 = cx + hw;
            n->rect.y0 = cy - hh;
            n->rect.y1 = cy + hh;
        }
    }

    So2h_UiResolveClips(ctx);

    // Runs and scroll rows animate after geometry, because both need the solved span.
    for (i = 0; i < ctx->descCount; i++) {
        const So2hUiDesc* d = &ctx->desc[i];
        So2hUiNode* n = &ctx->node[i];
        f32 pitch = d->pitch * (ctx->tile / SO2H_UI_TILE_SRC);

        if (!n->visible) {
            continue;
        }
        if (d->layout == SO2H_UI_LAYOUT_RUN) {
            So2h_UiSolveRun(d, n->rect.x1 - n->rect.x0, pitch, So2h_UiNode_Run((So2hUiId)i));
        } else if (d->layout == SO2H_UI_LAYOUT_SCROLL_ROW) {
            So2h_UiSolveScroll(d, n->rect.x1 - n->rect.x0, pitch, So2h_UiNode_Scroll((So2hUiId)i));
        } else if (d->layout == SO2H_UI_LAYOUT_SCROLL_COL) {
            // Same solver, the other span. Nothing in it is axis-aware.
            So2h_UiSolveScroll(d, n->rect.y1 - n->rect.y0, pitch, So2h_UiNode_Scroll((So2hUiId)i));
        }
    }

    So2h_UiAdvanceReveals(ctx);
}
