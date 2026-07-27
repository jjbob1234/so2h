/**
 * so2h [Menu] compat - OOT Item / OOT Equip pause pages.
 *
 * Part of patch 0001/0002 (pause menu 6-page hexagon prism coexistence).
 * See /home/user/ProjectZ/env/so2h/patches/0001_pause_menu_coexistence.md and
 * /home/user/ProjectZ/env/so2h/STRICTRULES.md rules 19-20.
 *
 * These are intentionally STUB implementations for this patch. The pause menu enum,
 * hexagon geometry tables, and dispatch wiring (z64pause_menu.h, z_kaleido_scope.h,
 * z_kaleido_scope_NES.c, z_kaleido_setup.c) are patch 0001/0002 scope and are done.
 * The actual OOT Item grid content and OOT Equip Link-doll framebuffer content are
 * separate, larger patches (0004 and 0005) not yet started. Until those land, these
 * pages exist in the L/R cycle (so2h's fixed 6-page order is correct end to end) but
 * draw nothing and don't move the cursor, because pauseCtx->itemOotPageVtx /
 * equipOotPageVtx are never allocated yet - the call sites in z_kaleido_scope_NES.c
 * already guard on that with a NULL check, so these functions are never actually
 * reached with a valid Vtx buffer today. They exist now so the dispatch switches in
 * z_kaleido_scope_NES.c compile and link cleanly against real symbols instead of
 * forward declarations with no definition.
 */

#include "global.h"
#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"

void KaleidoScope_DrawItemSelectOot(PlayState* play) {
    // SO2H TODO (patch 0004): real OOT Item page content - grid of OOT items reusing
    // 2s2h's item-icon draw primitives, sourced from the player's OOT inventory once
    // patch 0003 (scene-origin tagging) and the merged o2r (patch 0002b) exist.
}

void KaleidoScope_UpdateItemCursorOot(PlayState* play) {
    // SO2H TODO (patch 0004): real OOT Item page cursor movement, mirroring
    // KaleidoScope_UpdateItemCursor's structure once the page has real slot content.
}

void KaleidoScope_DrawEquipmentOot(PlayState* play) {
    // SO2H TODO (patch 0005): real OOT Equip page content - Link doll framebuffer
    // (ported from SoH's gPauseLinkFrameBuffer / PAUSE_EQUIP_PLAYER_WIDTH/HEIGHT,
    // referenced only, never copied verbatim) plus OOT equipment C-button slots.
}

void KaleidoScope_UpdateEquipCursorOot(PlayState* play) {
    // SO2H TODO (patch 0005): real OOT Equip page cursor/equip-slot movement, mirroring
    // KaleidoScope_UpdateMaskCursor/UpdateItemEquip's structure.
}
