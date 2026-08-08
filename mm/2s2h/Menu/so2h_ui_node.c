/*
 * File: so2h_ui_node.c
 * Description: SO2H [Menu] so2h_ui node tree, registration and lifecycle.
 *
 * Owns the single static context, the descriptor->runtime mapping, and the small keyed side
 * tables (runs, scroll rows, pages). Nothing here computes geometry or emits graphics.
 */

#include "so2h_ui_internal.h"

#include <string.h>

static So2hUiCtx sCtx;

So2hUiCtx* So2h_UiCtx(void) {
    return &sCtx;
}

s32 So2h_UiNode_IsValid(So2hUiId id) {
    return (id != SO2H_UI_INVALID) && ((s32)id < sCtx.descCount);
}

const So2hUiDesc* So2h_UiNode_Desc(So2hUiId id) {
    if (!So2h_UiNode_IsValid(id)) {
        return NULL;
    }
    return &sCtx.desc[id];
}

/**
 * Side tables are keyed by node id rather than indexed by it, so a tree with two scroll rows
 * costs two entries and not SO2H_UI_MAX_NODES. Lookup is a short linear scan over a table
 * capped at 24 entries, which is cheaper than the cache miss a sparse array would cost.
 */
So2hUiRunState* So2h_UiNode_Run(So2hUiId id) {
    s32 i;
    s32 free = -1;

    for (i = 0; i < SO2H_UI_MAX_RUNS; i++) {
        if (sCtx.run[i].inUse && (sCtx.run[i].node == id)) {
            return &sCtx.run[i];
        }
        if (!sCtx.run[i].inUse && (free < 0)) {
            free = i;
        }
    }
    if (free < 0) {
        return NULL;
    }
    memset(&sCtx.run[free], 0, sizeof(So2hUiRunState));
    sCtx.run[free].node = id;
    sCtx.run[free].inUse = 1;

    return &sCtx.run[free];
}

So2hUiScrollState* So2h_UiNode_Scroll(So2hUiId id) {
    s32 i;
    s32 free = -1;

    for (i = 0; i < SO2H_UI_MAX_SCROLL_ROWS; i++) {
        if (sCtx.scroll[i].inUse && (sCtx.scroll[i].node == id)) {
            return &sCtx.scroll[i];
        }
        if (!sCtx.scroll[i].inUse && (free < 0)) {
            free = i;
        }
    }
    if (free < 0) {
        return NULL;
    }
    memset(&sCtx.scroll[free], 0, sizeof(So2hUiScrollState));
    sCtx.scroll[free].node = id;
    sCtx.scroll[free].inUse = 1;

    return &sCtx.scroll[free];
}

So2hUiPageState* So2h_UiNode_Page(So2hUiId id) {
    s32 i;
    s32 free = -1;

    for (i = 0; i < SO2H_UI_MAX_PAGES; i++) {
        if (sCtx.page[i].inUse && (sCtx.page[i].group == id)) {
            return &sCtx.page[i];
        }
        if (!sCtx.page[i].inUse && (free < 0)) {
            free = i;
        }
    }
    if (free < 0) {
        return NULL;
    }
    memset(&sCtx.page[free], 0, sizeof(So2hUiPageState));
    sCtx.page[free].group = id;
    sCtx.page[free].active = SO2H_UI_INVALID;
    sCtx.page[free].cursor = SO2H_UI_INVALID;
    sCtx.page[free].inUse = 1;

    return &sCtx.page[free];
}

/**
 * A node's state is its descriptor's, unless a runtime override has been set (save-file
 * conditions greying out content), with two extra rules folded in:
 *
 *   - a HIDDEN page becomes DISABLED, not visible, when the debug reveal is on
 *   - anything whose ancestor is HIDDEN is HIDDEN, resolved during the layout walk
 */
u8 So2h_UiNode_ResolvedState(So2hUiId id) {
    const So2hUiDesc* d = So2h_UiNode_Desc(id);
    u8 state;

    if (d == NULL) {
        return SO2H_UI_HIDDEN;
    }
    state = (sCtx.stateOverride[id] != 0xFF) ? sCtx.stateOverride[id] : d->state;

    if ((state == SO2H_UI_HIDDEN) && (d->kind == SO2H_UI_PAGE) && sCtx.showHiddenPages) {
        state = SO2H_UI_DISABLED;
    }
    return state;
}

void So2h_Ui_Init(const So2hUiDesc* table, s32 count, const So2hUiVariant* variants, s32 variantCount) {
    s32 i;

    if ((table == NULL) || (count <= 0)) {
        return;
    }
    if (count > SO2H_UI_MAX_NODES) {
        count = SO2H_UI_MAX_NODES;
    }

    sCtx.desc = table;
    sCtx.descCount = count;
    sCtx.variants = variants;
    sCtx.variantCount = (variants != NULL) ? variantCount : 0;

    // Parents-first is a hard requirement of the single-pass solver. A row that violates it
    // is dropped to HIDDEN rather than being allowed to read an unsolved parent rect.
    // Registering a scene drops every content binding: the ids in the new table mean
    // something different, so a stale callback would paint the wrong panel rather than
    // simply not paint. Re-binding is the caller's job, immediately after this returns.
    for (i = 0; i < SO2H_UI_MAX_NODES; i++) {
        sCtx.drawFn[i] = NULL;
        sCtx.drawArg[i] = NULL;
        sCtx.cellFn[i] = NULL;
    }

    for (i = 0; i < count; i++) {
        sCtx.stateOverride[i] = 0xFF;

        if (table[i].id != (So2hUiId)i) {
            sCtx.stateOverride[i] = SO2H_UI_HIDDEN;
            continue;
        }
        if ((i > 0) && ((table[i].parent == SO2H_UI_INVALID) || (table[i].parent >= (So2hUiId)i))) {
            sCtx.stateOverride[i] = SO2H_UI_HIDDEN;
        }
    }

    // Seed each PAGEGROUP with its first non-hidden page.
    for (i = 0; i < count; i++) {
        if (table[i].kind == SO2H_UI_PAGEGROUP) {
            So2hUiPageState* ps = So2h_UiNode_Page(table[i].id);
            s32 j;

            if (ps == NULL) {
                continue;
            }
            for (j = 0; j < count; j++) {
                if ((table[j].kind == SO2H_UI_PAGE) && (table[j].parent == table[i].id) &&
                    (So2h_UiNode_ResolvedState(table[j].id) != SO2H_UI_HIDDEN)) {
                    ps->active = table[j].id;
                    break;
                }
            }
        }
    }

    sCtx.initialised = 1;
    So2h_Ui_Reset();
}

void So2h_Ui_Reset(void) {
    s32 i;

    for (i = 0; i < SO2H_UI_MAX_RUNS; i++) {
        sCtx.run[i].seeded = 0;
    }
    for (i = 0; i < SO2H_UI_MAX_SCROLL_ROWS; i++) {
        sCtx.scroll[i].seeded = 0;
        sCtx.scroll[i].offset = 0.0f;
        sCtx.scroll[i].target = 0.0f;
        sCtx.scroll[i].index = 0;
    }
    for (i = 0; i < SO2H_UI_MAX_NODES; i++) {
        sCtx.node[i].revealT = 0.0f;
    }
    sCtx.morphFrames = 0;
    So2h_UiNav_Reset();
}

void So2h_Ui_Update(void) {
    if (!sCtx.initialised) {
        return;
    }
    So2h_UiLayout_Update();
    So2h_UiNav_Validate();
}

Gfx* So2h_Ui_Draw(Gfx* gfx) {
    if (!sCtx.initialised) {
        return gfx;
    }
    return So2h_UiDraw_Tree(gfx);
}

const So2hUiNode* So2h_Ui_GetNode(So2hUiId id) {
    if (!So2h_UiNode_IsValid(id)) {
        return NULL;
    }
    return &sCtx.node[id];
}

const So2hUiRect* So2h_Ui_GetRect(So2hUiId id) {
    if (!So2h_UiNode_IsValid(id)) {
        return NULL;
    }
    return &sCtx.node[id].rect;
}

f32 So2h_Ui_TilePx(void) {
    return sCtx.tile;
}

f32 So2h_Ui_MorphT(void) {
    return sCtx.morphT;
}

void So2h_Ui_SetState(So2hUiId id, So2hUiState state) {
    if (!So2h_UiNode_IsValid(id)) {
        return;
    }
    sCtx.stateOverride[id] = (u8)state;
}

void So2h_Ui_SetActivePage(So2hUiId pageGroup, So2hUiId page) {
    So2hUiPageState* ps;

    if (!So2h_UiNode_IsValid(pageGroup) || !So2h_UiNode_IsValid(page)) {
        return;
    }
    ps = So2h_UiNode_Page(pageGroup);
    if (ps == NULL) {
        return;
    }
    if (So2h_UiNode_ResolvedState(page) == SO2H_UI_HIDDEN) {
        return;
    }
    // Remember where the cursor was on the page being left, so returning restores it.
    ps->cursor = sCtx.focus;
    ps->active = page;
}

So2hUiId So2h_Ui_GetActivePage(So2hUiId pageGroup) {
    So2hUiPageState* ps = So2h_UiNode_Page(pageGroup);

    return (ps != NULL) ? ps->active : SO2H_UI_INVALID;
}

void So2h_Ui_SetShowHiddenPages(s32 show) {
    sCtx.showHiddenPages = show;
}

s32 So2h_Ui_GetShowHiddenPages(void) {
    return sCtx.showHiddenPages;
}

// ---------------------------------------------------------------------------------------
// Content bindings. See So2h_Ui_BindDraw in so2h_ui.h for why these are attached at runtime
// instead of being fields the generator fills in.
// ---------------------------------------------------------------------------------------
void So2h_Ui_BindDraw(So2hUiId id, So2hUiDrawFn fn, void* user) {
    if (!So2h_UiNode_IsValid(id)) {
        return;
    }
    sCtx.drawFn[id] = fn;
    sCtx.drawArg[id] = user;
}

void So2h_Ui_BindCellState(So2hUiId id, So2hUiCellStateFn fn) {
    if (!So2h_UiNode_IsValid(id)) {
        return;
    }
    sCtx.cellFn[id] = fn;
}

void So2h_Ui_ScrollTo(So2hUiId row, s16 index) {
    const So2hUiDesc* d = So2h_UiNode_Desc(row);
    So2hUiScrollState* st;

    if (d == NULL) {
        return;
    }
    st = So2h_UiNode_Scroll(row);
    if (st == NULL) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if ((d->count > 0) && (index > (d->count - 1))) {
        index = d->count - 1;
    }
    st->index = index;
}

s16 So2h_Ui_GetScrollIndex(So2hUiId row) {
    So2hUiScrollState* st = So2h_UiNode_Scroll(row);

    return (st != NULL) ? st->index : 0;
}

/**
 * The one easing curve. Lifted unchanged from so2h_quest_layout.c so the new solver morphs
 * with exactly the feel the bar already has, and reused for reveals and scrolling so the
 * whole menu moves on one curve.
 */
f32 So2h_Ui_SmoothStep(f32 t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    return t * t * (3.0f - (2.0f * t));
}

/**
 * Gravity, not easing: accelerate the whole way down, land, then one small bounce. REVEAL_FALL
 * reads this instead of SmoothStep, which is the entire difference between a panel that glides
 * into place and an object that drops into it.
 */
f32 So2h_Ui_FallEase(f32 t) {
    f32 u;

    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    if (t <= 0.82f) {
        u = t / 0.82f;
        return u * u;
    }
    u = (t - 0.82f) / 0.18f;
    return 1.0f - (0.07f * (4.0f * u * (1.0f - u)));
}

/**
 * Deliberately NOT rand(). The solver may run more than once for a single displayed frame, and
 * a node's jitter must be a pure function of (node id, open seed) so it cannot change halfway
 * through its own entrance.
 */
u32 So2h_UiHash(u32 x) {
    x *= 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return x;
}
