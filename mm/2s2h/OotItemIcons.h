#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Real OOT pause-menu art lookup for the PAUSE_ITEM_OOT/PAUSE_EQUIP_OOT pages
// (mm/2s2h/Compat/Menu/kaleido_compat_menu.c). Backs `OotInventory.items[]` (a raw OOT ItemID
// byte, see z64save.h) and the (EquipmentType, tier) pairs read out of
// `OotInventory.equipment`/`OotItemEquips.equipment`.
//
// All lookups are safe to call unconditionally: they return NULL when OOT content isn't merged
// into this mm.o2r (unmodded base game) or when the id/tier is out of the known range, so callers
// always have an explicit "no art available yet" signal to fall back on (kaleido_compat_menu.c
// falls back to the pre-existing outline-box draw).
//
// Returned pointers are TexturePtr-compatible (`const char*` static resource path strings,
// e.g. "__OTR__ootr_textures/icon_item_static/gItemIconDekuStickTex") - pass them directly to
// KaleidoScope_DrawTexQuadRGBA32 / Gfx_DrawTexQuad4b exactly like MM's own `gItemIcons[itemId]`.
// Lifetime: process-lifetime static strings, safe to hold onto or call every frame.

// Highest OOT ItemID with icon/name art in the table below (ITEM_SOLD_OUT, 0x2C per
// reference/soh's z64item.h ItemID enum - the range OotInventory.items[] can actually hold).
#define OOT_ITEM_ICON_MAX_ID 0x2C

const char* OotItemIcons_GetItemIconPath(unsigned char itemId);
const char* OotItemIcons_GetItemNamePath(unsigned char itemId);

// equipType: 0=sword, 1=shield, 2=tunic, 3=boots (matches soh's EquipmentType enum order).
// tier: 1-based owned-equipment tier (matches the nibble values in OotInventory.equipment /
// OotItemEquips.equipment - tier 0 is "none" and has no art, callers should skip it). Sword
// goes up to tier 4 (Broken Giant's Knife); shield/tunic/boots only have tiers 1-3 and return
// NULL for tier 4 (their EQUIP_OOT_GRID_COLS 4th column is never owned/set).
const char* OotItemIcons_GetEquipIconPath(unsigned char equipType, unsigned char tier);
const char* OotItemIcons_GetEquipNamePath(unsigned char equipType, unsigned char tier);

#ifdef __cplusplus
}
#endif
