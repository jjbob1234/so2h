/**
 * so2h [Menu] compat - OOT Item / OOT Equip pause pages.
 *
 * Part of patch 0001/0002 (pause menu 6-page hexagon prism coexistence), filled in by patch
 * 0004/0005 with real (non-placeholder) grid content wired to `OotSaveInfo` (z64save.h,
 * gSaveContext.save.shipSaveInfo.so2h.oot), and by this pass with real per-item/per-equipment
 * icon art, name text, and ammo/quantity overlays - full parity with MM's real Item page,
 * sourced from OOT via O2RMerger's `ootr_`-prefixed merged assets (see
 * mm/2s2h/OotItemIcons.h/.cpp and mm/2s2h/OotAssets.h/.cpp). See
 * /home/user/ProjectZ/env/so2h/patches/0001_pause_menu_coexistence.md and
 * /home/user/ProjectZ/env/so2h/STRICTRULES.md rules 19-20.
 *
 * Scope of this pass:
 *  - Real per-slot state (filled/empty, owned/not-owned) sourced live from OotSaveInfo, as
 *    before.
 *  - Real per-item/per-equipment icon art via `OotItemIcons_GetItemIconPath()` /
 *    `OotItemIcons_GetEquipIconPath()`, mirroring MM's own
 *    `KaleidoScope_DrawTexQuadRGBA32(gfxCtx, gItemIcons[itemId], 32, 32, 0)` pattern
 *    (z_kaleido_item.c). Falls back to the pre-existing `gEquippedItemOutlineTex` outline box
 *    whenever no OOT content is merged in (unmodded base game) or a slot's id/tier has no
 *    known art yet, so this is always safe to ship.
 *  - Real item name + quantity/ammo text overlays:
 *     - Name text reuses MM's own generic name-panel machinery
 *       (`KaleidoScope_UpdateNamePanel` in z_kaleido_scope_NES.c, extended this pass to
 *       recognize PAUSE_ITEM_OOT/PAUSE_EQUIP_OOT) via `Kaleido_LoadItemNameStaticOot()` /
 *       `Kaleido_LoadEquipNameStaticOot()` below - no new draw code needed, MM's existing
 *       info-panel draw already renders whatever `pauseCtx->nameSegment` points at.
 *     - Ammo/quantity digits mirror `KaleidoScope_DrawAmmoCount`'s digit-texture approach
 *       (`gAmmoDigitTextures[10]`, 8x8 IA8, z_kaleido_item.c) but positioned relative to each
 *       OOT grid slot's own quad (`itemOotVtx[j].v.ob[]`) instead of MM's fixed per-button
 *       screen positions, since the OOT item grid is a generic NxM grid, not MM's fixed set of
 *       equippable buttons.
 *  - Cursor still does NOT support analog-stick navigation within the grid (SO2H TODO in the
 *    Update* functions, unchanged from prior passes) - L/R page cycling is sufficient to reach
 *    and view both pages today.
 *  - No gameplay logic: nothing here mutates OotSaveInfo, only reads it.
 */

#include "global.h"
#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"
#include "assets/interface/parameter_static/parameter_static.h"
#include "2s2h_assets.h"
#include "2s2h/OotItemIcons.h"

#define OOT_ITEM_NONE 0xFF // mirrors reference/soh's ITEM_NONE; kept local to avoid coupling to an OOT ItemId enum that doesn't exist in this tree yet

// Encodes an (equipType, tier) pair into the single u16 `pauseCtx->cursorItem[]` slot that
// `KaleidoScope_UpdateNamePanel` compares against `pauseCtx->namedItem` to decide whether to
// reload the name texture. equipType is 0-3, tier is 1-4, so this never collides with a real
// OOT ItemID (0x00-0x2C) or with PAUSE_ITEM_NONE (999).
#define OOT_EQUIP_ENCODE(equipType, tier) (0x100 | ((equipType) << 4) | (tier))
#define OOT_EQUIP_DECODE_TYPE(encoded) (((encoded) >> 4) & 0xF)
#define OOT_EQUIP_DECODE_TIER(encoded) ((encoded) & 0xF)

extern const char* gAmmoDigitTextures[10];

static s16 sItemOotCursorSlot = 0;
static s16 sEquipOotCursorSlot = 0;

// Called from `KaleidoScope_UpdateNamePanel` (z_kaleido_scope_NES.c) when
// `pauseCtx->pageIndex == PAUSE_ITEM_OOT`, exactly where that function already calls
// `Kaleido_LoadItemNameStatic` for every other non-map page.
void Kaleido_LoadItemNameStaticOot(void** segment, u32 itemId) {
    const char* path = OotItemIcons_GetItemNamePath((u8)itemId);
    *segment = (path != NULL) ? (void*)path : (void*)gEmptyTexture;
}

// Called from `KaleidoScope_UpdateNamePanel` when `pauseCtx->pageIndex == PAUSE_EQUIP_OOT`,
// with `encoded` built by `OOT_EQUIP_ENCODE` below.
void Kaleido_LoadEquipNameStaticOot(void** segment, u32 encoded) {
    u8 equipType = OOT_EQUIP_DECODE_TYPE(encoded);
    u8 tier = OOT_EQUIP_DECODE_TIER(encoded);
    const char* path = OotItemIcons_GetEquipNamePath(equipType, tier);
    *segment = (path != NULL) ? (void*)path : (void*)gEmptyTexture;
}

// Fills a 4-vertex quad in the SAME ortho vertex-space `itemOotVtx`/`equipOotVtx` already use
// (center-origin, y-up, drawn via gSPVertex+gSP1Quadrangle) - NOT raw top-left-origin screen
// pixels, which is the (incompatible) space Gfx_DrawTexRectIA8/sAmmoRectLeft use elsewhere in
// z_kaleido_item.c. left/top/right/bottom are in that vertex space.
static void OotVtx_SetRect(Vtx* vtx, s16 left, s16 top, s16 right, s16 bottom) {
    s32 i;

    vtx[0].v.ob[0] = vtx[2].v.ob[0] = left;
    vtx[1].v.ob[0] = vtx[3].v.ob[0] = right;
    vtx[0].v.ob[1] = vtx[1].v.ob[1] = top;
    vtx[2].v.ob[1] = vtx[3].v.ob[1] = bottom;

    for (i = 0; i < 4; i++) {
        vtx[i].v.ob[2] = 0;
        vtx[i].v.flag = 0;
        vtx[i].v.cn[0] = vtx[i].v.cn[1] = vtx[i].v.cn[2] = vtx[i].v.cn[3] = 255;
    }

    vtx[0].v.tc[0] = vtx[0].v.tc[1] = vtx[1].v.tc[1] = vtx[2].v.tc[0] = 0;
    vtx[1].v.tc[0] = vtx[2].v.tc[1] = vtx[3].v.tc[0] = vtx[3].v.tc[1] = 8 * (1 << 5);
}

// Draws the 2-digit ammo count for an OOT ammo-bearing item at the bottom-right corner of its
// grid slot's own quad (mirrors KaleidoScope_DrawAmmoCount's digit-draw halves, but positioned
// off `itemOotVtx[j]`'s own quad corners, in vertex space, instead of MM's fixed
// screen-pixel-space per-button table).
static void KaleidoScope_DrawAmmoCountOot(PauseContext* pauseCtx, GraphicsContext* gfxCtx, s16 ammo, s32 j) {
    static Vtx sDigitVtx[4];
    s16 right;
    s16 bottom;
    s16 ammoUpperDigit;

    if (ammo < 0) {
        ammo = 0;
    } else if (ammo > 99) {
        ammo = 99;
    }

    // Bottom-right corner of the 32x32 slot quad (itemOotVtx[j+1]/[j+2] are that quad's
    // top-right/bottom-left corners per OotVtx_SetRect's layout below), nudged in slightly so
    // the digits sit inside the icon instead of on its edge.
    right = pauseCtx->itemOotVtx[j + 1].v.ob[0] - 2;
    bottom = pauseCtx->itemOotVtx[j + 2].v.ob[1] + 2;

    OPEN_DISPS(gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

    for (ammoUpperDigit = 0; ammo >= 10; ammoUpperDigit++) {
        ammo -= 10;
    }

    if (ammoUpperDigit != 0) {
        OotVtx_SetRect(sDigitVtx, right - 14, bottom + 8, right - 6, bottom);
        gSPVertex(POLY_OPA_DISP++, sDigitVtx, 4, 0);
        POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gAmmoDigitTextures[ammoUpperDigit], 8, 8, 0);
    }

    OotVtx_SetRect(sDigitVtx, right - 8, bottom + 8, right, bottom);
    gSPVertex(POLY_OPA_DISP++, sDigitVtx, 4, 0);
    POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gAmmoDigitTextures[ammo], 8, 8, 0);

    CLOSE_DISPS(gfxCtx);
}

// itemId -> index into OotInventory.ammo[16], or -1 if that item doesn't carry ammo. Mirrors
// reference/soh's `gAmmoItems[]` (z_kaleido_item.c) inverted: only Stick/Nut/Bomb/Bow/
// Slingshot/Bombchu/Bean carry a count on the OOT side.
static s8 OotItem_GetAmmoSlotIndex(u8 itemId) {
    switch (itemId) {
        case 0x00: return 0;  // ITEM_STICK    -> SLOT_STICK
        case 0x01: return 1;  // ITEM_NUT      -> SLOT_NUT
        case 0x02: return 2;  // ITEM_BOMB     -> SLOT_BOMB
        case 0x03: return 3;  // ITEM_BOW      -> SLOT_BOW
        case 0x06: return 6;  // ITEM_SLINGSHOT-> SLOT_SLINGSHOT
        case 0x09: return 8;  // ITEM_BOMBCHU  -> SLOT_BOMBCHU
        case 0x10: return 14; // ITEM_BEAN     -> SLOT_BEAN
        default: return -1;
    }
}

void KaleidoScope_DrawItemSelectOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    OotInventory* ootInventory = &gSaveContext.save.shipSaveInfo.so2h.oot.inventory;
    s32 i;
    s32 j;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL42_Opa(play->state.gfxCtx);

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    for (i = 0, j = 0; i < ITEM_NUM_SLOTS; i++, j += 4) {
        u8 itemId;
        const char* iconPath;
        s8 ammoSlot;

        itemId = ootInventory->items[i];
        if (itemId == OOT_ITEM_NONE) {
            continue;
        }

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, (i == sItemOotCursorSlot) ? 255 : pauseCtx->alpha);
        gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemOotVtx[j], 4, 0);

        iconPath = OotItemIcons_GetItemIconPath(itemId);
        if (iconPath != NULL) {
            KaleidoScope_DrawTexQuadRGBA32(play->state.gfxCtx, (TexturePtr)iconPath, 32, 32, 0);
        } else {
            // No merged OOT content (base game) or unmapped id yet - keep the safe fallback.
            POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
        }

        ammoSlot = OotItem_GetAmmoSlotIndex(itemId);
        if (ammoSlot >= 0) {
            KaleidoScope_DrawAmmoCountOot(pauseCtx, play->state.gfxCtx, ootInventory->ammo[ammoSlot], j);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// Real analog-stick grid navigation for the OOT Item page, ported 1:1 from MM's own
// KaleidoScope_UpdateItemCursor (z_kaleido_item.c) - a deliberate template match, not just a
// convenient one: MM's native Item grid is ITEM_GRID_ROWS(4) x ITEM_GRID_COLS(6), exactly the
// same shape as this OOT grid and the sItemMaskCursorsX(6)/sItemMaskCursorsY(4) tables the
// cursor sprite position already reads (z_kaleido_scope_NES.c ~3368). It also already handles
// the one behavior this page needs: a sparse grid where empty (OOT_ITEM_NONE) slots are still
// freely selectable, unlike the Mask page which skip-searches past unowned slots.
void KaleidoScope_UpdateItemCursorOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    OotInventory* ootInventory = &gSaveContext.save.shipSaveInfo.so2h.oot.inventory;
    u16 cursorItem;
    s16 cursorPoint;
    s16 cursorXIndex;
    s16 cursorYIndex;
    s16 moveCursorResult;

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;

    if ((pauseCtx->state != PAUSE_STATE_MAIN) || (pauseCtx->mainState != PAUSE_MAIN_STATE_IDLE) ||
        (pauseCtx->pageIndex != PAUSE_ITEM_OOT) || pauseCtx->itemDescriptionOn) {
        return;
    }

    moveCursorResult = PAUSE_CURSOR_RESULT_NONE;
    cursorItem = pauseCtx->cursorItem[PAUSE_ITEM_OOT];

    // Move cursor left/right
    if (pauseCtx->cursorSpecialPos == 0) {
        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;

        if (ABS_ALT(pauseCtx->stickAdjX) > 30) {
            cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM_OOT];
            cursorXIndex = pauseCtx->cursorXIndex[PAUSE_ITEM_OOT];
            cursorYIndex = pauseCtx->cursorYIndex[PAUSE_ITEM_OOT];

            while (moveCursorResult == PAUSE_CURSOR_RESULT_NONE) {
                if (pauseCtx->stickAdjX < -30) {
                    // move cursor left
                    pauseCtx->cursorShrinkRate = 4.0f;
                    if (pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] != 0) {
                        pauseCtx->cursorXIndex[PAUSE_ITEM_OOT]--;
                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT]--;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] = cursorXIndex;
                        pauseCtx->cursorYIndex[PAUSE_ITEM_OOT]++;

                        if (pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] >= ITEM_GRID_ROWS) {
                            pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] = 0;
                        }

                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT] =
                            pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] + (pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] * ITEM_GRID_COLS);

                        if (pauseCtx->cursorPoint[PAUSE_ITEM_OOT] >= ITEM_NUM_SLOTS) {
                            pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = pauseCtx->cursorXIndex[PAUSE_ITEM_OOT];
                        }

                        if (cursorYIndex == pauseCtx->cursorYIndex[PAUSE_ITEM_OOT]) {
                            pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] = cursorXIndex;
                            pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = cursorPoint;

                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);

                            moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                        }
                    }
                } else if (pauseCtx->stickAdjX > 30) {
                    // move cursor right
                    pauseCtx->cursorShrinkRate = 4.0f;
                    if (pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] < (ITEM_GRID_COLS - 1)) {
                        pauseCtx->cursorXIndex[PAUSE_ITEM_OOT]++;
                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT]++;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] = cursorXIndex;
                        pauseCtx->cursorYIndex[PAUSE_ITEM_OOT]++;

                        if (pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] >= ITEM_GRID_ROWS) {
                            pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] = 0;
                        }

                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT] =
                            pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] + (pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] * ITEM_GRID_COLS);

                        if (pauseCtx->cursorPoint[PAUSE_ITEM_OOT] >= ITEM_NUM_SLOTS) {
                            pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = pauseCtx->cursorXIndex[PAUSE_ITEM_OOT];
                        }

                        if (cursorYIndex == pauseCtx->cursorYIndex[PAUSE_ITEM_OOT]) {
                            pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] = cursorXIndex;
                            pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = cursorPoint;

                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);

                            moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                        }
                    }
                }
            }

            if (moveCursorResult == PAUSE_CURSOR_RESULT_SLOT) {
                cursorItem = ootInventory->items[pauseCtx->cursorPoint[PAUSE_ITEM_OOT]];
            }
        }
    } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
        if (pauseCtx->stickAdjX > 30) {
            KaleidoScope_MoveCursorFromSpecialPos(play);
            pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] = 0;
            pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] = 0;
            pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = 0;
        }
    } else { // PAUSE_CURSOR_PAGE_RIGHT
        if (pauseCtx->stickAdjX < -30) {
            KaleidoScope_MoveCursorFromSpecialPos(play);
            pauseCtx->cursorXIndex[PAUSE_ITEM_OOT] = ITEM_GRID_COLS - 1;
            pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] = 0;
            pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = ITEM_GRID_COLS - 1;
        }
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        // Move cursor up/down
        if (ABS_ALT(pauseCtx->stickAdjY) > 30) {
            moveCursorResult = PAUSE_CURSOR_RESULT_NONE;

            cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM_OOT];
            cursorYIndex = pauseCtx->cursorYIndex[PAUSE_ITEM_OOT];

            while (moveCursorResult == PAUSE_CURSOR_RESULT_NONE) {
                if (pauseCtx->stickAdjY > 30) {
                    // move cursor up
                    moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                    if (pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] != 0) {
                        pauseCtx->cursorYIndex[PAUSE_ITEM_OOT]--;
                        pauseCtx->cursorShrinkRate = 4.0f;
                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT] -= ITEM_GRID_COLS;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] = cursorYIndex;
                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = cursorPoint;
                    }
                } else if (pauseCtx->stickAdjY < -30) {
                    // move cursor down
                    moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                    if (pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] < (ITEM_GRID_ROWS - 1)) {
                        pauseCtx->cursorYIndex[PAUSE_ITEM_OOT]++;
                        pauseCtx->cursorShrinkRate = 4.0f;
                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT] += ITEM_GRID_COLS;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorYIndex[PAUSE_ITEM_OOT] = cursorYIndex;
                        pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = cursorPoint;
                    }
                }
            }

            pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;

            if ((moveCursorResult == PAUSE_CURSOR_RESULT_SLOT) || (moveCursorResult != PAUSE_CURSOR_RESULT_SPECIAL_POS)) {
                cursorItem = ootInventory->items[pauseCtx->cursorPoint[PAUSE_ITEM_OOT]];
            }
        }
    }

    // Keep the existing highlight/draw code (KaleidoScope_DrawItemSelectOot) working unchanged -
    // it still reads the static slot index rather than pauseCtx->cursorPoint directly.
    sItemOotCursorSlot = pauseCtx->cursorPoint[PAUSE_ITEM_OOT];
    if (sItemOotCursorSlot < 0) {
        sItemOotCursorSlot = 0;
    } else if (sItemOotCursorSlot >= ITEM_NUM_SLOTS) {
        sItemOotCursorSlot = ITEM_NUM_SLOTS - 1;
    }
    pauseCtx->cursorPoint[PAUSE_ITEM_OOT] = sItemOotCursorSlot;

    // Feeds KaleidoScope_UpdateNamePanel's generic `cursorItem[pageIndex]` check so the shared
    // MM info-panel draw shows this slot's real OOT item name (via Kaleido_LoadItemNameStaticOot).
    cursorItem = ootInventory->items[sItemOotCursorSlot];
    if (cursorItem == OOT_ITEM_NONE) {
        pauseCtx->cursorItem[PAUSE_ITEM_OOT] = PAUSE_ITEM_NONE;
        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    } else {
        pauseCtx->cursorItem[PAUSE_ITEM_OOT] = cursorItem;
    }
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

    for (row = 0, j = 0; row < EQUIP_OOT_GRID_ROWS; row++) {
        u16 ownedMaskForRow = (ootInventory->equipment >> (row * 4)) & 0xF;
        u16 equippedTierForRow = (ootEquips->equipment >> (row * 4)) & 0xF;

        for (col = 0; col < EQUIP_OOT_GRID_COLS; col++, j += 4) {
            bool owned = (ownedMaskForRow & (1 << col)) != 0;
            bool equipped;
            u8 tier;
            const char* iconPath;

            if (!owned) {
                continue;
            }

            tier = col + 1;
            equipped = (tier == equippedTierForRow);

            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, equipped ? 255 : pauseCtx->alpha);
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->equipOotVtx[j], 4, 0);

            iconPath = OotItemIcons_GetEquipIconPath((u8)row, tier);
            if (iconPath != NULL) {
                KaleidoScope_DrawTexQuadRGBA32(play->state.gfxCtx, (TexturePtr)iconPath, 32, 32, 0);
            } else {
                POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// Real analog-stick grid navigation for the OOT Equip page. Same shape as
// KaleidoScope_UpdateItemCursorOot above (itself ported from MM's KaleidoScope_UpdateItemCursor),
// just re-parameterized for this page's EQUIP_OOT_GRID_ROWS(4) x EQUIP_OOT_GRID_COLS(4) grid.
// Deliberately free movement (cursor can land on unowned tiers), matching the Item page's
// philosophy rather than the Mask page's skip-search-past-unowned-slots one: there is still no
// A-button equip/unequip interaction on this page (that's future scope per the original TODO,
// pending the Link-doll preview render), so skip-searching only matters once that interaction
// exists. NOTE (known, deliberate scope cut - not fixed here): the cursor sprite's screen
// position (z_kaleido_scope_NES.c ~3375) reuses sItemMaskCursorsX[], a 6-entry table tuned for
// the 6-wide Item/Mask grids, indexed here by a 0-3 column - so the diamond cursor sprite may
// not sit perfectly concentric with this page's (correctly 4-wide-spaced) equipOotVtx grid
// cells. Fixing that means adding a dedicated 4-entry cursor position table wired into that
// switch statement, which is a draw-side change outside "make the cursor move".
void KaleidoScope_UpdateEquipCursorOot(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    OotInventory* ootInventory = &gSaveContext.save.shipSaveInfo.so2h.oot.inventory;
    s32 row;
    s32 col;
    u16 cursorItem;
    s16 cursorPoint;
    s16 cursorXIndex;
    s16 cursorYIndex;
    s16 moveCursorResult;

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;

    if ((pauseCtx->state != PAUSE_STATE_MAIN) || (pauseCtx->mainState != PAUSE_MAIN_STATE_IDLE) ||
        (pauseCtx->pageIndex != PAUSE_EQUIP_OOT) || pauseCtx->itemDescriptionOn) {
        return;
    }

    moveCursorResult = PAUSE_CURSOR_RESULT_NONE;

    // Move cursor left/right
    if (pauseCtx->cursorSpecialPos == 0) {
        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;

        if (ABS_ALT(pauseCtx->stickAdjX) > 30) {
            cursorPoint = pauseCtx->cursorPoint[PAUSE_EQUIP_OOT];
            cursorXIndex = pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT];
            cursorYIndex = pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT];

            while (moveCursorResult == PAUSE_CURSOR_RESULT_NONE) {
                if (pauseCtx->stickAdjX < -30) {
                    // move cursor left
                    pauseCtx->cursorShrinkRate = 4.0f;
                    if (pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] != 0) {
                        pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT]--;
                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT]--;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] = cursorXIndex;
                        pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT]++;

                        if (pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] >= EQUIP_OOT_GRID_ROWS) {
                            pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] = 0;
                        }

                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] +
                                                                  (pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] * EQUIP_OOT_GRID_COLS);

                        if (pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] >= EQUIP_OOT_NUM_SLOTS) {
                            pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT];
                        }

                        if (cursorYIndex == pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT]) {
                            pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] = cursorXIndex;
                            pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = cursorPoint;

                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);

                            moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                        }
                    }
                } else if (pauseCtx->stickAdjX > 30) {
                    // move cursor right
                    pauseCtx->cursorShrinkRate = 4.0f;
                    if (pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] < (EQUIP_OOT_GRID_COLS - 1)) {
                        pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT]++;
                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT]++;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] = cursorXIndex;
                        pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT]++;

                        if (pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] >= EQUIP_OOT_GRID_ROWS) {
                            pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] = 0;
                        }

                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] +
                                                                  (pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] * EQUIP_OOT_GRID_COLS);

                        if (pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] >= EQUIP_OOT_NUM_SLOTS) {
                            pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT];
                        }

                        if (cursorYIndex == pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT]) {
                            pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] = cursorXIndex;
                            pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = cursorPoint;

                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);

                            moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                        }
                    }
                }
            }
        }
    } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
        if (pauseCtx->stickAdjX > 30) {
            KaleidoScope_MoveCursorFromSpecialPos(play);
            pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] = 0;
            pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] = 0;
            pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = 0;
        }
    } else { // PAUSE_CURSOR_PAGE_RIGHT
        if (pauseCtx->stickAdjX < -30) {
            KaleidoScope_MoveCursorFromSpecialPos(play);
            pauseCtx->cursorXIndex[PAUSE_EQUIP_OOT] = EQUIP_OOT_GRID_COLS - 1;
            pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] = 0;
            pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = EQUIP_OOT_GRID_COLS - 1;
        }
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        // Move cursor up/down
        if (ABS_ALT(pauseCtx->stickAdjY) > 30) {
            moveCursorResult = PAUSE_CURSOR_RESULT_NONE;

            cursorPoint = pauseCtx->cursorPoint[PAUSE_EQUIP_OOT];
            cursorYIndex = pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT];

            while (moveCursorResult == PAUSE_CURSOR_RESULT_NONE) {
                if (pauseCtx->stickAdjY > 30) {
                    // move cursor up
                    moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                    if (pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] != 0) {
                        pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT]--;
                        pauseCtx->cursorShrinkRate = 4.0f;
                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] -= EQUIP_OOT_GRID_COLS;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] = cursorYIndex;
                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = cursorPoint;
                    }
                } else if (pauseCtx->stickAdjY < -30) {
                    // move cursor down
                    moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                    if (pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] < (EQUIP_OOT_GRID_ROWS - 1)) {
                        pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT]++;
                        pauseCtx->cursorShrinkRate = 4.0f;
                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] += EQUIP_OOT_GRID_COLS;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                    } else {
                        pauseCtx->cursorYIndex[PAUSE_EQUIP_OOT] = cursorYIndex;
                        pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = cursorPoint;
                    }
                }
            }

            pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;
        }
    }

    // Keep the existing highlight/draw code (KaleidoScope_DrawEquipmentOot) working unchanged -
    // it still reads the static slot index rather than pauseCtx->cursorPoint directly.
    sEquipOotCursorSlot = pauseCtx->cursorPoint[PAUSE_EQUIP_OOT];
    if (sEquipOotCursorSlot < 0) {
        sEquipOotCursorSlot = 0;
    } else if (sEquipOotCursorSlot >= EQUIP_OOT_NUM_SLOTS) {
        sEquipOotCursorSlot = EQUIP_OOT_NUM_SLOTS - 1;
    }
    pauseCtx->cursorPoint[PAUSE_EQUIP_OOT] = sEquipOotCursorSlot;

    row = sEquipOotCursorSlot / EQUIP_OOT_GRID_COLS;
    col = sEquipOotCursorSlot % EQUIP_OOT_GRID_COLS;

    // Same generic name-panel feed as the item page, but encoding an (equipType, tier) pair
    // instead of a raw ItemID (see OOT_EQUIP_ENCODE above).
    if (((ootInventory->equipment >> (row * 4)) & 0xF) & (1 << col)) {
        cursorItem = OOT_EQUIP_ENCODE(row, col + 1);
        pauseCtx->cursorItem[PAUSE_EQUIP_OOT] = cursorItem;
    } else {
        pauseCtx->cursorItem[PAUSE_EQUIP_OOT] = PAUSE_ITEM_NONE;
    }
}
