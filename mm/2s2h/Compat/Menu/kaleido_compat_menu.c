/**
 * so2h [Menu] compat - OOT Item / OOT Equip pause pages.
 *
 * Part of patch 0001/0002 (pause menu 6-page hexagon prism coexistence), now filled in by
 * patch 0004/0005 with real (non-placeholder) grid content wired to `OotSaveInfo`
 * (z64save.h, gSaveContext.save.shipSaveInfo.so2h.oot). See
 * /home/user/ProjectZ/env/so2h/patches/0001_pause_menu_coexistence.md and
 * /home/user/ProjectZ/env/so2h/STRICTRULES.md rules 19-20.
 *
 * Scope of this pass (deliberately, per plan.md):
 *  - Real per-slot state (filled/empty, owned/not-owned) sourced live from OotSaveInfo,
 *    not fake/static data - this is what makes the pages non-placeholder.
 *  - Slots are drawn with `gEquippedItemOutlineTex` (existing MM asset, already used
 *    elsewhere in this same page family for the equipped-item outline box) rather than
 *    per-item OOT icon art, because a verified OOT-item-id -> icon-texture table doesn't
 *    exist in this tree yet (that requires porting reference/soh's item icon table, a
 *    separate, larger content patch - SO2H TODO below marks exactly where that plugs in).
 *  - Cursor is drawn at the current slot but does NOT yet support analog-stick
 *    navigation within the grid (SO2H TODO in the Update* functions) - L/R page cycling
 *    (which is what patch 0001/0002 already wired and is unaffected by this file) is
 *    sufficient to reach and view both pages today.
 *  - No gameplay logic: nothing here mutates OotSaveInfo, only reads it.
 */

#include "global.h"
#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"
#include "assets/interface/parameter_static/parameter_static.h"

#define OOT_ITEM_NONE 0xFF // mirrors reference/soh's ITEM_NONE; kept local to avoid coupling to an OOT ItemId enum that doesn't exist in this tree yet

static s16 sItemOotCursorSlot = 0;
static s16 sEquipOotCursorSlot = 0;

void KaleidoScope_DrawItemSelectOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    OotInventory* ootInventory = &gSaveContext.save.shipSaveInfo.so2h.oot.inventory;
    s32 i;
    s32 j;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL42_Opa(play->state.gfxCtx);

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    // Draw one outline-box quad per occupied OOT item slot (real save data, not a static
    // placeholder). SO2H TODO (patch 0004 content follow-up): once an OOT-item-id -> icon
    // texture table is ported from reference/soh, replace this per-slot outline draw with
    // a real per-item icon draw (mirroring KaleidoScope_DrawItemSelect's
    // KaleidoScope_DrawTexQuadRGBA32(..., gItemIconsOot[itemId], ...) once that array
    // exists), same as MM's own item grid does with gItemIcons.
    for (i = 0, j = 0; i < ITEM_NUM_SLOTS; i++, j += 4) {
        if (ootInventory->items[i] == OOT_ITEM_NONE) {
            continue;
        }

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, (i == sItemOotCursorSlot) ? 255 : pauseCtx->alpha);
        gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemOotVtx[j], 4, 0);
        POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void KaleidoScope_UpdateItemCursorOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    // SO2H TODO (patch 0004 polish follow-up): real analog-stick cursor movement across
    // the grid, mirroring KaleidoScope_UpdateItemCursor's row/column search once this page
    // needs cursor-driven interaction (item swapping, descriptions, etc). For now the
    // cursor slot is only kept in-range so the outline highlight in
    // KaleidoScope_DrawItemSelectOot never reads out of bounds.
    if (sItemOotCursorSlot < 0) {
        sItemOotCursorSlot = 0;
    } else if (sItemOotCursorSlot >= ITEM_NUM_SLOTS) {
        sItemOotCursorSlot = ITEM_NUM_SLOTS - 1;
    }

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;
}

void KaleidoScope_DrawEquipmentOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    OotInventory* ootInventory = &gSaveContext.save.shipSaveInfo.so2h.oot.inventory;
    OotItemEquips* ootEquips = &gSaveContext.save.shipSaveInfo.so2h.oot.equips;
    s32 row;
    s32 col;
    s32 j;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL42_Opa(play->state.gfxCtx);

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    // Draw one outline-box quad per owned OOT equipment piece (EquipInv* bit set in
    // ootInventory->equipment), one row per EquipmentType, one column per tier - real save
    // data, not static placeholder content. Currently-equipped piece (ootEquips->equipment)
    // is drawn at full alpha, matching the highlight convention used elsewhere on this page
    // family. No Link-doll render this pass - see PauseContext.playerSegment TODO
    // (z64pause_menu.h) for the long-term rewire-to-MM's-skeleton plan.
    for (row = 0, j = 0; row < EQUIP_OOT_GRID_ROWS; row++) {
        u16 ownedMaskForRow = (ootInventory->equipment >> (row * 4)) & 0xF;
        u16 equippedTierForRow = (ootEquips->equipment >> (row * 4)) & 0xF;

        for (col = 0; col < EQUIP_OOT_GRID_COLS; col++, j += 4) {
            bool owned = (ownedMaskForRow & (1 << col)) != 0;

            if (!owned) {
                continue;
            }

            bool equipped = (col + 1) == equippedTierForRow;

            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, equipped ? 255 : pauseCtx->alpha);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->equipOotVtx[j], 4, 0);
            POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void KaleidoScope_UpdateEquipCursorOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;

    // SO2H TODO (patch 0005 polish follow-up): real analog-stick cursor movement +
    // equip/unequip interaction, mirroring KaleidoScope_UpdateMaskCursor's structure,
    // once the Link-doll render (out of scope this pass) exists to preview the result.
    if (sEquipOotCursorSlot < 0) {
        sEquipOotCursorSlot = 0;
    } else if (sEquipOotCursorSlot >= EQUIP_OOT_NUM_SLOTS) {
        sEquipOotCursorSlot = EQUIP_OOT_NUM_SLOTS - 1;
    }

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;
}
