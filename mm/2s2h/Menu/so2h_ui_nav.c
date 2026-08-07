/*
 * File: so2h_ui_nav.c
 * Description: SO2H [Menu] so2h_ui focus graph and cursor movement.
 *
 * ---------------------------------------------------------------------------------------
 * THE FOCUS MODEL
 * ---------------------------------------------------------------------------------------
 * Focus is a pair: a node, and an index within it.
 *
 * A plain CELL or CUSTOM node is focused with index 0 and that is the whole story. A
 * container whose layout is GRID, SCROLL_ROW or RUN is *itself* the focused node, and the
 * index says which of its virtual cells the cursor is on. That is deliberate: a 4 x 5
 * equipment grid is one descriptor row, not twenty, so it must also be one focus target with
 * an index rather than twenty nodes the nav graph has to walk.
 *
 * The index lives in the scroll side-table, which is keyed by node id and therefore already
 * gives every container its own remembered position for free. Leaving a row and coming back
 * puts the cursor exactly where it was - the requirement that boots, tunics and swords each
 * keep their own offset falls straight out of this rather than needing a mechanism.
 *
 * ---------------------------------------------------------------------------------------
 * HOW A DIRECTION IS RESOLVED
 * ---------------------------------------------------------------------------------------
 *   1. An explicit navUp / navDown / navLeft / navRight in the descriptor wins outright.
 *   2. Otherwise the move is tried *inside* the focused container first - across a grid row,
 *      along a scroll row, down a grid column.
 *   3. Only when the move falls off the container's own edge does it become a search for
 *      another node, scored geometrically from the focused cell's rect.
 *   4. If that search finds nothing, So2h_Ui_Navigate returns 0.
 *
 * Step 4 is the boundary contract. Zero means "the cursor walked off the edge of my tree,
 * you deal with it" - which is exactly the protocol So2h_QuestBar_UpdateCursor already has
 * with z_kaleido_scope_NES.c:4796, so porting the bar onto this API does not change how it
 * hands control back to vanilla.
 *
 * ---------------------------------------------------------------------------------------
 * SCROLL ROWS DO NOT WRAP
 * ---------------------------------------------------------------------------------------
 * L/R inside a SCROLL_ROW moves the index and lets the solver pan the row; the cursor parks
 * in place rather than riding the content off screen. At either end the move is refused - it
 * does not wrap, and it does not jump to the neighbouring row either, because a row of thirty
 * items silently teleporting you into the next category on the thirty-first press is the kind
 * of thing that feels broken long before anyone can explain why. U/D is what changes
 * category, and it restores that row's own remembered index.
 */

#include "so2h_ui_internal.h"

// Perpendicular drift is weighted against travel in the requested direction, so a candidate
// that is slightly further away but squarely in line beats one that is closer but off to the
// side. Anything above about 2.0 makes diagonal layouts feel sticky.
#define SO2H_UI_NAV_PERP_WEIGHT 2.0f

// A candidate must clear the current rect by at least this much in the requested direction to
// count as being "that way" at all. Keeps overlapping panels from focusing each other.
#define SO2H_UI_NAV_MIN_TRAVEL 0.5f

static f32 So2h_UiNavAbs(f32 v) {
    return (v < 0.0f) ? -v : v;
}

static s32 So2h_UiNavIsContainer(const So2hUiDesc* d) {
    return (d != NULL) && ((d->layout == SO2H_UI_LAYOUT_GRID) || (d->layout == SO2H_UI_LAYOUT_SCROLL_ROW) ||
                           (d->layout == SO2H_UI_LAYOUT_SCROLL_COL) || (d->layout == SO2H_UI_LAYOUT_RUN));
}

/**
 * Number of virtual cells a container offers. GRID counts its whole grid, SCROLL_ROW counts
 * every item including the disabled ones (a mostly-empty row still reserves its slots), RUN
 * counts however many tiles the solver settled on this frame.
 */
static s32 So2h_UiNavCellCount(So2hUiId id) {
    const So2hUiDesc* d = So2h_UiNode_Desc(id);

    if (d == NULL) {
        return 0;
    }
    if (d->layout == SO2H_UI_LAYOUT_GRID) {
        return (s32)d->cols * (s32)d->rows;
    }
    if ((d->layout == SO2H_UI_LAYOUT_SCROLL_ROW) || (d->layout == SO2H_UI_LAYOUT_SCROLL_COL)) {
        return d->count;
    }
    if (d->layout == SO2H_UI_LAYOUT_RUN) {
        So2hUiRunState* rs = So2h_UiNode_Run(id);

        return (rs != NULL) ? rs->count : 0;
    }
    return 0;
}

/**
 * The index side-table. Containers of every layout share the scroll state purely as an index
 * holder, which is why a grid remembers its cursor as reliably as a scroll row does.
 */
static s16 So2h_UiNavIndex(So2hUiId id) {
    So2hUiScrollState* st = So2h_UiNode_Scroll(id);

    return (st != NULL) ? st->index : 0;
}

static void So2h_UiNavSetIndex(So2hUiId id, s16 index) {
    So2hUiScrollState* st = So2h_UiNode_Scroll(id);
    s32 count = So2h_UiNavCellCount(id);

    if (st == NULL) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if ((count > 0) && (index > (s16)(count - 1))) {
        index = (s16)(count - 1);
    }
    st->index = index;
}

/**
 * Whether a container cell can actually take the cursor. A cellState predicate is how a grid
 * that is large and mostly disabled costs one descriptor row and a function instead of one
 * row per slot.
 */
static s32 So2h_UiNavCellEnabled(So2hUiId id, s16 index) {
    const So2hUiDesc* d = So2h_UiNode_Desc(id);
    s32 count = So2h_UiNavCellCount(id);

    if ((d == NULL) || (index < 0) || (index >= (s16)count)) {
        return 0;
    }
    if (d->cellState == NULL) {
        return 1;
    }
    return d->cellState(id, index) == SO2H_UI_ENABLED;
}

/**
 * Rect the cursor is actually sitting on: the container's focused cell, or the node itself.
 */
static void So2h_UiNavFocusRect(So2hUiId id, So2hUiRect* out) {
    const So2hUiDesc* d = So2h_UiNode_Desc(id);
    const So2hUiRect* r;

    out->x0 = out->y0 = out->x1 = out->y1 = 0.0f;
    if (d == NULL) {
        return;
    }
    if (So2h_UiNavIsContainer(d)) {
        So2h_Ui_GetCellRect(id, So2h_UiNavIndex(id), out);
        if ((out->x1 - out->x0) > 0.0f) {
            return;
        }
        // A container with no solved cell this frame still has a rect of its own to move from.
    }
    r = So2h_Ui_GetRect(id);
    if (r != NULL) {
        *out = *r;
    }
}

static s32 So2h_UiNavFocusable(So2hUiId id) {
    const So2hUiNode* n = So2h_Ui_GetNode(id);

    return (n != NULL) && n->visible && n->focusable;
}

/**
 * First cell of a container that will accept focus, scanning from `from` in `step`. Returns
 * -1 when the container has nothing focusable at all, which is how a fully-disabled category
 * gets skipped over rather than swallowing the cursor.
 */
static s32 So2h_UiNavFirstEnabled(So2hUiId id, s32 from, s32 step) {
    s32 count = So2h_UiNavCellCount(id);
    s32 i;

    if (count <= 0) {
        return -1;
    }
    if (from < 0) {
        from = 0;
    }
    if (from >= count) {
        from = count - 1;
    }
    for (i = from; (i >= 0) && (i < count); i += step) {
        if (So2h_UiNavCellEnabled(id, (s16)i)) {
            return i;
        }
    }
    // Nothing in that direction; take anything.
    for (i = 0; i < count; i++) {
        if (So2h_UiNavCellEnabled(id, (s16)i)) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------
void So2h_UiNav_Reset(void) {
    So2h_UiCtx()->focus = SO2H_UI_INVALID;
}

/**
 * Called every frame after the solve. Focus can be invalidated by anything that changes the
 * tree - a page switch, a save-file condition disabling a row, the debug reveal being turned
 * off - so rather than every one of those having to fix the cursor up, the cursor is simply
 * re-checked once, here.
 */
void So2h_UiNav_Validate(void) {
    So2hUiCtx* ctx = So2h_UiCtx();
    s32 i;

    if (So2h_UiNavFocusable(ctx->focus)) {
        const So2hUiDesc* d = So2h_UiNode_Desc(ctx->focus);

        if (So2h_UiNavIsContainer(d)) {
            s32 idx = So2h_UiNavIndex(ctx->focus);

            if (!So2h_UiNavCellEnabled(ctx->focus, (s16)idx)) {
                idx = So2h_UiNavFirstEnabled(ctx->focus, idx, 1);
                if (idx >= 0) {
                    So2h_UiNavSetIndex(ctx->focus, (s16)idx);
                    return;
                }
            } else {
                return;
            }
        } else {
            return;
        }
    }

    // Fall back to the first thing that will take it, in table order. Table order is the
    // author's order, so this lands somewhere deliberate rather than somewhere arbitrary.
    for (i = 0; i < ctx->descCount; i++) {
        if (!So2h_UiNavFocusable((So2hUiId)i)) {
            continue;
        }
        if (So2h_UiNavIsContainer(So2h_UiNode_Desc((So2hUiId)i))) {
            s32 idx = So2h_UiNavFirstEnabled((So2hUiId)i, 0, 1);

            if (idx < 0) {
                continue;
            }
            So2h_UiNavSetIndex((So2hUiId)i, (s16)idx);
        }
        ctx->focus = (So2hUiId)i;
        return;
    }

    ctx->focus = SO2H_UI_INVALID;
}

// ---------------------------------------------------------------------------------------
// Public focus
// ---------------------------------------------------------------------------------------
void So2h_Ui_SetFocus(So2hUiId id) {
    So2hUiCtx* ctx = So2h_UiCtx();

    if (!So2h_UiNavFocusable(id)) {
        return;
    }
    if (So2h_UiNavIsContainer(So2h_UiNode_Desc(id))) {
        s32 idx = So2h_UiNavFirstEnabled(id, So2h_UiNavIndex(id), 1);

        if (idx >= 0) {
            So2h_UiNavSetIndex(id, (s16)idx);
        }
    }
    ctx->focus = id;
}

So2hUiId So2h_Ui_GetFocus(void) {
    return So2h_UiCtx()->focus;
}

s16 So2h_Ui_GetFocusIndex(void) {
    So2hUiId f = So2h_UiCtx()->focus;

    if (!So2h_UiNode_IsValid(f)) {
        return 0;
    }
    return So2h_UiNavIsContainer(So2h_UiNode_Desc(f)) ? So2h_UiNavIndex(f) : 0;
}

// ---------------------------------------------------------------------------------------
// Movement inside a container
// ---------------------------------------------------------------------------------------

/**
 * Returns 1 when the move stayed inside the container, 0 when it fell off an edge and the
 * caller should search the rest of the tree.
 */
static s32 So2h_UiNavMoveInside(So2hUiId id, So2hUiDir dir) {
    const So2hUiDesc* d = So2h_UiNode_Desc(id);
    s32 count = So2h_UiNavCellCount(id);
    s32 idx = So2h_UiNavIndex(id);
    s32 next;
    s32 step;

    if ((d == NULL) || (count <= 0)) {
        return 0;
    }

    if (d->layout == SO2H_UI_LAYOUT_GRID) {
        s32 cols = (d->cols > 0) ? d->cols : 1;
        s32 col = idx % cols;
        s32 row = idx / cols;
        s32 rows = (d->rows > 0) ? d->rows : 1;

        switch (dir) {
            case SO2H_UI_LEFT:
                col--;
                break;
            case SO2H_UI_RIGHT:
                col++;
                break;
            case SO2H_UI_UP:
                row--;
                break;
            case SO2H_UI_DOWN:
            default:
                row++;
                break;
        }
        if ((col < 0) || (col >= cols) || (row < 0) || (row >= rows)) {
            return 0;
        }
        next = (row * cols) + col;

        // Skip disabled cells in the direction of travel, so a sparse grid still walks
        // naturally instead of stopping on every hole.
        step = ((dir == SO2H_UI_LEFT) || (dir == SO2H_UI_UP)) ? -1 : 1;
        while ((next >= 0) && (next < count) && !So2h_UiNavCellEnabled(id, (s16)next)) {
            s32 nCol = next % cols;

            if ((dir == SO2H_UI_LEFT) || (dir == SO2H_UI_RIGHT)) {
                nCol += step;
                if ((nCol < 0) || (nCol >= cols)) {
                    return 0;
                }
                next += step;
            } else {
                next += step * cols;
            }
        }
        if ((next < 0) || (next >= count) || !So2h_UiNavCellEnabled(id, (s16)next)) {
            return 0;
        }
        So2h_UiNavSetIndex(id, (s16)next);
        return 1;
    }

    if ((d->layout == SO2H_UI_LAYOUT_SCROLL_ROW) || (d->layout == SO2H_UI_LAYOUT_SCROLL_COL) ||
        (d->layout == SO2H_UI_LAYOUT_RUN)) {
        s32 vertical = (d->layout == SO2H_UI_LAYOUT_SCROLL_COL);

        // The strip consumes the axis it scrolls along and hands back the other one, so
        // leaving a strip is always the perpendicular direction and never needs a wrap rule.
        if (vertical != ((dir == SO2H_UI_UP) || (dir == SO2H_UI_DOWN))) {
            return 0;
        }
        if (vertical) {
            step = (dir == SO2H_UI_DOWN) ? 1 : -1;
        } else {
            step = (dir == SO2H_UI_RIGHT) ? 1 : -1;
        }
        next = idx + step;
        while ((next >= 0) && (next < count) && !So2h_UiNavCellEnabled(id, (s16)next)) {
            next += step;
        }
        if ((next < 0) || (next >= count)) {
            return 0; // edge stop, no wrap
        }
        So2h_UiNavSetIndex(id, (s16)next);
        return 1;
    }

    return 0;
}

// ---------------------------------------------------------------------------------------
// Geometric search
// ---------------------------------------------------------------------------------------
static s32 So2h_UiNavScore(const So2hUiRect* from, const So2hUiRect* to, So2hUiDir dir, f32* outScore) {
    f32 fx = (from->x0 + from->x1) * 0.5f;
    f32 fy = (from->y0 + from->y1) * 0.5f;
    f32 tx = (to->x0 + to->x1) * 0.5f;
    f32 ty = (to->y0 + to->y1) * 0.5f;
    f32 travel;
    f32 perp;

    switch (dir) {
        case SO2H_UI_LEFT:
            travel = from->x0 - to->x1;
            perp = So2h_UiNavAbs(ty - fy);
            break;
        case SO2H_UI_RIGHT:
            travel = to->x0 - from->x1;
            perp = So2h_UiNavAbs(ty - fy);
            break;
        case SO2H_UI_UP:
            travel = from->y0 - to->y1;
            perp = So2h_UiNavAbs(tx - fx);
            break;
        case SO2H_UI_DOWN:
        default:
            travel = to->y0 - from->y1;
            perp = So2h_UiNavAbs(tx - fx);
            break;
    }

    if (travel < SO2H_UI_NAV_MIN_TRAVEL) {
        return 0;
    }
    *outScore = travel + (perp * SO2H_UI_NAV_PERP_WEIGHT);

    return 1;
}

/**
 * Best node in `dir` from `fromRect`, excluding `self`. Containers are entered at whichever
 * of their cells is closest to where the cursor came from, except for SCROLL_ROWs, which
 * restore their own remembered index instead - arriving in a category should put you back
 * where you left it, not wherever you happened to be in the row above.
 */
static So2hUiId So2h_UiNavSearch(So2hUiId self, const So2hUiRect* fromRect, So2hUiDir dir) {
    So2hUiCtx* ctx = So2h_UiCtx();
    So2hUiId best = SO2H_UI_INVALID;
    f32 bestScore = 0.0f;
    s32 bestCell = -1;
    s32 i;

    for (i = 0; i < ctx->descCount; i++) {
        So2hUiId id = (So2hUiId)i;
        const So2hUiDesc* d = So2h_UiNode_Desc(id);
        f32 score;

        if ((id == self) || !So2h_UiNavFocusable(id)) {
            continue;
        }

        if (So2h_UiNavIsContainer(d)) {
            s32 count = So2h_UiNavCellCount(id);
            s32 remembered = ((d->layout == SO2H_UI_LAYOUT_SCROLL_ROW) ||
                              (d->layout == SO2H_UI_LAYOUT_SCROLL_COL))
                                 ? So2h_UiNavIndex(id)
                                 : -1;
            s32 c;

            for (c = 0; c < count; c++) {
                So2hUiRect cell;

                if (!So2h_UiNavCellEnabled(id, (s16)c)) {
                    continue;
                }
                // A scroll row is entered at its remembered cell; the geometric score for the
                // row as a whole is still taken from that cell, so a row parked off to one
                // side does not out-compete a nearer one.
                if ((remembered >= 0) && (c != remembered)) {
                    continue;
                }
                So2h_Ui_GetCellRect(id, (s16)c, &cell);
                if ((cell.x1 - cell.x0) <= 0.0f) {
                    continue;
                }
                if (!So2h_UiNavScore(fromRect, &cell, dir, &score)) {
                    continue;
                }
                if ((best == SO2H_UI_INVALID) || (score < bestScore)) {
                    best = id;
                    bestScore = score;
                    bestCell = c;
                }
            }
            continue;
        }

        {
            const So2hUiRect* r = So2h_Ui_GetRect(id);

            if ((r == NULL) || !So2h_UiNavScore(fromRect, r, dir, &score)) {
                continue;
            }
            if ((best == SO2H_UI_INVALID) || (score < bestScore)) {
                best = id;
                bestScore = score;
                bestCell = -1;
            }
        }
    }

    if ((best != SO2H_UI_INVALID) && (bestCell >= 0)) {
        So2h_UiNavSetIndex(best, (s16)bestCell);
    }
    return best;
}

// ---------------------------------------------------------------------------------------
// The move
// ---------------------------------------------------------------------------------------
s32 So2h_Ui_Navigate(So2hUiDir dir) {
    So2hUiCtx* ctx = So2h_UiCtx();
    const So2hUiDesc* d;
    So2hUiRect fromRect;
    So2hUiId explicitTarget = SO2H_UI_INVALID;
    So2hUiId target;

    if (!ctx->initialised) {
        return 0;
    }

    // Nothing focused yet: the first press claims the cursor rather than moving it, which is
    // what makes entering the menu from vanilla feel like one press instead of two.
    if (!So2h_UiNavFocusable(ctx->focus)) {
        So2h_UiNav_Validate();
        return So2h_UiNavFocusable(ctx->focus);
    }

    d = So2h_UiNode_Desc(ctx->focus);
    if (d == NULL) {
        return 0;
    }

    switch (dir) {
        case SO2H_UI_UP:
            explicitTarget = d->navUp;
            break;
        case SO2H_UI_DOWN:
            explicitTarget = d->navDown;
            break;
        case SO2H_UI_LEFT:
            explicitTarget = d->navLeft;
            break;
        case SO2H_UI_RIGHT:
        default:
            explicitTarget = d->navRight;
            break;
    }

    if ((explicitTarget != SO2H_UI_INVALID) && So2h_UiNavFocusable(explicitTarget)) {
        So2h_Ui_SetFocus(explicitTarget);
        return 1;
    }

    if (So2h_UiNavIsContainer(d) && So2h_UiNavMoveInside(ctx->focus, dir)) {
        return 1;
    }

    So2h_UiNavFocusRect(ctx->focus, &fromRect);
    target = So2h_UiNavSearch(ctx->focus, &fromRect, dir);
    if (target == SO2H_UI_INVALID) {
        return 0; // off the edge of the tree - hand back to vanilla
    }

    ctx->focus = target;
    return 1;
}

// ---------------------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------------------
s32 So2h_Ui_HitTest(f32 x, f32 y, So2hUiId* outId) {
    So2hUiCtx* ctx = So2h_UiCtx();
    s32 i;

    // Reverse table order, so a node authored later - and therefore drawn over the top -
    // wins the hit rather than the one underneath it.
    for (i = ctx->descCount - 1; i >= 0; i--) {
        So2hUiId id = (So2hUiId)i;
        const So2hUiDesc* d = So2h_UiNode_Desc(id);
        const So2hUiNode* n = So2h_Ui_GetNode(id);
        const So2hUiRect* r;

        if (!So2h_UiNavFocusable(id) || (n == NULL)) {
            continue;
        }
        // The clip rect is part of the target: half a cell scrolled out of its window is not
        // clickable on the half that is not there.
        if ((x < n->clipRect.x0) || (x > n->clipRect.x1) || (y < n->clipRect.y0) || (y > n->clipRect.y1)) {
            continue;
        }

        if (So2h_UiNavIsContainer(d)) {
            s32 count = So2h_UiNavCellCount(id);
            s32 c;

            for (c = 0; c < count; c++) {
                So2hUiRect cell;

                if (!So2h_UiNavCellEnabled(id, (s16)c)) {
                    continue;
                }
                So2h_Ui_GetCellRect(id, (s16)c, &cell);
                if ((x >= cell.x0) && (x <= cell.x1) && (y >= cell.y0) && (y <= cell.y1)) {
                    So2h_UiNavSetIndex(id, (s16)c);
                    if (outId != NULL) {
                        *outId = id;
                    }
                    return 1;
                }
            }
            continue;
        }

        r = &n->rect;
        if ((x >= r->x0) && (x <= r->x1) && (y >= r->y0) && (y <= r->y1)) {
            if (outId != NULL) {
                *outId = id;
            }
            return 1;
        }
    }

    if (outId != NULL) {
        *outId = SO2H_UI_INVALID;
    }
    return 0;
}
