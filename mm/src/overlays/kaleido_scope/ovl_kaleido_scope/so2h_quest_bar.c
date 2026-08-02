/*
 * File: so2h_quest_bar.c
 * Overlay: ovl_kaleido_scope
 * Description: SO2H [Menu] backwards-L merged OOT + MM quest / song bar (see so2h_quest_bar.h)
 */

#include "z_kaleido_scope.h"
#include "so2h_quest_bar.h"
#include "so2h_quest_layout.h"
#include "2s2h/Menu/so2h_pause_window.h"
#include "2s2h/OotItemIcons.h"
#include "BenPort.h"
#include "interface/parameter_static/parameter_static.h"
#include "archives/icon_item_static/icon_item_static_yar.h"
#include "archives/icon_item_24_static/icon_item_24_static_yar.h"
#include "assets/2s2h_assets.h"

// 2S2H [Port] the digit textures were made global specifically so the kaleido files could
// reach them (see the comment above sCounterTextures in z_parameter.c).
extern const char* sCounterTextures[];

// ---------------------------------------------------------------------------------------
// Contents. All geometry lives in so2h_quest_layout.c and arrives in screen space.
//
// The bar holds the whole merged quest page: OOT's medallions / spiritual stones / Stone of
// Agony / Gerudo Card / skulltula tokens plus MM's boss remains / Bomber's Notebook / heart
// tracker / spider-house tokens, and all 22 songs from both games. Equipment (sword, shield,
// quiver, bomb bag, wallet) is deliberately NOT here - MM's equipment screen owns it.
// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------
// Geometry
//
// Every on-screen rect now comes from so2h_quest_layout.c, in screen space - the
// widescreen-extended N64 rect space that gSPWideTextureRectangle actually consumes (see the
// coordinate-space note at the top of so2h_quest_layout.h). Two rules follow from that and
// both are load-bearing:
//
//   1. Nothing in this file may hard-code a 320-relative x coordinate any more.
//   2. Nothing may push a coordinate through a widescreen fan-out. The layout engine already
//      applied it, exactly once. So2h_MapX is gone for that reason - double-mapping was the
//      easiest way to silently break every wide rect on a non-4:3 display.
//
// What survives here is the stuff that is genuinely not layout: source texture dimensions,
// the fixed song count, and the proportions used to place content inside a region.
// ---------------------------------------------------------------------------------------

// The 22 songs are a fixed set. The *grid shape* they sit in is per-layout (11x2 wide,
// 6x4 at 4:3) and comes from So2h_Layout_Get()->songCols / ->songRows, so anything that
// needs the shape asks the layout instead of a macro.
#define SO2H_SONG_COUNT 22
#define BOTTOM_ARM_CELLS SO2H_SONG_COUNT

// Radial ring proportions inside SO2H_REGION_REMAINS_WINDOW, as a fraction of the window's
// inner half-height: OOT's six medallions on the outer hexagon, MM's four boss remains on
// the inner diamond, both centred on the window rather than on a fixed screen point.
#define MEDALLION_RADIUS_FRAC 0.72f
#define MEDALLION_ICON_FRAC 0.38f
#define REMAINS_RADIUS_FRAC 0.34f
#define REMAINS_ICON_FRAC 0.30f

// How much of the remains window the lifted OOT hexagon line-art fills.
#define HEX_ART_FRAC 0.88f

// Phase 1 hosts the nine legacy non-radial collectible cells in SO2H_REGION_QUEST_GRID as a
// plain 3x3 (the real 3x2 grid + separate notebook lands in phase 2). The heart tracker is
// pulled out of that run and given SO2H_REGION_HEART_WINDOW, which is its final home.
#define ROW_GRID_COLS 3
#define ROW_GRID_ROWS 3
#define ROW_ICON_FRAC 0.76f
// How much of its disc a radial icon fills, leaving the disc's bevel visible as a rim.
#define RADIAL_ICON_FRAC 0.70f

// Placeholder regions: empty-but-framed slot grids, so a region that has no content yet still
// reads as somewhere content goes.
#define PLACEHOLDER_COLS 4
#define PLACEHOLDER_ROWS 3
#define PLACEHOLDER_MENUBAR_COLS 6

// The data screen's stat slats: how many per column group, and how much of the row height is
// trimmed off each side to turn a cell into a bar.
#define DATA_SLAT_ROWS 4
#define DATA_SLAT_SQUEEZE 0.22f

// Gaps and insets are expressed against the sheet tile so they track resolution the same way
// the art does instead of collapsing at small sizes.
#define CELL_GAP_FRAC 0.12f
#define WINDOW_INSET_FRAC 0.55f

#define BTN_TEX 16 // gOcarinaATex & co are IA8 16x16

// --- Menu skin geometry ---------------------------------------------------------------
// The art is sliced from the OOT/MM GUI sheet by tools/so2h_slice_menu_sheet.py on its
// native 35 px grid (70 px for the 2x2 pieces); the source sizes below must match what
// that script writes out. Every piece is RGBA32 - this is a PC build, the N64 4 KiB TMEM
// ceiling that would have forced 32x32 / IA8 does not apply here.
#define SKIN_FRAME_CORNER_TEX 52 // gSo2hFrameTL/TR/BL/BR are 52x52
#define SKIN_FRAME_RUN_TEX 35    // the edge strips are a 35 px run across the 52 px band
#define SKIN_CELL_TEX 70         // gSo2hCellTile / gSo2hRoundTile / gSo2hSlotRecess
#define SKIN_GLYPH_TEX 35        // every single-cell glyph
#define SKIN_STAFF_TEX 70        // gSo2hStaffLines
#define SKIN_CLEF_W_TEX 35
#define SKIN_CLEF_H_TEX 70

// On-screen corner size of a drawn panel. The source corners are 52 px on the sheet's 35 px
// grid, so a corner is 52/35 of a tile; deriving it from So2h_Layout_TilePx() keeps the frame
// weight identical at every resolution instead of pinning it to one.
#define PANEL_CORNER_TILES (52.0f / 35.0f)
// Sub-windows are much smaller than a full arm panel, so they get a thinner frame.
#define SUB_PANEL_CORNER_TILES (34.0f / 35.0f)

// MM's own 24x24 quest icons and OOT's icon_item_24_static are both RGBA32 24x24
// (see the G_IM_FMT_RGBA / G_IM_SIZ_32b / 24 / 24 load in z_message.c).
#define QUEST_ICON_TEX 24
// MM's item icons (gItemIcons[]) are RGBA32 32x32, except the 0x7A.. heart-tracker run
// which is IA8 48x48 (drawn with Gfx_DrawTexQuadIA8 in z_kaleido_collect.c).
#define ITEM_ICON_TEX 32
#define HEART_TRACKER_TEX 48

// ---------------------------------------------------------------------------------------
// Right arm cells. Bar-local ordering, safe to change - nothing outside this file indexes
// it, and unlike PauseMenuPage there is no geometry table keyed off it.
// ---------------------------------------------------------------------------------------
typedef enum So2hQuestBarCell {
    /*  0 */ SO2H_CELL_MEDALLION_FIRST, // 0..5   Forest, Fire, Water, Spirit, Shadow, Light
    /*  6 */ SO2H_CELL_REMAINS_FIRST = 6, // 6..9 Odolwa, Goht, Gyorg, Twinmold
    /* 10 */ SO2H_CELL_STONE_FIRST = 10, // 10..12 Kokiri Emerald, Goron Ruby, Zora Sapphire
    /* 13 */ SO2H_CELL_STONE_OF_AGONY = 13,
    /* 14 */ SO2H_CELL_GERUDO_CARD,
    /* 15 */ SO2H_CELL_BOMBERS_NOTEBOOK,
    /* 16 */ SO2H_CELL_HEART_TRACKER,
    /* 17 */ SO2H_CELL_SKULLTULA_OOT,
    /* 18 */ SO2H_CELL_SKULLTULA_HOUSE,
    /* 19 */ SO2H_CELL_MAX
} So2hQuestBarCell;

#define RIGHT_ARM_CELLS SO2H_CELL_MAX

// Cursor neighbour table. The right arm is part radial (the hexagon) and part grid (the
// three rows), so plain index arithmetic cannot describe it; this is the same explicit
// approach MM uses for its own quest-page cursor tables in z_kaleido_collect.c.
#define NAV_NONE -1     // no movement, stay put
#define NAV_EXIT_LEFT -2   // leave the bar, back onto the page-right marker
#define NAV_EXIT_DOWN -3   // drop around the corner into the song grid

typedef struct So2hCellNav {
    s8 up;
    s8 down;
    s8 left;
    s8 right;
} So2hCellNav;

static So2hCellNav sRightArmNav[SO2H_CELL_MAX] = {
    /*  0 Forest      */ { NAV_NONE, 6, 5, 1 },
    /*  1 Fire        */ { 0, 2, 7, NAV_NONE },
    /*  2 Water       */ { 1, 3, 8, NAV_NONE },
    /*  3 Spirit      */ { 9, 11, 4, 2 },
    /*  4 Shadow      */ { 5, 3, NAV_EXIT_LEFT, 9 },
    /*  5 Light       */ { 0, 4, NAV_EXIT_LEFT, 6 },
    /*  6 Odolwa      */ { 0, 9, 5, 7 },
    /*  7 Goht        */ { 0, 8, 6, 1 },
    /*  8 Gyorg       */ { 7, 3, 9, 2 },
    /*  9 Twinmold    */ { 6, 3, 4, 8 },
    /* 10 Emerald     */ { 4, 14, NAV_EXIT_LEFT, 11 },
    /* 11 Ruby        */ { 3, 14, 10, 12 },
    /* 12 Sapphire    */ { 3, 15, 11, 13 },
    /* 13 Agony       */ { 2, 16, 12, NAV_NONE },
    /* 14 Gerudo Card */ { 10, 17, NAV_EXIT_LEFT, 15 },
    /* 15 Notebook    */ { 12, 17, 14, 16 },
    /* 16 Hearts      */ { 13, 18, 15, NAV_NONE },
    /* 17 Skull OOT   */ { 14, NAV_EXIT_DOWN, NAV_EXIT_LEFT, 18 },
    /* 18 Skull House */ { 16, NAV_EXIT_DOWN, 17, NAV_NONE },
};

// ---------------------------------------------------------------------------------------
// OOT quest bits. Values mirror reference/soh's QuestItem enum (env/soh/soh/include/z64item.h),
// which is the layout `OotInventory.questItems` stores (mm/include/z64save.h).
// ---------------------------------------------------------------------------------------
#define OOT_QUEST_MEDALLION_FOREST 0x00
#define OOT_QUEST_SONG_MINUET 0x06
#define OOT_QUEST_SONG_BOLERO 0x07
#define OOT_QUEST_SONG_SERENADE 0x08
#define OOT_QUEST_SONG_REQUIEM 0x09
#define OOT_QUEST_SONG_NOCTURNE 0x0A
#define OOT_QUEST_SONG_PRELUDE 0x0B
#define OOT_QUEST_SONG_LULLABY 0x0C
#define OOT_QUEST_SONG_EPONA 0x0D
#define OOT_QUEST_SONG_SARIA 0x0E
#define OOT_QUEST_SONG_SUN 0x0F
#define OOT_QUEST_SONG_TIME 0x10
#define OOT_QUEST_SONG_STORMS 0x11
#define OOT_QUEST_KOKIRI_EMERALD 0x12
#define OOT_QUEST_STONE_OF_AGONY 0x15
#define OOT_QUEST_GERUDO_CARD 0x16

#define OOT_QUEST_NONE 0xFF

// ---------------------------------------------------------------------------------------
// The 22 songs
// ---------------------------------------------------------------------------------------

// Ownership rules that can't be expressed as a single quest bit.
#define SONG_RULE_PLAIN 0
#define SONG_RULE_LULLABY_INTRO 1 // Goron's Lullaby: the intro flag also lights the slot
#define SONG_RULE_FROM_TIME 2     // Inverted / Double Time: no save bit, derived from Song of Time
#define SONG_RULE_SCARECROW 3     // Scarecrow's Song: gSaveContext...scarecrowSpawnSongSet

// No OCARINA_SONG_* entry exists for the six OOT warp songs (MM's ocarina code never learns
// them), so their button sequences come from sWarpSongButtons instead of gOcarinaSongButtons.
#define SONG_OCARINA_NONE 0xFF
#define SONG_WARP_NONE 0xFF

typedef struct So2hSongInfo {
    u8 mmQuestBit;  // MM QuestItem bit, or 0xFF
    u8 ootQuestBit; // OOT QuestItem bit, or OOT_QUEST_NONE
    u8 ocarinaSong; // index into gOcarinaSongButtons, or SONG_OCARINA_NONE
    u8 warpSong;    // index into sWarpSongButtons, or SONG_WARP_NONE
    u8 rule;
    u8 color[3];
} So2hSongInfo;

// Row 0 is the OOT lineage, row 1 the MM lineage, so a song MM inherited from OOT sits
// directly above/below its counterpart where one exists.
static So2hSongInfo sSongs[BOTTOM_ARM_CELLS] = {
    // --- row 0: OOT lineage ---
    /*  0 Zelda's Lullaby   */ { 0xFF, OOT_QUEST_SONG_LULLABY, OCARINA_SONG_ZELDAS_LULLABY, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 120, 180 } },
    /*  1 Epona's Song      */ { QUEST_SONG_EPONA, OOT_QUEST_SONG_EPONA, OCARINA_SONG_EPONAS, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 255, 255 } },
    /*  2 Saria's Song      */ { QUEST_SONG_SARIA, OOT_QUEST_SONG_SARIA, OCARINA_SONG_SARIAS, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 240, 100 } },
    /*  3 Sun's Song        */ { QUEST_SONG_SUN, OOT_QUEST_SONG_SUN, OCARINA_SONG_SUNS, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 255, 255 } },
    /*  4 Song of Storms    */ { QUEST_SONG_STORMS, OOT_QUEST_SONG_STORMS, OCARINA_SONG_STORMS, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 255, 255 } },
    /*  5 Minuet of Forest  */ { 0xFF, OOT_QUEST_SONG_MINUET, SONG_OCARINA_NONE, 0, SONG_RULE_PLAIN,
                                 { 60, 230, 60 } },
    /*  6 Bolero of Fire    */ { 0xFF, OOT_QUEST_SONG_BOLERO, SONG_OCARINA_NONE, 1, SONG_RULE_PLAIN,
                                 { 255, 60, 40 } },
    /*  7 Serenade of Water */ { 0xFF, OOT_QUEST_SONG_SERENADE, SONG_OCARINA_NONE, 2, SONG_RULE_PLAIN,
                                 { 60, 120, 255 } },
    /*  8 Requiem of Spirit */ { 0xFF, OOT_QUEST_SONG_REQUIEM, SONG_OCARINA_NONE, 3, SONG_RULE_PLAIN,
                                 { 255, 160, 40 } },
    /*  9 Nocturne of Shadow*/ { 0xFF, OOT_QUEST_SONG_NOCTURNE, SONG_OCARINA_NONE, 4, SONG_RULE_PLAIN,
                                 { 170, 80, 230 } },
    /* 10 Prelude of Light  */ { 0xFF, OOT_QUEST_SONG_PRELUDE, SONG_OCARINA_NONE, 5, SONG_RULE_PLAIN,
                                 { 255, 255, 120 } },
    // --- row 1: MM lineage ---
    /* 11 Song of Time      */ { QUEST_SONG_TIME, OOT_QUEST_SONG_TIME, OCARINA_SONG_TIME, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 255, 255 } },
    /* 12 Inverted SoT      */ { 0xFF, OOT_QUEST_NONE, OCARINA_SONG_INVERTED_TIME, SONG_WARP_NONE,
                                 SONG_RULE_FROM_TIME, { 200, 220, 255 } },
    /* 13 Song of Double    */ { 0xFF, OOT_QUEST_NONE, OCARINA_SONG_DOUBLE_TIME, SONG_WARP_NONE,
                                 SONG_RULE_FROM_TIME, { 255, 230, 200 } },
    /* 14 Song of Soaring   */ { QUEST_SONG_SOARING, OOT_QUEST_NONE, OCARINA_SONG_SOARING, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 255, 255 } },
    /* 15 Song of Healing   */ { QUEST_SONG_HEALING, OOT_QUEST_NONE, OCARINA_SONG_HEALING, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 255, 255 } },
    /* 16 Sonata of Awaken. */ { QUEST_SONG_SONATA, OOT_QUEST_NONE, OCARINA_SONG_SONATA, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 150, 255, 100 } },
    /* 17 Goron's Lullaby   */ { QUEST_SONG_LULLABY, OOT_QUEST_NONE, OCARINA_SONG_GORON_LULLABY, SONG_WARP_NONE,
                                 SONG_RULE_LULLABY_INTRO, { 255, 80, 40 } },
    /* 18 New Wave Bossa N. */ { QUEST_SONG_BOSSA_NOVA, OOT_QUEST_NONE, OCARINA_SONG_NEW_WAVE, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 100, 150, 255 } },
    /* 19 Elegy of Emptiness*/ { QUEST_SONG_ELEGY, OOT_QUEST_NONE, OCARINA_SONG_ELEGY, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 160, 0 } },
    /* 20 Oath to Order     */ { QUEST_SONG_OATH, OOT_QUEST_NONE, OCARINA_SONG_OATH, SONG_WARP_NONE,
                                 SONG_RULE_PLAIN, { 255, 100, 255 } },
    /* 21 Scarecrow's Song  */ { 0xFF, OOT_QUEST_NONE, OCARINA_SONG_SCARECROW_SPAWN, SONG_WARP_NONE,
                                 SONG_RULE_SCARECROW, { 220, 220, 180 } },
};

// Button sequences for the six OOT warp songs, in OCARINA_BTN_* values
// (A 0, C_DOWN 1, C_RIGHT 2, C_LEFT 3, C_UP 4). MM's gOcarinaSongButtons has no entry for
// these because MM's ocarina code never recognises them.
#define WARP_SONG_MAX_BUTTONS 8

static u8 sWarpSongButtonCount[6] = { 6, 8, 5, 6, 7, 6 };
static u8 sWarpSongButtons[6][WARP_SONG_MAX_BUTTONS] = {
    /* Minuet of Forest  */ { OCARINA_BTN_A, OCARINA_BTN_C_UP, OCARINA_BTN_C_LEFT, OCARINA_BTN_C_RIGHT,
                              OCARINA_BTN_C_LEFT, OCARINA_BTN_C_RIGHT, 0, 0 },
    /* Bolero of Fire    */ { OCARINA_BTN_C_DOWN, OCARINA_BTN_A, OCARINA_BTN_C_DOWN, OCARINA_BTN_A,
                              OCARINA_BTN_C_RIGHT, OCARINA_BTN_C_DOWN, OCARINA_BTN_C_RIGHT, OCARINA_BTN_C_DOWN },
    /* Serenade of Water */ { OCARINA_BTN_A, OCARINA_BTN_C_DOWN, OCARINA_BTN_C_RIGHT, OCARINA_BTN_C_RIGHT,
                              OCARINA_BTN_C_LEFT, 0, 0, 0 },
    /* Requiem of Spirit */ { OCARINA_BTN_A, OCARINA_BTN_C_DOWN, OCARINA_BTN_A, OCARINA_BTN_C_RIGHT,
                              OCARINA_BTN_C_DOWN, OCARINA_BTN_A, 0, 0 },
    /* Nocturne of Shadow*/ { OCARINA_BTN_C_LEFT, OCARINA_BTN_C_RIGHT, OCARINA_BTN_C_RIGHT, OCARINA_BTN_A,
                              OCARINA_BTN_C_LEFT, OCARINA_BTN_C_RIGHT, OCARINA_BTN_C_DOWN, 0 },
    /* Prelude of Light  */ { OCARINA_BTN_C_UP, OCARINA_BTN_C_RIGHT, OCARINA_BTN_C_UP, OCARINA_BTN_C_RIGHT,
                              OCARINA_BTN_C_LEFT, OCARINA_BTN_C_UP, 0, 0 },
};

// Full-colour 35x35 button glyphs from the menu sheet, indexed by OCARINA_BTN_*. The vanilla
// staff uses the IA8 16x16 gOcarinaATex set tinted at draw time; the sheet ships these already
// coloured and at the same scale as every other glyph in this menu, so the ocarina run matches
// the rest of the page instead of being the one tinted-monochrome element on it.
static TexturePtr sOcarinaButtonGlyphs[5] = {
    (TexturePtr)gSo2hBtnATex, (TexturePtr)gSo2hBtnCDownTex, (TexturePtr)gSo2hBtnCRightTex,
    (TexturePtr)gSo2hBtnCLeftTex, (TexturePtr)gSo2hBtnCUpTex,
};

// ---------------------------------------------------------------------------------------
// Art lookup tables
// ---------------------------------------------------------------------------------------

// OOT art ids for the six medallions and three spiritual stones, in ring order.
static u8 sMedallionArt[6] = {
    OOT_QUEST_ART_MEDALLION_FOREST, OOT_QUEST_ART_MEDALLION_FIRE,   OOT_QUEST_ART_MEDALLION_WATER,
    OOT_QUEST_ART_MEDALLION_SPIRIT, OOT_QUEST_ART_MEDALLION_SHADOW, OOT_QUEST_ART_MEDALLION_LIGHT,
};
static u8 sStoneArt[3] = {
    OOT_QUEST_ART_STONE_KOKIRI,
    OOT_QUEST_ART_STONE_GORON,
    OOT_QUEST_ART_STONE_ZORA,
};
// The 2x3 hexagon block, in row-major order (top-left, top-right, mid-left, ...).
static u8 sHexTileArt[6] = {
    OOT_QUEST_ART_HEX_TILE_03, OOT_QUEST_ART_HEX_TILE_04, OOT_QUEST_ART_HEX_TILE_13,
    OOT_QUEST_ART_HEX_TILE_14, OOT_QUEST_ART_HEX_TILE_23, OOT_QUEST_ART_HEX_TILE_24,
};

// Ring angles, degrees, screen-space (y grows downward so the sine is subtracted). The
// medallions sit on the hexagon's six vertices starting at the top and running clockwise in
// OOT's own order; the four boss remains sit on a concentric inner ring, offset 45 degrees so
// they land in the hexagon's gaps and can never collide with a medallion.
static s16 sMedallionAngles[6] = { 90, 30, 330, 270, 210, 150 };
static s16 sRemainsAngles[4] = { 135, 45, 315, 225 }; // Odolwa, Goht, Gyorg, Twinmold

static u8 sCellUnownedColor[3] = { 52, 48, 44 };
// The lifted OOT hexagon tiles are IA8, i.e. monochrome - this is the tint they take.
static u8 sHexArtColor[3] = { 190, 175, 130 };

// ---------------------------------------------------------------------------------------
// State. The arm-internal cursor index deliberately lives here and not in PauseContext, so
// no save struct or [PAUSE_PAGE_MAX] array has to grow for it.
// ---------------------------------------------------------------------------------------

static s16 sBarCursorIndex = 0;
static s16 sBarCursorPhase = 0;
// Remembers which spider house the player was last standing in, so the second skulltula
// counter still shows something meaningful once they have left it.
static s16 sLastSpiderHouseScene = SCENE_KINSTA1;

void So2h_QuestBar_Reset(void) {
    sBarCursorIndex = 0;
    sBarCursorPhase = 0;
}

s32 So2h_QuestBar_IsCursorInBar(PauseContext* pauseCtx) {
    return (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) ||
           (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_BOTTOM);
}

// ---------------------------------------------------------------------------------------
// Ownership tests
// ---------------------------------------------------------------------------------------

static s32 So2h_OotQuestBit(u8 bit) {
    if (bit == OOT_QUEST_NONE) {
        return false;
    }
    return (gSaveContext.save.shipSaveInfo.so2h.oot.inventory.questItems & (1 << bit)) != 0;
}

/**
 * A song is lit when MM's bit is set, OR OOT's mirror bit is set, OR its derived rule holds.
 * MM's bit is the authoritative one for the six songs both games share; the OOT bit is OR'd
 * in as a fallback so progress made on the OOT side still shows on the merged page.
 */
static s32 So2h_SongOwned(s16 songIndex) {
    So2hSongInfo* song = &sSongs[songIndex];

    if ((song->mmQuestBit != 0xFF) && (CHECK_QUEST_ITEM(song->mmQuestBit) != 0)) {
        return true;
    }
    if (So2h_OotQuestBit(song->ootQuestBit)) {
        return true;
    }

    switch (song->rule) {
        case SONG_RULE_LULLABY_INTRO:
            return CHECK_QUEST_ITEM(QUEST_SONG_LULLABY_INTRO) != 0;

        case SONG_RULE_FROM_TIME:
            // Neither variant has its own save bit in either game - holding the Song of Time
            // is what unlocks them.
            return (CHECK_QUEST_ITEM(QUEST_SONG_TIME) != 0) || So2h_OotQuestBit(OOT_QUEST_SONG_TIME);

        case SONG_RULE_SCARECROW:
            return gSaveContext.save.saveInfo.scarecrowSpawnSongSet != 0;

        default:
            return false;
    }
}

// ---------------------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------
// OVERLAY_DISP budget guard.
//
// This file writes straight into OVERLAY_DISP with `gfx++` (the same thing every other 2S2H
// HUD/menu drawer does), which bypasses the THGA bounds entirely. The bar is by far the
// biggest single consumer of the overlay list in the game, and overrunning the buffer does
// not fault - it walks into workBuffer/debugBuffer, which the master DL branches to, so
// Fast3D ends up executing garbage and the process dies with no log line and no stack. Both
// reported crashes were this: with OOT content merged the hexagon window + medallion ring
// pushed it over on pause open, and without OOT content the extra button-strip glyphs and
// cursor highlight pushed it over as soon as the cursor landed on a song.
//
// The buffer itself now has real headroom (see gfx.h), but every emit still goes through one
// of the four primitives below, and each one refuses to write when fewer than
// SO2H_GFX_RESERVE entries are left before the arena tail. Worst case the bar draws
// partially for a frame instead of corrupting memory.
// ---------------------------------------------------------------------------------------
#define SO2H_GFX_RESERVE 64

static Gfx* sGfxBudgetEnd = NULL;

static s32 So2h_GfxRoom(Gfx* gfx, s32 need) {
    return (sGfxBudgetEnd == NULL) || ((gfx + need + SO2H_GFX_RESERVE) < sGfxBudgetEnd);
}

/**
 * Rounds a screen-space float to the integer the rect commands take. Layout maths runs in
 * f32 all the way down and only collapses to integers here, so a region and the art inside
 * it can never disagree by a pixel because they rounded at different times.
 */
static s16 So2h_Rnd(f32 v) {
    return (s16)((v < 0.0f) ? (v - 0.5f) : (v + 0.5f));
}

/**
 * Current panel corner sizes, derived from the sheet tile.
 */
static s16 So2h_PanelCorner(void) {
    s16 c = (s16)(((f32)So2h_Layout_TilePx() * PANEL_CORNER_TILES) + 0.5f);

    return (c < 2) ? 2 : c;
}

// So2h_SubPanelCorner() is gone with the re-skin: sub-windows are no longer nine-sliced
// panels with a computed corner radius, they are a stretched gSo2hWindow whose corner
// scales with the rect. SUB_PANEL_CORNER_TILES is kept as documentation of the old radius.

/**
 * Grid gap / window inset in screen units, tracked against the sheet tile.
 */
static f32 So2h_CellGap(void) {
    return So2h_Layout_Get()->tile * CELL_GAP_FRAC;
}

static f32 So2h_WindowInset(void) {
    return So2h_Layout_Get()->tile * WINDOW_INSET_FRAC;
}

static Gfx* So2h_FillRect(Gfx* gfx, s16 x0, s16 y0, s16 x1, s16 y1, u8 r, u8 g, u8 b, u8 a) {
    if (!So2h_GfxRoom(gfx, 4)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillWideRectangle(gfx++, x0, y0, x1, y1);

    return gfx;
}

/**
 * RGBA32 texture rectangle. No such helper exists in the codebase - z64interface.h only
 * declares IA8 / I8 / RGBA16 rect variants - so this mirrors Gfx_DrawTexRectIA8 with the
 * RGBA32 load and without the HUD-editor branches, which do not apply to pause menu art.
 */
static Gfx* So2h_DrawTexRectRGBA32(Gfx* gfx, TexturePtr texture, s16 textureWidth, s16 textureHeight, s16 rectLeft,
                                   s16 rectTop, s16 rectWidth, s16 rectHeight) {
    u16 dsdx;
    u16 dtdy;

    if ((rectWidth <= 0) || (rectHeight <= 0) || !So2h_GfxRoom(gfx, 16)) {
        return gfx;
    }
    dsdx = (u16)(((u32)textureWidth << 10) / (u32)rectWidth);
    dtdy = (u16)(((u32)textureHeight << 10) / (u32)rectHeight);

    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, textureWidth, textureHeight, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    gSPWideTextureRectangle(gfx++, rectLeft << 2, rectTop << 2, (rectLeft + rectWidth) << 2,
                            (rectTop + rectHeight) << 2, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);

    return gfx;
}

/**
 * RGBA32 texture rectangle given screen-space edges instead of a width/height. Passing a
 * width next to an already-extended left edge silently breaks on widescreen for anything
 * wide, so every piece of art in the bar goes through an edge-pair helper.
 */
static Gfx* So2h_DrawSkinRect(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, s16 x0, s16 y0, s16 x1, s16 y1) {
    if ((x1 <= x0) || (y1 <= y0)) {
        return gfx;
    }
    return So2h_DrawTexRectRGBA32(gfx, texture, texW, texH, x0, y0, x1 - x0, y1 - y0);
}

/**
 * So2h_DrawSkinRect against a layout rect.
 */
static Gfx* So2h_DrawSkinRectR(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, const So2hRect* r) {
    return So2h_DrawSkinRect(gfx, texture, texW, texH, So2h_Rnd(r->x0), So2h_Rnd(r->y0), So2h_Rnd(r->x1),
                             So2h_Rnd(r->y1));
}

/**
 * IA8 counterpart of So2h_DrawSkinRect, for MM's ocarina button glyphs, the heart tracker and
 * the lifted OOT hexagon tiles (all IA8 - the colour comes from the prim colour).
 */
static Gfx* So2h_DrawIARect(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, s16 x0, s16 y0, s16 x1, s16 y1) {
    s16 w;
    s16 h;

    if ((x1 <= x0) || (y1 <= y0)) {
        return gfx;
    }
    w = x1 - x0;
    h = y1 - y0;

    if (!So2h_GfxRoom(gfx, 16)) {
        return gfx;
    }
    return Gfx_DrawTexRectIA8(gfx, texture, texW, texH, x0, y0, w, h, (u16)(((u32)texW << 10) / (u32)w),
                              (u16)(((u32)texH << 10) / (u32)h));
}

/**
 * So2h_DrawIARect against a layout rect.
 */
static Gfx* So2h_DrawIARectR(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, const So2hRect* r) {
    return So2h_DrawIARect(gfx, texture, texW, texH, So2h_Rnd(r->x0), So2h_Rnd(r->y0), So2h_Rnd(r->x1),
                           So2h_Rnd(r->y1));
}

static Gfx* So2h_SetupSkinMode(Gfx* gfx, u8 alpha) {
    if (!So2h_GfxRoom(gfx, 3)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, alpha);

    return gfx;
}

static Gfx* So2h_SetupTintMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 alpha) {
    if (!So2h_GfxRoom(gfx, 3)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, alpha);

    return gfx;
}

static Gfx* So2h_SetupIAMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 alpha) {
    if (!So2h_GfxRoom(gfx, 3)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, alpha);

    return gfx;
}

/**
 * Restores the plain XLU 1-cycle state the bar draws in. Called after anything that changed
 * the cycle type or render mode (the texture-rect helpers do).
 */
static Gfx* So2h_RestoreBlendState(Gfx* gfx) {
    if (!So2h_GfxRoom(gfx, 3)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    return gfx;
}

/**
 * Nine-slice panel built from the sheet's frame pieces: the recessed fill is stretched over
 * the whole rect first, then the four edge runs, then the four corners on top. Drawing the
 * fill under everything means no seam can show between the slices even when the panel is
 * scaled to an odd size.
 */
static Gfx* So2h_DrawPanelEx(Gfx* gfx, s16 x0, s16 y0, s16 x1, s16 y1, s16 corner, u8 alpha) {
    s16 c = corner;

    if (((x1 - x0) < (c * 2)) || ((y1 - y0) < (c * 2))) {
        c = ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 2;
    }

    gfx = So2h_SetupSkinMode(gfx, alpha);

    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameFillTex, SKIN_FRAME_RUN_TEX, SKIN_FRAME_RUN_TEX, x0, y0, x1, y1);

    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameTopTex, SKIN_FRAME_RUN_TEX, SKIN_FRAME_CORNER_TEX, x0 + c, y0,
                            x1 - c, y0 + c);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameBottomTex, SKIN_FRAME_RUN_TEX, SKIN_FRAME_CORNER_TEX, x0 + c,
                            y1 - c, x1 - c, y1);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameLeftTex, SKIN_FRAME_CORNER_TEX, SKIN_FRAME_RUN_TEX, x0, y0 + c,
                            x0 + c, y1 - c);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameRightTex, SKIN_FRAME_CORNER_TEX, SKIN_FRAME_RUN_TEX, x1 - c,
                            y0 + c, x1, y1 - c);

    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameTLTex, SKIN_FRAME_CORNER_TEX, SKIN_FRAME_CORNER_TEX, x0, y0,
                            x0 + c, y0 + c);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameTRTex, SKIN_FRAME_CORNER_TEX, SKIN_FRAME_CORNER_TEX, x1 - c, y0,
                            x1, y0 + c);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameBLTex, SKIN_FRAME_CORNER_TEX, SKIN_FRAME_CORNER_TEX, x0, y1 - c,
                            x0 + c, y1);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hFrameBRTex, SKIN_FRAME_CORNER_TEX, SKIN_FRAME_CORNER_TEX, x1 - c,
                            y1 - c, x1, y1);

    return gfx;
}

/**
 * Nine-slice panel over a layout rect, offset by (dx, dy) screen units so the two arm panels
 * can still slide in from off-frame.
 */
static Gfx* So2h_DrawPanelR(Gfx* gfx, const So2hRect* r, s16 corner, f32 dx, f32 dy, u8 alpha) {
    return So2h_DrawPanelEx(gfx, So2h_Rnd(r->x0 + dx), So2h_Rnd(r->y0 + dy), So2h_Rnd(r->x1 + dx),
                            So2h_Rnd(r->y1 + dy), corner, alpha);
}

// ---------------------------------------------------------------------------------------
// Region skins
//
// The bar deliberately does NOT draw the same nine-slice frame for every region - that is
// what made the first pass read as one window stamped over and over. The reference composite
// gets its depth from using a *different* piece of the sheet for each job, positioned and
// scaled but never edited:
//
//   gSo2hFrame*      52/35    the nine-slice - now ONLY the two outer arm panels
//   gSo2hWindow      210x210  inset rounded sub-window - heart, quest grid, remains, data
//                             screen, song grid, song preview, content area, menubar
//   gSo2hSlotRecess  70x70    recessed dark slot - every cell, and the data-screen slats
//   gSo2hCellTile    70x70    raised bright block - the notebook overhang and the selection
//   gSo2hRoundTile   70x70    raised disc - the backing under each radial icon
//   gSo2hSlotHatch   35x35    hatched fill - an empty-but-selectable placeholder cell
//
// These are stretched to their rect rather than nine-sliced, because that is how the
// reference is built: the window art's rounded corner grows with the panel instead of
// staying a fixed radius, and that proportional corner is most of what makes a region read
// as a window rather than as a box. The nine-slice is still right for the arm panels, whose
// frame weight has to stay constant no matter how long the arm gets.
// ---------------------------------------------------------------------------------------

#define SKIN_WINDOW_TEX 210 // gSo2hWindow

/**
 * Inset rounded sub-window. The workhorse of the redesign.
 */
static Gfx* So2h_DrawWindow(Gfx* gfx, const So2hRect* r, u8 alpha) {
    if (alpha == 0) {
        return gfx;
    }
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hWindowTex, SKIN_WINDOW_TEX, SKIN_WINDOW_TEX, r);

    return So2h_RestoreBlendState(gfx);
}

/**
 * A recessed slot. Used square for cells and stretched along x for the data-screen slats.
 */
static Gfx* So2h_DrawRecess(Gfx* gfx, const So2hRect* r, u8 alpha) {
    if (alpha == 0) {
        return gfx;
    }
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hSlotRecessTex, SKIN_CELL_TEX, SKIN_CELL_TEX, r);

    return So2h_RestoreBlendState(gfx);
}

/**
 * A raised block - the opposite read to So2h_DrawRecess, so a thing sitting on top of the
 * page (the notebook, the cursor's cell) is instantly distinguishable from a hole in it.
 */
static Gfx* So2h_DrawRaised(Gfx* gfx, const So2hRect* r, u8 alpha) {
    if (alpha == 0) {
        return gfx;
    }
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hCellTileTex, SKIN_CELL_TEX, SKIN_CELL_TEX, r);

    return So2h_RestoreBlendState(gfx);
}

/**
 * A raised disc, for the radial icons - a round backing under a round arrangement reads far
 * better than square cells scattered around a hexagon.
 */
static Gfx* So2h_DrawDisc(Gfx* gfx, const So2hRect* r, u8 alpha) {
    if (alpha == 0) {
        return gfx;
    }
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hRoundTileTex, SKIN_CELL_TEX, SKIN_CELL_TEX, r);

    return So2h_RestoreBlendState(gfx);
}

/**
 * So2h_DrawWindow for a whole layout region, honouring the region's morph alpha.
 */
static Gfx* So2h_DrawRegionWindow(Gfx* gfx, So2hLayoutRegion regionId, u8 alpha) {
    const So2hRect* region = So2h_Layout_Region(regionId);
    u8 a = (u8)((f32)alpha * So2h_Layout_RegionAlpha(regionId));

    if ((a == 0) || (region->x1 <= region->x0) || (region->y1 <= region->y0)) {
        return gfx;
    }
    return So2h_DrawWindow(gfx, region, a);
}

/**
 * Draws a right-aligned decimal count using the shared HUD counter digit textures.
 */
static Gfx* So2h_DrawCount(Gfx* gfx, s16 value, s16 rightX, s16 topY, u8 alpha) {
    s16 digits[3];
    s16 count = 0;
    s16 i;
    s16 x;

    if (value < 0) {
        value = 0;
    }
    if (value > 999) {
        value = 999;
    }

    do {
        digits[count++] = value % 10;
        value /= 10;
    } while ((value != 0) && (count < 3));

    gfx = So2h_SetupIAMode(gfx, 255, 255, 255, alpha);

    x = rightX - (count * 7);
    for (i = count - 1; i >= 0; i--) {
        if (!So2h_GfxRoom(gfx, 16)) {
            break;
        }
        // sCounterTextures[] is I8 everywhere else in the codebase (z_parameter.c's HUD counters
        // and z_kaleido_collect.c both load it as G_IM_FMT_I / G_IM_SIZ_8b); drawing it as IA8
        // reinterprets the intensity ramp as alpha and makes the digits fade out.
        gfx = Gfx_DrawTexRectI8(gfx, (TexturePtr)sCounterTextures[digits[i]], 8, 16, x, topY, 6, 12, (8 << 10) / 6,
                                (16 << 10) / 12);
        x += 7;
    }

    return gfx;
}

// ---------------------------------------------------------------------------------------
// Cell geometry
// ---------------------------------------------------------------------------------------

/**
 * Cheap integer cos/sin for the ring layouts, in 1/1000ths. Only the eight angles the two
 * rings actually use are needed, so this avoids dragging sinf/cosf and a degrees-to-binang
 * conversion into the overlay for four constants.
 */
static void So2h_RingOffset(s16 angleDeg, s16 radius, s16* dx, s16* dy) {
    // cos/sin * 1000 for the angles used by sMedallionAngles / sRemainsAngles.
    static const s16 sAngleTable[8][3] = {
        // { angle, cos*1000, sin*1000 }
        { 30, 866, 500 },   { 45, 707, 707 },    { 90, 0, 1000 },    { 135, -707, 707 },
        { 150, -866, 500 }, { 210, -866, -500 }, { 225, -707, -707 }, { 270, 0, -1000 },
    };
    s16 i;

    for (i = 0; i < 8; i++) {
        if (sAngleTable[i][0] == angleDeg) {
            *dx = (s16)(((s32)radius * sAngleTable[i][1]) / 1000);
            *dy = (s16)(-((s32)radius * sAngleTable[i][2]) / 1000);
            return;
        }
    }
    // 315 and 330 are the mirrors of 45 and 30 about the x axis.
    if (angleDeg == 315) {
        *dx = (s16)(((s32)radius * 707) / 1000);
        *dy = (s16)(((s32)radius * 707) / 1000);
        return;
    }
    if (angleDeg == 330) {
        *dx = (s16)(((s32)radius * 866) / 1000);
        *dy = (s16)(((s32)radius * 500) / 1000);
        return;
    }
    *dx = 0;
    *dy = 0;
}

/**
 * Song grid shape for the layout currently on screen. The 22 songs are fixed but the grid
 * they sit in is not (11x2 wide, 6x4 at 4:3), so cursor arithmetic asks for it rather than
 * assuming a constant. Guaranteed to describe at least SO2H_SONG_COUNT cells.
 */
static s16 So2h_SongCols(void) {
    s16 cols = So2h_Layout_Get()->songCols;

    return (cols < 1) ? 1 : cols;
}

static s16 So2h_SongRows(void) {
    s16 rows = So2h_Layout_Get()->songRows;

    return (rows < 1) ? 1 : rows;
}

/**
 * Screen-space rect of a right-arm cell.
 *
 *   0..5    OOT medallions  - outer hexagon of the remains window
 *   6..9    MM boss remains - inner diamond of the same window
 *   16      heart tracker   - its own window (its final home)
 *   others  the legacy collectible cells, hosted 3x3 in the quest grid for phase 1
 *
 * The radial cells are placed from the *window's* centre with a radius taken from the
 * window's own inner half-size, so they follow the frame instead of a fixed screen point.
 */
static void So2h_RightArmCellRect(s16 index, So2hRect* out) {
    const So2hRect* window;
    f32 inset = So2h_WindowInset();
    f32 cx;
    f32 cy;
    f32 half;
    f32 radius;
    f32 icon;
    s16 dx;
    s16 dy;
    s16 slot;

    if (((index >= SO2H_CELL_MEDALLION_FIRST) && (index < SO2H_CELL_MEDALLION_FIRST + 6)) ||
        ((index >= SO2H_CELL_REMAINS_FIRST) && (index < SO2H_CELL_REMAINS_FIRST + 4))) {
        s32 isMedallion = (index < SO2H_CELL_REMAINS_FIRST);
        f32 w;
        f32 h;

        window = So2h_Layout_Region(SO2H_REGION_REMAINS_WINDOW);
        cx = (window->x0 + window->x1) * 0.5f;
        cy = (window->y0 + window->y1) * 0.5f;

        w = (window->x1 - window->x0) - (inset * 4.0f);
        h = (window->y1 - window->y0) - (inset * 4.0f);
        half = ((w < h) ? w : h) * 0.5f;
        if (half < 1.0f) {
            half = 1.0f;
        }

        if (isMedallion) {
            slot = index - SO2H_CELL_MEDALLION_FIRST;
            radius = half * MEDALLION_RADIUS_FRAC;
            icon = half * MEDALLION_ICON_FRAC;
            So2h_RingOffset(sMedallionAngles[slot], (s16)radius, &dx, &dy);
        } else {
            slot = index - SO2H_CELL_REMAINS_FIRST;
            radius = half * REMAINS_RADIUS_FRAC;
            icon = half * REMAINS_ICON_FRAC;
            So2h_RingOffset(sRemainsAngles[slot], (s16)radius, &dx, &dy);
        }

        out->x0 = cx + (f32)dx - (icon * 0.5f);
        out->y0 = cy + (f32)dy - (icon * 0.5f);
        out->x1 = out->x0 + icon;
        out->y1 = out->y0 + icon;
        return;
    }

    if (index == SO2H_CELL_HEART_TRACKER) {
        So2h_Layout_Inset(So2h_Layout_Region(SO2H_REGION_HEART_WINDOW), inset * 2.0f, out);
        return;
    }

    // Legacy 3x3 run: stones, Stone of Agony, Gerudo Card, notebook, the two skulltula
    // counters. Index 16 was lifted out above, so 17/18 close the gap it left.
    if (index < SO2H_CELL_HEART_TRACKER) {
        slot = index - SO2H_CELL_STONE_FIRST;
    } else {
        slot = index - SO2H_CELL_STONE_FIRST - 1;
    }
    if (slot < 0) {
        slot = 0;
    }
    if (slot >= (ROW_GRID_COLS * ROW_GRID_ROWS)) {
        slot = (ROW_GRID_COLS * ROW_GRID_ROWS) - 1;
    }

    So2h_Layout_GridCell(So2h_Layout_Region(SO2H_REGION_QUEST_GRID), ROW_GRID_COLS, ROW_GRID_ROWS,
                         (s16)(slot % ROW_GRID_COLS), (s16)(slot / ROW_GRID_COLS), So2h_CellGap(), out);
}

/**
 * Screen-space rect of a song cell, from the song grid region and the active grid shape.
 */
static void So2h_BottomArmCellRect(s16 index, So2hRect* out) {
    s16 cols = So2h_SongCols();

    So2h_Layout_GridCell(So2h_Layout_Region(SO2H_REGION_SONG_GRID), cols, So2h_SongRows(), (s16)(index % cols),
                         (s16)(index / cols), So2h_CellGap(), out);
}

// ---------------------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------------------

static void So2h_QuestBar_Enter(PlayState* play, s16 specialPos, s16 index) {
    PauseContext* pauseCtx = &play->pauseCtx;

    pauseCtx->cursorSpecialPos = specialPos;
    sBarCursorIndex = index;
    pauseCtx->pageSwitchInputTimer = 0;
    pauseCtx->nameDisplayTimer = 0;
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

static void So2h_QuestBar_Exit(PlayState* play, s16 specialPos) {
    PauseContext* pauseCtx = &play->pauseCtx;

    pauseCtx->cursorSpecialPos = specialPos;
    sBarCursorIndex = 0;
    pauseCtx->pageSwitchInputTimer = 0;
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

/**
 * Applies one entry of the right arm's neighbour table.
 */
static void So2h_QuestBar_Navigate(PlayState* play, s8 target) {
    if (target == NAV_NONE) {
        return;
    }
    if (target == NAV_EXIT_LEFT) {
        So2h_QuestBar_Exit(play, PAUSE_CURSOR_PAGE_RIGHT);
        return;
    }
    if (target == NAV_EXIT_DOWN) {
        // Around the corner into the rightmost song column.
        So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_BOTTOM, (s16)(So2h_SongCols() - 1));
        return;
    }

    sBarCursorIndex = target;
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

s32 So2h_QuestBar_UpdateCursor(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 cols;
    s16 col;
    s16 row;

    // The bar only exists while the window is engaged (never on Owl Warp or Game Over).
    if (!So2h_PauseWindow_IsActive()) {
        if (So2h_QuestBar_IsCursorInBar(pauseCtx)) {
            pauseCtx->cursorSpecialPos = 0;
            sBarCursorIndex = 0;
        }
        return false;
    }

    // Entry gestures, taken from the two page-switch special positions so no per-page cursor
    // function has to be modified:
    //   - push right again while parked on the R (page right) marker -> right arm
    //   - push down while parked on either page marker              -> bottom arm
    if (!So2h_QuestBar_IsCursorInBar(pauseCtx)) {
        if ((pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) ||
            (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT)) {
            if (pauseCtx->stickAdjY < -30) {
                So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_BOTTOM, 0);
                return true;
            }
            if ((pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT) && (pauseCtx->stickAdjX > 30)) {
                So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_RIGHT, SO2H_CELL_MEDALLION_FIRST);
                return true;
            }
        }
        return false;
    }

    if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) {
        if (sBarCursorIndex < 0) {
            sBarCursorIndex = 0;
        } else if (sBarCursorIndex >= RIGHT_ARM_CELLS) {
            sBarCursorIndex = RIGHT_ARM_CELLS - 1;
        }

        if (pauseCtx->stickAdjX < -30) {
            So2h_QuestBar_Navigate(play, sRightArmNav[sBarCursorIndex].left);
        } else if (pauseCtx->stickAdjX > 30) {
            So2h_QuestBar_Navigate(play, sRightArmNav[sBarCursorIndex].right);
        } else if (pauseCtx->stickAdjY > 30) {
            So2h_QuestBar_Navigate(play, sRightArmNav[sBarCursorIndex].up);
        } else if (pauseCtx->stickAdjY < -30) {
            So2h_QuestBar_Navigate(play, sRightArmNav[sBarCursorIndex].down);
        }
        return true;
    }

    // PAUSE_CURSOR_QUEST_BAR_BOTTOM
    cols = So2h_SongCols();
    col = sBarCursorIndex % cols;
    row = sBarCursorIndex / cols;

    if (pauseCtx->stickAdjX < -30) {
        if (col > 0) {
            sBarCursorIndex--;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
    } else if (pauseCtx->stickAdjX > 30) {
        if ((col < (cols - 1)) && ((sBarCursorIndex + 1) < BOTTOM_ARM_CELLS)) {
            sBarCursorIndex++;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        } else {
            // Rightmost song column steps up into the right arm's bottom row.
            So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_RIGHT, SO2H_CELL_SKULLTULA_HOUSE);
        }
    } else if (pauseCtx->stickAdjY > 30) {
        if (row > 0) {
            sBarCursorIndex -= cols;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        } else {
            // Top row of the bottom arm returns to the page itself.
            KaleidoScope_MoveCursorFromSpecialPos(play);
            sBarCursorIndex = 0;
        }
    } else if (pauseCtx->stickAdjY < -30) {
        if ((row < (So2h_SongRows() - 1)) && ((sBarCursorIndex + cols) < BOTTOM_ARM_CELLS)) {
            sBarCursorIndex += cols;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
    }

    if (sBarCursorIndex < 0) {
        sBarCursorIndex = 0;
    } else if (sBarCursorIndex >= BOTTOM_ARM_CELLS) {
        sBarCursorIndex = BOTTOM_ARM_CELLS - 1;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Right arm
// ---------------------------------------------------------------------------------------

/**
 * The hexagon sub-window: a thin nine-slice frame from the sheet, with OOT's own quest-page
 * hexagon line-art tiled inside it. Per the merge rules MM-sourced art needs no added border
 * but OOT-sourced art must be small pieces inside a sheet-built window, which is exactly what
 * this is - six 80x32 IA8 tiles stacked 2x3 into one 160x96 image and scaled down, never
 * blown up. Falls back to the plain frame when OOT content isn't merged in.
 */
static Gfx* So2h_DrawHexWindow(Gfx* gfx, u8 alpha) {
    const So2hRect* window = So2h_Layout_Region(SO2H_REGION_REMAINS_WINDOW);
    So2hRect art;
    f32 cx = (window->x0 + window->x1) * 0.5f;
    f32 cy = (window->y0 + window->y1) * 0.5f;
    f32 inset = So2h_WindowInset();
    f32 w = ((window->x1 - window->x0) - (inset * 4.0f)) * HEX_ART_FRAC;
    f32 h = ((window->y1 - window->y0) - (inset * 4.0f)) * HEX_ART_FRAC;
    f32 tileW;
    f32 tileH;
    s16 i;

    gfx = So2h_DrawWindow(gfx, window, alpha);

    // The lifted art is 2 tiles wide x 3 tall of 80x32, so it is 160x96 - wider than it is
    // tall. Fit it to the window's shorter axis so it never spills out of the frame.
    if ((w / 160.0f) > (h / 96.0f)) {
        w = (h / 96.0f) * 160.0f;
    } else {
        h = (w / 160.0f) * 96.0f;
    }
    tileW = w * 0.5f;
    tileH = h / 3.0f;

    gfx = So2h_SetupIAMode(gfx, sHexArtColor[0], sHexArtColor[1], sHexArtColor[2], alpha);
    for (i = 0; i < 6; i++) {
        const char* path = OotQuestArt_GetPath(sHexTileArt[i]);

        if (path == NULL) {
            continue;
        }
        art.x0 = cx - (w * 0.5f) + ((f32)(i % 2) * tileW);
        art.y0 = cy - (h * 0.5f) + ((f32)(i / 2) * tileH);
        art.x1 = art.x0 + tileW;
        art.y1 = art.y0 + tileH;

        gfx = So2h_DrawIARectR(gfx, (TexturePtr)path, OOT_QUEST_ART_HEX_TILE_W, OOT_QUEST_ART_HEX_TILE_H, &art);
    }

    return So2h_RestoreBlendState(gfx);
}

/**
 * One collectible icon, drawn owned (full colour) or unowned (heavily dimmed, so the slot
 * still reads as a place a thing goes). Every icon here is real art - the old flat colour
 * swatches are gone.
 */
static Gfx* So2h_DrawCollectible(Gfx* gfx, TexturePtr tex, s16 texDim, const So2hRect* rect, s32 owned, u8 alpha) {
    if (tex == NULL) {
        // No art available (OOT content not merged in): fall back to the sheet's recessed
        // slot so the layout doesn't collapse into a hole.
        return So2h_DrawRecess(gfx, rect, (u8)(alpha / 3));
    }

    if (owned) {
        gfx = So2h_SetupSkinMode(gfx, alpha);
    } else {
        gfx = So2h_SetupTintMode(gfx, sCellUnownedColor[0] * 2, sCellUnownedColor[1] * 2, sCellUnownedColor[2] * 2,
                                 (u8)(alpha / 3));
    }
    gfx = So2h_DrawSkinRectR(gfx, tex, texDim, texDim, rect);

    if (!owned) {
        // A dimmed icon alone is ambiguous at a glance against a dark page; the sheet's red
        // cross over it makes "not collected" unmistakable without hiding what the slot is.
        gfx = So2h_SetupSkinMode(gfx, (u8)(alpha * 2 / 5));
        gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hCrossRedTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, rect);
    }

    return So2h_RestoreBlendState(gfx);
}

/**
 * The two concentric rings inside the hexagon window: OOT's six medallions on the hexagon's
 * vertices, MM's four boss remains on an inner ring sharing the same center.
 */
static void So2h_IconInRect(const So2hRect* cell, f32 frac, So2hRect* out);

static Gfx* So2h_DrawRadialIcons(Gfx* gfx, u8 alpha) {
    So2hRect cell;
    So2hRect icon;
    s16 i;

    for (i = 0; i < 6; i++) {
        const char* path = OotQuestArt_GetPath(sMedallionArt[i]);

        So2h_RightArmCellRect((s16)(SO2H_CELL_MEDALLION_FIRST + i), &cell);
        // A round backing under a round arrangement: the disc reads as a socket the
        // medallion sits in, and keeps the hexagon line-art from running under the icon.
        gfx = So2h_DrawDisc(gfx, &cell, alpha);
        So2h_IconInRect(&cell, RADIAL_ICON_FRAC, &icon);
        gfx = So2h_DrawCollectible(gfx, (TexturePtr)path, OOT_QUEST_ART_ICON_DIM, &icon,
                                   So2h_OotQuestBit(OOT_QUEST_MEDALLION_FOREST + i), alpha);
    }

    for (i = 0; i < 4; i++) {
        So2h_RightArmCellRect((s16)(SO2H_CELL_REMAINS_FIRST + i), &cell);
        gfx = So2h_DrawDisc(gfx, &cell, alpha);
        So2h_IconInRect(&cell, RADIAL_ICON_FRAC, &icon);
        gfx = So2h_DrawCollectible(gfx, (TexturePtr)gItemIcons[ITEM_REMAINS_ODOLWA + i], ITEM_ICON_TEX, &icon,
                                   CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA + i) != 0, alpha);
    }

    return gfx;
}

/**
 * Centred square inside a cell, `frac` of the cell's shorter axis. Icons are square art and
 * cells are not, so nothing may just fill its cell or every icon stretches.
 */
static void So2h_IconInRect(const So2hRect* cell, f32 frac, So2hRect* out) {
    f32 w = cell->x1 - cell->x0;
    f32 h = cell->y1 - cell->y0;
    f32 size = ((w < h) ? w : h) * frac;
    f32 cx = (cell->x0 + cell->x1) * 0.5f;
    f32 cy = (cell->y0 + cell->y1) * 0.5f;

    if (size < 1.0f) {
        size = 1.0f;
    }
    out->x0 = cx - (size * 0.5f);
    out->y0 = cy - (size * 0.5f);
    out->x1 = out->x0 + size;
    out->y1 = out->y0 + size;
}

/**
 * The non-radial collectibles: spiritual stones, Stone of Agony, Gerudo Card, Bomber's
 * Notebook and the heart tracker, each in a recessed sheet slot.
 *
 * Phase 1 keeps the exact same element set as before and only re-hosts it: eight of them run
 * 3x3 through the quest grid region and the heart tracker takes the heart window. Splitting
 * the grid into its real 3x2 + separate overhanging notebook is phase 2.
 */
static Gfx* So2h_DrawCollectibleRows(Gfx* gfx, u8 alpha) {
    So2hRect cell;
    So2hRect icon;
    s16 heartPieces;
    s16 i;

    for (i = SO2H_CELL_STONE_FIRST; i <= SO2H_CELL_HEART_TRACKER; i++) {
        So2h_RightArmCellRect(i, &cell);
        So2h_IconInRect(&cell, ROW_ICON_FRAC, &icon);

        // The notebook is the one element that sits *on* the page rather than in it, so it
        // gets the raised block and everything else gets the recessed slot. That one
        // inversion is what stops the grid reading as nine identical holes.
        if (i == SO2H_CELL_BOMBERS_NOTEBOOK) {
            gfx = So2h_DrawRaised(gfx, &cell, alpha);
        } else {
            gfx = So2h_DrawRecess(gfx, &cell, alpha);
        }

        if ((i >= SO2H_CELL_STONE_FIRST) && (i < SO2H_CELL_STONE_OF_AGONY)) {
            s16 stone = i - SO2H_CELL_STONE_FIRST;

            gfx = So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(sStoneArt[stone]), OOT_QUEST_ART_ICON_DIM,
                                       &icon, So2h_OotQuestBit(OOT_QUEST_KOKIRI_EMERALD + stone), alpha);
        } else if (i == SO2H_CELL_STONE_OF_AGONY) {
            gfx = So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(OOT_QUEST_ART_STONE_OF_AGONY),
                                       OOT_QUEST_ART_ICON_DIM, &icon, So2h_OotQuestBit(OOT_QUEST_STONE_OF_AGONY),
                                       alpha);
        } else if (i == SO2H_CELL_GERUDO_CARD) {
            gfx = So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(OOT_QUEST_ART_GERUDO_CARD),
                                       OOT_QUEST_ART_ICON_DIM, &icon, So2h_OotQuestBit(OOT_QUEST_GERUDO_CARD), alpha);
        } else if (i == SO2H_CELL_BOMBERS_NOTEBOOK) {
            gfx = So2h_DrawCollectible(gfx, (TexturePtr)gItemIconBombersNotebookTex, ITEM_ICON_TEX, &icon,
                                       CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK) != 0, alpha);
        } else {
            // Heart tracker: MM's own 48x48 IA8 heart-piece-fill icon, the same
            // gItemIcons[0x7A + count] the vanilla quest page uses, plus the heart total.
            heartPieces = (s16)((GET_SAVE_INVENTORY_QUEST_ITEMS & 0xF0000000) >> QUEST_HEART_PIECE_COUNT);

            gfx = So2h_SetupIAMode(gfx, 255, 255, 255, alpha);
            gfx = So2h_DrawIARectR(gfx, (TexturePtr)gItemIcons[0x7A + heartPieces], HEART_TRACKER_TEX,
                                   HEART_TRACKER_TEX, &icon);
            gfx = So2h_RestoreBlendState(gfx);

            gfx = So2h_DrawCount(gfx, (s16)(gSaveContext.save.saveInfo.playerData.healthCapacity / 16),
                                 (s16)(So2h_Rnd(cell.x1) - 3), (s16)(So2h_Rnd(cell.y0) + 3), alpha);
            gfx = So2h_RestoreBlendState(gfx);
        }
    }

    return gfx;
}

/**
 * The two skulltula counters. The first is OOT's Gold Skulltula token total, the second the
 * current (or last visited) MM spider house. Both use MM's own 24x24 skulltula icon from
 * icon_item_24_static_yar and MM's HUD counter digits - no OOT art needed.
 */
static Gfx* So2h_DrawSkulltulaCounters(Gfx* gfx, PlayState* play, u8 alpha) {
    So2hRect cell;
    So2hRect icon;
    s16 counts[2];
    s16 i;

    if ((play->sceneId == SCENE_KINSTA1) || (play->sceneId == SCENE_KINDAN2)) {
        sLastSpiderHouseScene = play->sceneId;
    }

    counts[0] = gSaveContext.save.shipSaveInfo.so2h.oot.inventory.gsTokens;
    counts[1] = Inventory_GetSkullTokenCount(sLastSpiderHouseScene);

    for (i = 0; i < 2; i++) {
        So2h_RightArmCellRect((s16)(SO2H_CELL_SKULLTULA_OOT + i), &cell);

        // The icon sits left in its cell so the digits have the right half to themselves.
        So2h_IconInRect(&cell, ROW_ICON_FRAC, &icon);
        icon.x0 = cell.x0 + ((cell.x1 - cell.x0) * 0.04f);
        icon.x1 = icon.x0 + (icon.y1 - icon.y0);

        gfx = So2h_DrawRecess(gfx, &cell, alpha);
        gfx = So2h_SetupSkinMode(gfx, alpha);
        gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gQuestIconGoldSkulltulaTex, QUEST_ICON_TEX, QUEST_ICON_TEX, &icon);
        gfx = So2h_RestoreBlendState(gfx);

        gfx = So2h_DrawCount(gfx, counts[i], (s16)(So2h_Rnd(cell.x1) - 3), (s16)(So2h_Rnd(cell.y0) + 1), alpha);
        gfx = So2h_RestoreBlendState(gfx);
    }

    return gfx;
}

// ---------------------------------------------------------------------------------------
// Bottom arm
// ---------------------------------------------------------------------------------------

static Gfx* So2h_DrawSongCell(Gfx* gfx, s16 index, u8 alpha) {
    So2hSongInfo* song = &sSongs[index];
    So2hRect cell;
    So2hRect note;
    s32 owned = So2h_SongOwned(index);

    So2h_BottomArmCellRect(index, &cell);
    So2h_IconInRect(&cell, 0.86f, &note);

    gfx = So2h_DrawRecess(gfx, &cell, alpha);

    if (owned) {
        gfx = So2h_SetupTintMode(gfx, song->color[0], song->color[1], song->color[2], alpha);
        gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hNoteWhiteTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, &note);
    } else {
        gfx = So2h_SetupTintMode(gfx, 255, 255, 255, (u8)(alpha * 3 / 4));
        gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hNoteLockedTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, &note);
    }

    return So2h_RestoreBlendState(gfx);
}

/**
 * The song preview region: clef + staff, plus the ocarina button sequence for whichever song
 * the cursor is on. Phase 1 only re-hosts what was already the full-width button strip into
 * SO2H_REGION_SONG_PREVIEW; the song *name* and staff-positioned notes are phase 4.
 *
 * Sequences come from MM's own gOcarinaSongButtons wherever MM knows the song; the six OOT
 * warp songs, which MM's ocarina code never learns, come from sWarpSongButtons.
 */
static Gfx* So2h_DrawSongButtonStrip(Gfx* gfx, PauseContext* pauseCtx, u8 alpha) {
    const So2hRect* region = So2h_Layout_Region(SO2H_REGION_SONG_PREVIEW);
    u8 buttons[WARP_SONG_MAX_BUTTONS];
    So2hRect inner;
    So2hRect rect;
    f32 clefW;
    f32 glyph;
    f32 gap;
    f32 totalW;
    f32 x;
    f32 cy;
    s16 numButtons = 0;
    So2hSongInfo* song;
    s16 songIndex;
    s16 i;

    // The preview sits in its own sub-window, like every other region in the reference.
    gfx = So2h_DrawRegionWindow(gfx, SO2H_REGION_SONG_PREVIEW, alpha);
    So2h_Layout_Inset(region, So2h_WindowInset() * 2.0f, &inner);

    // A staff run is 2 tiles tall on the sheet; the clef is half a tile wide against it.
    glyph = (inner.y1 - inner.y0) * 0.55f;
    if (glyph < 4.0f) {
        glyph = 4.0f;
    }
    gap = glyph * 0.2f;
    clefW = glyph * 0.5f;
    cy = (inner.y0 + inner.y1) * 0.5f;

    // Clef flourish + staff, always present so the region never reads as an empty gap.
    gfx = So2h_SetupSkinMode(gfx, alpha);
    rect.x0 = inner.x0;
    rect.x1 = inner.x0 + clefW;
    rect.y0 = cy - glyph;
    rect.y1 = cy + glyph;
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hStaffClefTex, SKIN_CLEF_W_TEX, SKIN_CLEF_H_TEX, &rect);

    rect.x0 = inner.x0 + clefW;
    rect.x1 = inner.x1;
    rect.y0 = cy - (glyph * 0.7f);
    rect.y1 = cy + (glyph * 0.7f);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hStaffLinesTex, SKIN_STAFF_TEX, SKIN_STAFF_TEX, &rect);
    gfx = So2h_RestoreBlendState(gfx);

    if (pauseCtx->cursorSpecialPos != PAUSE_CURSOR_QUEST_BAR_BOTTOM) {
        return gfx;
    }

    songIndex = sBarCursorIndex;
    if ((songIndex < 0) || (songIndex >= BOTTOM_ARM_CELLS)) {
        return gfx;
    }
    song = &sSongs[songIndex];

    if (song->warpSong != SONG_WARP_NONE) {
        numButtons = sWarpSongButtonCount[song->warpSong];
        for (i = 0; i < numButtons; i++) {
            buttons[i] = sWarpSongButtons[song->warpSong][i];
        }
    } else if (song->ocarinaSong != SONG_OCARINA_NONE) {
        numButtons = gOcarinaSongButtons[song->ocarinaSong].numButtons;
        if (numButtons > WARP_SONG_MAX_BUTTONS) {
            numButtons = WARP_SONG_MAX_BUTTONS;
        }
        for (i = 0; i < numButtons; i++) {
            buttons[i] = gOcarinaSongButtons[song->ocarinaSong].buttonIndex[i];
        }
    }

    if ((numButtons <= 0) || !So2h_SongOwned(songIndex)) {
        // An unlearned song keeps its sequence secret, exactly like the vanilla quest page.
        return gfx;
    }

    // Shrink the run rather than overflow the region if eight glyphs will not fit.
    totalW = ((f32)numButtons * glyph) + ((f32)(numButtons - 1) * gap);
    {
        f32 avail = (inner.x1 - (inner.x0 + clefW + gap));

        if ((totalW > avail) && (totalW > 0.0f)) {
            f32 shrink = avail / totalW;

            glyph *= shrink;
            gap *= shrink;
            totalW = avail;
        }
    }

    x = inner.x0 + clefW + gap + (((inner.x1 - (inner.x0 + clefW + gap)) - totalW) * 0.5f);

    for (i = 0; i < numButtons; i++) {
        u8 btn = buttons[i];

        if (btn >= 5) {
            // OCARINA_BTN_C_RIGHT_OR_C_LEFT / OCARINA_BTN_INVALID: nothing sensible to draw.
            x += glyph + gap;
            continue;
        }
        rect.x0 = x;
        rect.x1 = x + glyph;
        rect.y0 = cy - (glyph * 0.5f);
        rect.y1 = rect.y0 + glyph;

        gfx = So2h_SetupSkinMode(gfx, alpha);
        gfx = So2h_DrawSkinRectR(gfx, sOcarinaButtonGlyphs[btn], SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, &rect);
        x += glyph + gap;
    }

    return So2h_RestoreBlendState(gfx);
}

// ---------------------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------------------

/**
 * Draws an empty framed placeholder region: the frame plus a run of selectable-but-empty
 * cells, so the region reads as somewhere content will live rather than as dead space.
 * Phase 1 draws the frame only; the cells arrive with the nav work in phase 4/5.
 */
static Gfx* So2h_DrawPlaceholder(Gfx* gfx, So2hLayoutRegion regionId, u8 alpha) {
    const So2hRect* region = So2h_Layout_Region(regionId);
    f32 regionAlpha = So2h_Layout_RegionAlpha(regionId);
    u8 a = (u8)((f32)alpha * regionAlpha);
    So2hRect inner;
    So2hRect cellRect;
    So2hRect hatchRect;
    s16 cols;
    s16 rows;
    s16 col;
    s16 row;

    if ((a == 0) || (region->x1 <= region->x0) || (region->y1 <= region->y0)) {
        return gfx;
    }

    gfx = So2h_DrawWindow(gfx, region, a);
    So2h_Layout_Inset(region, So2h_WindowInset() * 1.6f, &inner);

    // A framed void reads as a bug; a frame full of empty slots reads as somewhere content
    // goes. The menubar gets one row of wide slots, everything else a grid.
    cols = (regionId == SO2H_REGION_MENUBAR) ? PLACEHOLDER_MENUBAR_COLS : PLACEHOLDER_COLS;
    rows = (regionId == SO2H_REGION_MENUBAR) ? 1 : PLACEHOLDER_ROWS;

    for (row = 0; row < rows; row++) {
        for (col = 0; col < cols; col++) {
            So2h_Layout_GridCell(&inner, cols, rows, col, row, So2h_CellGap(), &cellRect);
            if ((cellRect.x1 - cellRect.x0) < 2.0f) {
                continue;
            }
            gfx = So2h_DrawRecess(gfx, &cellRect, (u8)(a * 3 / 4));

            // Hatch fill inside the recess: an empty slot that is *meant* to be empty for
            // now, distinct from a live cell whose contents just failed to resolve.
            So2h_Layout_Inset(&cellRect, So2h_WindowInset(), &hatchRect);
            if ((hatchRect.x1 - hatchRect.x0) >= 2.0f) {
                gfx = So2h_SetupSkinMode(gfx, (u8)(a / 2));
                gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hSlotHatchTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX,
                                         &hatchRect);
                gfx = So2h_RestoreBlendState(gfx);
            }
        }
    }

    return gfx;
}

/**
 * The data screen. Not a placeholder skin - the reference builds it as a run of long thin
 * recessed slats grouped into columns, one slat per stat line, and that slat run is most of
 * what makes the region read as a readout instead of an empty panel. The stat *text* lands
 * in phase 5; the slats it will sit on are the geometry, so they go in now.
 */
static Gfx* So2h_DrawDataScreen(Gfx* gfx, u8 alpha) {
    const So2hRect* region = So2h_Layout_Region(SO2H_REGION_DATA_SCREEN);
    const So2hLayout* layout = So2h_Layout_Get();
    u8 a = (u8)((f32)alpha * So2h_Layout_RegionAlpha(SO2H_REGION_DATA_SCREEN));
    So2hRect inner;
    So2hRect group;
    So2hRect slat;
    s16 cols;
    s16 col;
    s16 row;

    if ((a == 0) || (region->x1 <= region->x0) || (region->y1 <= region->y0)) {
        return gfx;
    }

    gfx = So2h_DrawWindow(gfx, region, a);
    So2h_Layout_Inset(region, So2h_WindowInset() * 1.6f, &inner);

    cols = layout->dataCols;
    if (cols < 1) {
        cols = 1;
    }

    for (col = 0; col < cols; col++) {
        // Column groups are gapped generously so the slats read as two blocks of stats
        // rather than as one undifferentiated grid.
        So2h_Layout_GridCell(&inner, cols, 1, col, 0, So2h_CellGap() * 2.0f, &group);

        for (row = 0; row < DATA_SLAT_ROWS; row++) {
            f32 pad;

            So2h_Layout_GridCell(&group, 1, DATA_SLAT_ROWS, 0, row, 0.0f, &slat);
            // A slat is a thin bar, not a full-height cell: squeeze it vertically about its
            // own centre. This is the one place the sheet art is stretched along its long
            // axis on purpose.
            pad = (slat.y1 - slat.y0) * DATA_SLAT_SQUEEZE;
            slat.y0 += pad;
            slat.y1 -= pad;

            if ((slat.y1 - slat.y0) < 2.0f) {
                continue;
            }
            gfx = So2h_DrawRecess(gfx, &slat, a);
        }
    }

    return gfx;
}

void So2h_QuestBar_Draw(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    const So2hLayout* layout;
    const So2hRect* rightArm;
    const So2hRect* bottomArm;
    So2hRect cell;
    Gfx* gfx;
    s16 i;
    u8 alpha;
    f32 factor = So2h_PauseWindow_GetFactor();
    f32 slideX;
    f32 slideY;

    if (!So2h_PauseWindow_IsActive()) {
        return;
    }

    // One layout solve per frame, before anything reads a region rect.
    So2h_Layout_Update();
    layout = So2h_Layout_Get();
    rightArm = &layout->region[SO2H_REGION_RIGHT_ARM_PANEL];
    bottomArm = &layout->region[SO2H_REGION_BOTTOM_ARM_PANEL];

    alpha = (u8)(255.0f * factor);
    // Slide the two arms in from off-frame as the window settles. Both offsets are in screen
    // units and are taken from the arms' own sizes, so the slide is the same gesture at any
    // aspect instead of a fixed 320-relative distance.
    slideX = (1.0f - factor) * (rightArm->x1 - rightArm->x0);
    slideY = (1.0f - factor) * (bottomArm->y1 - bottomArm->y0);

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL39_Overlay(play->state.gfxCtx);

    gfx = OVERLAY_DISP;
    // Arm the budget guard: every So2h_* emitter checks against the overlay arena tail before
    // writing, so a full quest page degrades (drops the tail of the frame) instead of running
    // off the end of overlayBuffer and into the neighbouring pool buffers.
    sGfxBudgetEnd = (Gfx*)play->state.gfxCtx->overlay.d;

    gDPPipeSync(gfx++);
    // The pause pages render through a shrunken viewport, which leaves the scissor clipped
    // to the window rect (View_ApplyLetterbox scissors to view->viewport). Restore the full
    // screen before drawing anything outside the window.
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);

    // --- The two background arm panels ---
    gfx = So2h_DrawPanelR(gfx, rightArm, So2h_PanelCorner(), slideX, 0.0f, alpha);
    gfx = So2h_DrawPanelR(gfx, bottomArm, So2h_PanelCorner(), 0.0f, slideY, alpha);

    // Only populate the arms once they have arrived, so nothing streaks across the screen.
    if (factor > 0.98f) {
        // ----------------------------- Right arm: merged quest page ---------------------
        gfx = So2h_DrawHexWindow(gfx, alpha);
        gfx = So2h_DrawRadialIcons(gfx, alpha);
        // Window only, not a placeholder: both regions get their real contents drawn on top
        // by So2h_DrawCollectibleRows, so a placeholder's empty-cell grid would double-draw
        // a recess under every live cell.
        gfx = So2h_DrawRegionWindow(gfx, SO2H_REGION_HEART_WINDOW, alpha);
        gfx = So2h_DrawRegionWindow(gfx, SO2H_REGION_QUEST_GRID, alpha);
        gfx = So2h_DrawCollectibleRows(gfx, alpha);
        gfx = So2h_DrawSkulltulaCounters(gfx, play, alpha);

        // ----------------------------- Bottom arm: 22 songs -----------------------------
        // The song grid is its own sub-window. Staff lines used to run behind every row here;
        // in the reference the staff belongs to the preview only, and the grid is a plain
        // window full of note cells, so the per-row stave is gone.
        gfx = So2h_DrawRegionWindow(gfx, SO2H_REGION_SONG_GRID, alpha);

        for (i = 0; i < BOTTOM_ARM_CELLS; i++) {
            gfx = So2h_DrawSongCell(gfx, i, alpha);
        }

        gfx = So2h_DrawSongButtonStrip(gfx, pauseCtx, alpha);

        // ----------------------------- Empty regions ------------------------------------
        // Framed but unpopulated for now, so the new layout is visible end to end and any
        // region that lands in the wrong place is obvious rather than invisible.
        gfx = So2h_DrawDataScreen(gfx, alpha);
        gfx = So2h_DrawPlaceholder(gfx, SO2H_REGION_CONTENT_AREA, alpha);
        gfx = So2h_DrawPlaceholder(gfx, SO2H_REGION_MENUBAR, alpha);

        // ----------------------------- Cursor -------------------------------------------
        // The vanilla KaleidoScope_DrawCursor lives in the pause 3D space and would be trapped
        // inside the shrunken window, so while the cursor is in an arm the bar draws its own
        // 2D highlight instead and the caller suppresses the 3D one.
        if (So2h_QuestBar_IsCursorInBar(pauseCtx) && (pauseCtx->state == PAUSE_STATE_MAIN)) {
            s16 cx0;
            s16 cy0;
            s16 cx1;
            s16 cy1;
            u8 pulse;

            sBarCursorPhase += 0x400;
            pulse = (u8)(160.0f + (95.0f * Math_SinS(sBarCursorPhase)));

            if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) {
                So2h_RightArmCellRect(sBarCursorIndex, &cell);
            } else {
                So2h_BottomArmCellRect(sBarCursorIndex, &cell);
            }

            cx0 = So2h_Rnd(cell.x0);
            cy0 = So2h_Rnd(cell.y0);
            cx1 = So2h_Rnd(cell.x1);
            cy1 = So2h_Rnd(cell.y1);

            if ((cx1 > cx0) && (cy1 > cy0)) {
                // Four one-pixel edges instead of a filled quad, so the cell contents stay
                // readable underneath the highlight.
                gfx = So2h_FillRect(gfx, cx0, cy0, cx1, (s16)(cy0 + 1), 255, 255, 160, pulse);
                gfx = So2h_FillRect(gfx, cx0, (s16)(cy1 - 1), cx1, cy1, 255, 255, 160, pulse);
                gfx = So2h_FillRect(gfx, cx0, cy0, (s16)(cx0 + 1), cy1, 255, 255, 160, pulse);
                gfx = So2h_FillRect(gfx, (s16)(cx1 - 1), cy0, cx1, cy1, 255, 255, 160, pulse);
            }
        }
    }

    gDPPipeSync(gfx++);
    sGfxBudgetEnd = NULL;
    OVERLAY_DISP = gfx;

    CLOSE_DISPS(play->state.gfxCtx);
}
