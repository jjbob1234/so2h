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

/**
 * Length of the travel vector. A slide is always axis-aligned (one component is 0), so this is
 * an absolute value in practice - written as a length so the POP overshoot stays correct if a
 * diagonal entrance is ever declared. Never 0: the caller divides by it.
 */
static f32 So2h_UiLen2(f32 x, f32 y) {
    f32 ax = (x < 0.0f) ? -x : x;
    f32 ay = (y < 0.0f) ? -y : y;
    f32 lo;
    f32 hi;
    f32 r;

    if (ax < ay) {
        lo = ax;
        hi = ay;
    } else {
        lo = ay;
        hi = ax;
    }
    if (hi <= 0.0f) {
        return 1.0f;
    }
    // Alpha-max-plus-beta-min: exact for an axis-aligned vector, and within 4% otherwise.
    r = hi + (0.428f * lo);
    return r;
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
/**
 * True for the draw modes that put a FRAME on screen - the ones that own a border and
 * therefore have a content rect distinct from their outer rect. A stretched or native single
 * slice is just a picture: it has no chrome, so it insets nothing.
 */
static s32 So2h_UiIsFrameMode(u8 mode) {
    return (mode == SO2H_UI_DRAW_NINESLICE) || (mode == SO2H_UI_DRAW_RUN) || (mode == SO2H_UI_DRAW_TILE);
}

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
            f32 bl = 0.0f;
            f32 bt = 0.0f;
            f32 br = 0.0f;
            f32 bb = 0.0f;

            // Solve against the ring of the parent this node attaches to - by default the
            // CONTENT rect, inside both the outer frame line and the inner lip. A framed
            // parent owns its chrome, and a child that ignores it lands underneath. Ring
            // widths come from the sheet in source texels, scaled by the same tilePx/unit
            // the frame art uses, so the inset can never drift from the art.
            {
                const So2hUiDesc* pd = &ctx->desc[d->parent];

                if (So2h_UiIsFrameMode(pd->style.drawMode)) {
                    So2h_UiSheet_RingPx(pd->style.sheet, d->attach, ctx->tile, &bl, &bt, &br, &bb);
                }
            }

            px = pr->x0 + bl;
            py = pr->y0 + bt;
            pw = (pr->x1 - pr->x0) - bl - br;
            ph = (pr->y1 - pr->y0) - bt - bb;

            // A panel narrower than its own chrome is a layout bug upstream, but it must not
            // produce an inverted rect here - clamp to an empty content box at the centre.
            if (pw < 0.0f) {
                px += pw * 0.5f;
                pw = 0.0f;
            }
            if (ph < 0.0f) {
                py += ph * 0.5f;
                ph = 0.0f;
            }
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
 * Displaces a node and everything under it. The descriptor table is authored parents-first, so
 * a subtree is a contiguous forward run and "move this window and its contents" is one pass
 * with no recursion and no child lists.
 */
static void So2h_UiOffsetSubtree(So2hUiCtx* ctx, s32 root, f32 dx, f32 dy) {
    s32 j;

    for (j = root; j < ctx->descCount; j++) {
        So2hUiNode* n;

        if ((j != root) && !ctx->node[j].inSubtree) {
            continue;
        }
        n = &ctx->node[j];
        n->rect.x0 += dx;
        n->rect.x1 += dx;
        n->rect.y0 += dy;
        n->rect.y1 += dy;
        n->clipRect.x0 += dx;
        n->clipRect.x1 += dx;
        n->clipRect.y0 += dy;
        n->clipRect.y1 += dy;
    }
}

/**
 * Marks node `root` and every descendant of it in ctx->node[].inSubtree. Parents-first
 * authoring is what makes the single forward pass correct.
 */
static void So2h_UiMarkSubtree(So2hUiCtx* ctx, s32 root) {
    s32 j;

    for (j = 0; j < ctx->descCount; j++) {
        ctx->node[j].inSubtree = 0;
    }
    ctx->node[root].inSubtree = 1;
    for (j = root + 1; j < ctx->descCount; j++) {
        So2hUiId parent = ctx->desc[j].parent;

        if ((parent != SO2H_UI_INVALID) && (parent < ctx->descCount) && ctx->node[parent].inSubtree) {
            ctx->node[j].inSubtree = 1;
        }
    }
}

static void So2h_UiMoveSubtree(So2hUiCtx* ctx, s32 root, f32 dx, f32 dy) {
    if ((dx == 0.0f) && (dy == 0.0f)) {
        return;
    }
    So2h_UiMarkSubtree(ctx, root);
    So2h_UiOffsetSubtree(ctx, root, dx, dy);
}

/**
 * Scales a node and its descendants about a pivot in screen units. GROW and the exit squash
 * are the same operation with different pivots, which is why neither needs its own code in
 * any window that uses them.
 */
static void So2h_UiStretchSubtree(So2hUiCtx* ctx, s32 root, f32 sx, f32 sy, f32 px, f32 py) {
    s32 j;

    So2h_UiMarkSubtree(ctx, root);
    for (j = root; j < ctx->descCount; j++) {
        So2hUiNode* n = &ctx->node[j];
        So2hUiRect* r;
        s32 k;

        if ((j != root) && !n->inSubtree) {
            continue;
        }
        for (k = 0; k < 2; k++) {
            r = (k == 0) ? &n->rect : &n->clipRect;
            r->x0 = px + ((r->x0 - px) * sx);
            r->x1 = px + ((r->x1 - px) * sx);
            r->y0 = py + ((r->y0 - py) * sy);
            r->y1 = py + ((r->y1 - py) * sy);
        }
    }
}

/**
 * This node's total wait before it starts moving: the table delay plus this open's slop.
 *
 * The table sets the CHOREOGRAPHY - which window is early, which is late - and the jitter
 * makes sure no two opens are ever bar-for-bar identical. Both are read here and nowhere
 * else, so a window joins the sequence by naming one number.
 */
static s32 So2h_UiRevealDelay(So2hUiCtx* ctx, s32 i) {
    const So2hUiStyle* st = &ctx->desc[i].style;

    if (st->revealJitter == 0) {
        return st->revealDelay;
    }
    return st->revealDelay + (s32)(So2h_UiHash((u32)i ^ ctx->openSeed) % (u32)(st->revealJitter + 1));
}

/** Same idea on the way out, but tighter: a beat of slop, not a second stagger. */
static s32 So2h_UiCloseDelay(So2hUiCtx* ctx, s32 i) {
    if (SO2H_UI_CLOSE_JITTER <= 0) {
        return 0;
    }
    return (s32)(So2h_UiHash(((u32)i * 7919u) ^ (ctx->openSeed + 0x9E3779B9u)) % (u32)(SO2H_UI_CLOSE_JITTER + 1));
}

/**
 * Reveal is one shared 0..1 per node on the same curve as everything else, so FADE, SLIDE,
 * FALL, POP, GROW and UNROLL are six readings of one number rather than six animation systems.
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
            // The stagger is spent BEFORE the node starts moving, so every window shares one
            // clock and the whole sequence is a column of delay numbers in the table.
            if (n->revealHold < So2h_UiRevealDelay(ctx, i)) {
                n->revealHold++;
            } else {
                // The two frames worth sounding are the two you can see: the frame this node
                // stops waiting and starts moving, and the frame it settles. One voice each,
                // per node, deliberately stacking - see so2h_ui_sfx.c.
                if (n->revealT == 0.0f) {
                    So2h_UiSfx_Slide(i, ctx->openSeed);
                }
                n->revealT += 1.0f / (f32)SO2H_UI_REVEAL_FRAMES;
                if (n->revealT >= 1.0f) {
                    n->revealT = 1.0f;
                    So2h_UiSfx_SlideStop(i);
                    So2h_UiSfx_Placed(i, ctx->openSeed);
                }
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

/**
 * The screen edge a travelling node enters through, and how far off it has to start.
 *
 * AUTO is DERIVED from the solved rect: whichever screen edge the node's own centre is nearest.
 * Distance is measured from the rect, so a node travels exactly far enough to be fully hidden -
 * no baked travel distance anywhere, and it holds at every aspect.
 */
static void So2h_UiSlideEdge(So2hUiCtx* ctx, s32 i, f32* dx, f32* dy) {
    const So2hUiDesc* d = &ctx->desc[i];
    const So2hUiRect* r = &ctx->node[i].rect;
    u8 e = d->style.revealFrom;

    *dx = 0.0f;
    *dy = 0.0f;

    if (d->style.reveal == SO2H_UI_REVEAL_FALL) {
        e = SO2H_UI_FROM_TOP; // a thing that falls comes from above. Never derived.
    }
    if (e == SO2H_UI_FROM_AUTO) {
        f32 cx = (r->x0 + r->x1) * 0.5f;
        f32 cy = (r->y0 + r->y1) * 0.5f;
        f32 dl = cx - ctx->screenX0;
        f32 dr = ctx->screenX1 - cx;
        f32 dt = cy;
        f32 db = SO2H_UI_DESIGN_H - cy;
        f32 m = dl;

        e = SO2H_UI_FROM_LEFT;
        if (dr < m) {
            m = dr;
            e = SO2H_UI_FROM_RIGHT;
        }
        if (dt < m) {
            m = dt;
            e = SO2H_UI_FROM_TOP;
        }
        if (db < m) {
            e = SO2H_UI_FROM_BOTTOM;
        }
    }

    switch (e) {
        case SO2H_UI_FROM_LEFT:
            *dx = -(r->x1 - ctx->screenX0);
            break;
        case SO2H_UI_FROM_RIGHT:
            *dx = ctx->screenX1 - r->x0;
            break;
        case SO2H_UI_FROM_TOP:
            *dy = -r->y1;
            break;
        case SO2H_UI_FROM_BOTTOM:
        default:
            *dy = SO2H_UI_DESIGN_H - r->y0;
            break;
    }
}

/**
 * SLIDE, FALL and POP are one displacement read through three curves: SLIDE eases in and out,
 * FALL accelerates and bounces once, POP finishes its travel early and spends the frames it has
 * left sailing past the resting place before snapping back onto it.
 */
static void So2h_UiAdvanceSlides(So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        const So2hUiDesc* d = &ctx->desc[i];
        So2hUiNode* n = &ctx->node[i];
        u8 rv = d->style.reveal;
        f32 dx;
        f32 dy;
        f32 t;
        f32 k;
        f32 over = 0.0f;

        if ((rv != SO2H_UI_REVEAL_SLIDE) && (rv != SO2H_UI_REVEAL_FALL) && (rv != SO2H_UI_REVEAL_POP)) {
            continue;
        }
        if (n->revealT >= 1.0f) {
            continue;
        }

        So2h_UiSlideEdge(ctx, i, &dx, &dy);
        t = n->revealT;

        if (rv == SO2H_UI_REVEAL_FALL) {
            k = 1.0f - So2h_Ui_FallEase(t);
        } else if (rv == SO2H_UI_REVEAL_POP) {
            f32 s = t / SO2H_UI_POP_SETTLE;

            if (s > 1.0f) {
                s = 1.0f;
            }
            k = 1.0f - So2h_Ui_SmoothStep(s);
            if (t > SO2H_UI_POP_SETTLE) {
                f32 u = (t - SO2H_UI_POP_SETTLE) / (1.0f - SO2H_UI_POP_SETTLE);

                over = -SO2H_UI_POP_OVER * So2h_UiPulse(u * u);
            }
        } else {
            k = 1.0f - So2h_Ui_SmoothStep(t);
        }

        if (over != 0.0f) {
            // The overshoot runs along the SAME axis the node entered on, so it is derived from
            // the travel vector and no node carries a direction of its own for it.
            f32 len = So2h_UiLen2(dx, dy);

            So2h_UiMoveSubtree(ctx, i, (dx * k) + (over * dx / len), (dy * k) + (over * dy / len));
        } else {
            So2h_UiMoveSubtree(ctx, i, dx * k, dy * k);
        }
    }
}

/**
 * GROW: the node starts as a short letterbox pinned at its OWN TOP EDGE and opens downward to
 * its solved height, scaling its whole subtree with it. A window animates its contents by
 * existing - nothing inside it declares anything.
 */
static void So2h_UiAdvanceGrows(So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        So2hUiNode* n = &ctx->node[i];
        f32 k;

        if (ctx->desc[i].style.reveal != SO2H_UI_REVEAL_GROW) {
            continue;
        }
        if (n->revealT >= 1.0f) {
            continue;
        }
        k = SO2H_UI_GROW_FROM + ((1.0f - SO2H_UI_GROW_FROM) * So2h_Ui_SmoothStep(n->revealT));
        So2h_UiStretchSubtree(ctx, i, 1.0f, k, n->rect.x0, n->rect.y0);
    }
}

/**
 * Drives REVEAL_SWAP nodes off the bottom of the screen and back.
 *
 * The displacement is DERIVED from the solved rect (how far this node's top is from the screen
 * bottom), never from a baked distance - so a node that comes back twice as tall is still
 * exactly hidden on the way out, and an aspect change needs no new numbers.
 */
static void So2h_UiAdvanceSwaps(So2hUiCtx* ctx) {
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        So2hUiNode* n = &ctx->node[i];

        if (ctx->desc[i].style.reveal != SO2H_UI_REVEAL_SWAP) {
            continue;
        }
        if (n->swapOut) {
            n->swapT -= 1.0f / (f32)SO2H_UI_MORPH_FRAMES;
            if (n->swapT <= 0.0f) {
                n->swapT = 0.0f;
                // Fully off-screen: the ONLY moment content is allowed to change.
                if (n->swapPending && (ctx->swapApply[i] != NULL)) {
                    ctx->swapApply[i](i, ctx->swapArg[i]);
                }
                n->swapPending = 0;
                n->swapOut = 0;
            }
        } else if (n->swapT < 1.0f) {
            // On the way IN a SWAP node is also part of the opening performance, so its rise
            // waits out the same delay + jitter every other window does. The entrance and the
            // content swap are the same move; there is no second mechanism.
            if (n->revealHold >= So2h_UiRevealDelay(ctx, i)) {
                n->swapT += 1.0f / (f32)SO2H_UI_MORPH_FRAMES;
                if (n->swapT > 1.0f) {
                    n->swapT = 1.0f;
                }
            }
        }
        if (n->swapT < 1.0f) {
            So2h_UiMoveSubtree(ctx, i, 0.0f,
                               (1.0f - So2h_Ui_SmoothStep(n->swapT)) * (SO2H_UI_DESIGN_H - n->rect.y0));
        }
    }
}

/**
 * The exit is NOT the entrance played backwards: everything gathers UPWARD for a few frames of
 * held breath, then the floor drops out and each node falls under gravity, stretching as it
 * picks up speed. Per-node desync means the menu comes apart rather than leaving as one sheet.
 */
static void So2h_UiAdvanceClose(So2hUiCtx* ctx) {
    s32 i;

    if (!ctx->closing) {
        return;
    }
    ctx->closeFrames++;

    for (i = 0; i < ctx->descCount; i++) {
        So2hUiNode* n = &ctx->node[i];
        s32 f;

        if (n->closeF < 0) {
            continue;
        }
        n->closeF++;
        f = n->closeF - So2h_UiCloseDelay(ctx, i);
        if (f <= 0) {
            continue;
        }
        if (f <= SO2H_UI_CLOSE_RISE_F) {
            // Build-up: rise, eased OUT, so it reads as being pulled up short.
            f32 u = So2h_Ui_SmoothStep((f32)f / (f32)SO2H_UI_CLOSE_RISE_F);

            // Same two audible beats as the entrance, reused: the node picks itself up, then
            // lets go. The "placed" thud lands on the release, which is the accent you feel.
            if (f == 1) {
                So2h_UiSfx_Slide(i, ctx->openSeed ^ 0x5BF03635u);
            } else if (f == SO2H_UI_CLOSE_RISE_F) {
                So2h_UiSfx_SlideStop(i);
                So2h_UiSfx_Placed(i, ctx->openSeed ^ 0x5BF03635u);
            }
            So2h_UiMoveSubtree(ctx, i, 0.0f, -SO2H_UI_CLOSE_RISE_U * u);
        } else {
            f32 g = (f32)(f - SO2H_UI_CLOSE_RISE_F);
            f32 dy = -SO2H_UI_CLOSE_RISE_U + (0.5f * SO2H_UI_CLOSE_GRAVITY * g * g);
            f32 v = SO2H_UI_CLOSE_GRAVITY * g;
            f32 sy = 1.0f + ((v * 0.03f < SO2H_UI_CLOSE_SQUASH) ? (v * 0.03f) : SO2H_UI_CLOSE_SQUASH);
            f32 sx = 1.0f / (1.0f + (SO2H_UI_CLOSE_PINCH * (sy - 1.0f)));
            f32 cx = (n->rect.x0 + n->rect.x1) * 0.5f;
            f32 cy = (n->rect.y0 + n->rect.y1) * 0.5f;

            So2h_UiStretchSubtree(ctx, i, sx, sy, cx, cy);
            So2h_UiMoveSubtree(ctx, i, 0.0f, dy);
        }
    }
}

void So2h_Ui_Open(void) {
    So2hUiCtx* ctx = So2h_UiCtx();
    s32 i;

    ctx->closing = 0;
    ctx->closeFrames = 0;
    ctx->openSeed = So2h_UiHash(ctx->openSeed + 0x2545F491u);
    if (ctx->openSeed == 0) {
        ctx->openSeed = 1;
    }
    for (i = 0; i < SO2H_UI_MAX_NODES; i++) {
        So2hUiNode* n = &ctx->node[i];

        n->revealT = 0.0f;
        n->revealHold = 0;
        n->closeF = -1;
        // A SWAP node opens the way it comes back from a swap: up off the bottom.
        n->swapT = 0.0f;
        n->swapOut = 0;
        n->swapPending = 0;
    }
}

void So2h_Ui_Close(void) {
    So2hUiCtx* ctx = So2h_UiCtx();
    s32 i;

    ctx->closing = 1;
    ctx->closeFrames = 0;
    for (i = 0; i < SO2H_UI_MAX_NODES; i++) {
        ctx->node[i].closeF = 0;
    }
}

s32 So2h_Ui_CloseDone(void) {
    So2hUiCtx* ctx = So2h_UiCtx();
    s32 i;

    if (!ctx->closing) {
        return 0;
    }
    for (i = 0; i < ctx->descCount; i++) {
        So2hUiNode* n = &ctx->node[i];
        f32 g;

        if (!n->visible) {
            continue;
        }
        g = (f32)(n->closeF - So2h_UiCloseDelay(ctx, i) - SO2H_UI_CLOSE_RISE_F);
        if (g < 0.0f) {
            return 0;
        }
        if ((0.5f * SO2H_UI_CLOSE_GRAVITY * g * g) < (SO2H_UI_DESIGN_H + 40.0f)) {
            return 0;
        }
    }
    return 1;
}

s32 So2h_Ui_Swap(s32 nodeId, So2hUiSwapFn apply, void* arg) {
    So2hUiCtx* ctx = So2h_UiCtx();
    So2hUiNode* n;

    if (!So2h_UiNode_IsValid((So2hUiId)nodeId)) {
        return 0;
    }
    if (ctx->desc[nodeId].style.reveal != SO2H_UI_REVEAL_SWAP) {
        // Not a swap node: the content change is not animatable, so just do it.
        if (apply != NULL) {
            apply(nodeId, arg);
        }
        return 0;
    }
    n = &ctx->node[nodeId];
    ctx->swapApply[nodeId] = apply;
    ctx->swapArg[nodeId] = arg;
    n->swapPending = 1;
    n->swapOut = 1;
    return 1;
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

    // Cheap no-op once the two custom samples are in the SYSTEM bank; retries until audio is up.
    So2h_UiSfx_Bind();
    So2h_UiAdvanceReveals(ctx);
    So2h_UiAdvanceSlides(ctx);
    So2h_UiAdvanceGrows(ctx);
    So2h_UiAdvanceSwaps(ctx);
    So2h_UiAdvanceClose(ctx);
}
