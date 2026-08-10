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

// ---------------------------------------------------------------------------------------
// OOT quest-page art, for the SO2H merged quest bar
// (mm/src/overlays/kaleido_scope/ovl_kaleido_scope/so2h_quest_bar.c).
//
// Deliberately a SEPARATE table from kOotItemArt: that one's indices are a hard contract with
// soh's ItemID enum (0x00..OOT_ITEM_ICON_MAX_ID) and must never shift, and the medallions /
// spiritual stones live outside that range anyway. The ids below are private to SO2H.
//
// Two families:
//   * collectible icons - textures/icon_item_24_static, 24x24 RGBA32, drawn minified.
//   * hexagon line-art  - textures/icon_item_static gPauseQuestStatus<A><B>Tex, 80x32 IA8.
//     IMPORTANT: the two name digits are NOT row-major. soh's page-background vertex builder
//     (z_kaleido_scope_PAL.c func_80823A0C :2802-2814) steps A over x and B over y, so
//     **A = screen COLUMN (0..2), B = screen ROW (0..4)** and the full background is
//     3 cols x 5 rows of 80x32 = 240x160. The medallion hexagon lives in columns 1-2,
//     rows 0-3: tiles 10/20/11/21/12/22/13/23, an 8-tile 2-wide x 4-tall 160x128 block.
//     (The old 03/04/13/14/23/24 set was the bottom two rows of all three columns - the
//     treble clef / staff art - which is why the block rendered wrong.)
//     IA8 is monochrome+alpha, so the colour comes from the prim colour.
//
// Same contract as the functions above: returns NULL when OOT content isn't merged into this
// mm.o2r, or on an out-of-range id, so the bar always has an explicit fallback signal.
typedef enum OotQuestArtId {
    /*  0 */ OOT_QUEST_ART_MEDALLION_FOREST,
    /*  1 */ OOT_QUEST_ART_MEDALLION_FIRE,
    /*  2 */ OOT_QUEST_ART_MEDALLION_WATER,
    /*  3 */ OOT_QUEST_ART_MEDALLION_SPIRIT,
    /*  4 */ OOT_QUEST_ART_MEDALLION_SHADOW,
    /*  5 */ OOT_QUEST_ART_MEDALLION_LIGHT,
    /*  6 */ OOT_QUEST_ART_STONE_KOKIRI,
    /*  7 */ OOT_QUEST_ART_STONE_GORON,
    /*  8 */ OOT_QUEST_ART_STONE_ZORA,
    /*  9 */ OOT_QUEST_ART_STONE_OF_AGONY,
    /* 10 */ OOT_QUEST_ART_GERUDO_CARD,
    /* 11 */ OOT_QUEST_ART_GOLD_SKULLTULA,
    /* 12 */ OOT_QUEST_ART_HEART_CONTAINER,
    /* 13 */ OOT_QUEST_ART_HEART_PIECE,
    // The 2x4 hexagon block, in draw order: pairs left-to-right, stacked top-to-bottom.
    // Name digits are <column><row>, see the note above.
    /* 14 */ OOT_QUEST_ART_HEX_TILE_10, // row 0 left
    /* 15 */ OOT_QUEST_ART_HEX_TILE_20, // row 0 right
    /* 16 */ OOT_QUEST_ART_HEX_TILE_11, // row 1 left
    /* 17 */ OOT_QUEST_ART_HEX_TILE_21, // row 1 right
    /* 18 */ OOT_QUEST_ART_HEX_TILE_12, // row 2 left
    /* 19 */ OOT_QUEST_ART_HEX_TILE_22, // row 2 right
    /* 20 */ OOT_QUEST_ART_HEX_TILE_13, // row 3 left
    /* 21 */ OOT_QUEST_ART_HEX_TILE_23, // row 3 right
    /* 22 */ OOT_QUEST_ART_HEX_TILE_10_ENG, // language fallback for tile 10 only
    /* 23 */ OOT_QUEST_ART_MAX
} OotQuestArtId;

// Source dimensions of the two families, so callers don't hardcode them.
#define OOT_QUEST_ART_ICON_DIM 24
#define OOT_QUEST_ART_HEX_TILE_W 80
#define OOT_QUEST_ART_HEX_TILE_H 32

// Shape of the medallion-hexagon block assembled from the 8 tiles above.
#define OOT_QUEST_ART_HEX_BLOCK_COLS 2
#define OOT_QUEST_ART_HEX_BLOCK_ROWS 4
#define OOT_QUEST_ART_HEX_BLOCK_W (OOT_QUEST_ART_HEX_TILE_W * OOT_QUEST_ART_HEX_BLOCK_COLS) // 160
#define OOT_QUEST_ART_HEX_BLOCK_H (OOT_QUEST_ART_HEX_TILE_H * OOT_QUEST_ART_HEX_BLOCK_ROWS) // 128

const char* OotQuestArt_GetPath(int artId);

// One-shot diagnostic: logs every OotQuestArtId with its resolved archive path and whether the
// entry actually exists in the merged o2r. Repeat calls are no-ops. Used to settle whether blank
// quest slots are a layout bug or simply missing OOT art in the user's merged archive.
void OotQuestArt_LogAudit(void);

#ifdef __cplusplus
}
#endif
