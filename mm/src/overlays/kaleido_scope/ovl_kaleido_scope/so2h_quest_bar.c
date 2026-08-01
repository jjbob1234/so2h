/*
 * File: so2h_quest_bar.c
 * Overlay: ovl_kaleido_scope
 * Description: SO2H [Menu] backwards-L merged OOT + MM quest / song bar (see so2h_quest_bar.h)
 */

#include "z_kaleido_scope.h"
#include "so2h_quest_bar.h"
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
// Layout, N64 320x240 screen space. Horizontal values are fanned out through So2h_MapX so
// the bar hugs the real screen edges on non-4:3 displays.
//
// The bar holds the whole merged quest page: OOT's medallions / spiritual stones / Stone of
// Agony / Gerudo Card / skulltula tokens plus MM's boss remains / Bomber's Notebook / heart
// tracker / spider-house tokens, and all 22 songs from both games. Equipment (sword, shield,
// quiver, bomb bag, wallet) is deliberately NOT here - MM's equipment screen owns it.
// ---------------------------------------------------------------------------------------

#define BAR_SPLIT_X SO2H_WINDOW_RIGHT_X  // 192, left edge of the right arm
#define BAR_SPLIT_Y SO2H_WINDOW_BOTTOM_Y // 168, top edge of the bottom arm

// --- Right arm: the hexagon window -----------------------------------------------------
#define HEX_WIN_X0 196
#define HEX_WIN_Y0 4
#define HEX_WIN_X1 316
#define HEX_WIN_Y1 112
#define HEX_CX 256
#define HEX_CY 58

// The lifted OOT line-art, 2 tiles wide x 3 tall of 80x32 = 160x96 source, centered on
// (HEX_CX, HEX_CY) and scaled down. Kept slightly inside the medallion ring so the ring
// reads as sitting on the hexagon's vertices.
#define HEX_ART_W 108
#define HEX_ART_H 65

#define MEDALLION_RADIUS 33
#define MEDALLION_ICON 18
#define REMAINS_RADIUS 16
#define REMAINS_ICON 14

// --- Right arm: the three collectible rows below the window ----------------------------
#define ROW_A_Y 114 // Kokiri Emerald / Goron Ruby / Zora Sapphire / Stone of Agony
#define ROW_A_H 18
#define ROW_A_X 196
#define ROW_A_CELL_W 30

#define ROW_B_Y 134 // Gerudo Card / Bomber's Notebook / heart tracker
#define ROW_B_H 18
#define ROW_B_X 196
#define ROW_B_CELL_W 40

#define ROW_C_Y 154 // the two skulltula counters
#define ROW_C_H 14
#define ROW_C_X 198
#define ROW_C_CELL_W 58

#define ROW_ICON 16

// --- Bottom arm: 22 songs, 11 x 2, plus the button-sequence strip ----------------------
#define BOTTOM_ARM_COLS 11
#define BOTTOM_ARM_ROWS 2
#define BOTTOM_ARM_CELLS (BOTTOM_ARM_COLS * BOTTOM_ARM_ROWS) // 22

#define BOTTOM_ARM_INNER_X 6
#define BOTTOM_ARM_INNER_Y 180
#define BOTTOM_ARM_CELL_W 28
#define BOTTOM_ARM_CELL_H 17
#define BOTTOM_ARM_ROW_GAP 2
#define BOTTOM_ARM_NOTE 15

// 22 cells across 320 px leaves 28 px each, which is far too narrow for a per-cell run of
// up to eight ocarina button glyphs. They get their own full-width strip under the grid
// instead, showing the sequence for whichever song the cursor is on.
#define BTN_STRIP_Y 218
#define BTN_STRIP_H 10
#define BTN_GLYPH 10
#define BTN_GLYPH_GAP 2
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

// On-screen corner size of a drawn panel, in N64 320x240 space. The source corners are 52
// px of a 295 px wide authored panel; the bar's panels are far smaller than that, so the
// corners are scaled down rather than eating the whole panel.
#define PANEL_CORNER 12
// The hexagon sub-window is much smaller again, so it gets its own thinner frame.
#define HEX_PANEL_CORNER 8

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

// IA8 16x16 button glyphs, indexed by OCARINA_BTN_*. Same table z_kaleido_collect.c and
// z_message.c build for the vanilla ocarina staff.
static TexturePtr sOcarinaButtonGlyphs[5] = {
    gOcarinaATex, gOcarinaCDownTex, gOcarinaCRightTex, gOcarinaCLeftTex, gOcarinaCUpTex,
};

// A is blue, the C buttons are yellow, matching the in-game controller colours.
static u8 sOcarinaButtonColors[5][3] = {
    { 90, 160, 255 },  // A
    { 255, 220, 60 },  // C-Down
    { 255, 220, 60 },  // C-Right
    { 255, 220, 60 },  // C-Left
    { 255, 220, 60 },  // C-Up
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

/**
 * Maps an N64 320-wide x coordinate onto the widescreen-extended range, proportionally.
 * This is the same fan-out FB_DrawFromFramebufferRect applies to the pause background, so
 * the bar and the shrunken background stay aligned at any aspect ratio.
 */
static s16 So2h_MapX(s16 x) {
    f32 left = (f32)OTRGetRectDimensionFromLeftEdge(0);
    f32 right = (f32)OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH);

    return (s16)(left + ((right - left) * ((f32)x / (f32)SCREEN_WIDTH)));
}

static Gfx* So2h_FillRect(Gfx* gfx, s16 x0, s16 y0, s16 x1, s16 y1, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillWideRectangle(gfx++, So2h_MapX(x0), y0, So2h_MapX(x1), y1);

    return gfx;
}

/**
 * RGBA32 texture rectangle. No such helper exists in the codebase - z64interface.h only
 * declares IA8 / I8 / RGBA16 rect variants - so this mirrors Gfx_DrawTexRectIA8 with the
 * RGBA32 load and without the HUD-editor branches, which do not apply to pause menu art.
 */
static Gfx* So2h_DrawTexRectRGBA32(Gfx* gfx, TexturePtr texture, s16 textureWidth, s16 textureHeight, s16 rectLeft,
                                   s16 rectTop, s16 rectWidth, s16 rectHeight) {
    u16 dsdx = (u16)(((u32)textureWidth << 10) / (u32)rectWidth);
    u16 dtdy = (u16)(((u32)textureHeight << 10) / (u32)rectHeight);

    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, textureWidth, textureHeight, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    gSPWideTextureRectangle(gfx++, rectLeft << 2, rectTop << 2, (rectLeft + rectWidth) << 2,
                            (rectTop + rectHeight) << 2, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);

    return gfx;
}

/**
 * RGBA32 texture rectangle given N64-space edges instead of a width/height, with both edges
 * pushed through So2h_MapX. Passing a screen width next to a mapped left edge silently breaks
 * on widescreen for anything wide, so every piece of art in the bar goes through this.
 */
static Gfx* So2h_DrawSkinRect(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, s16 x0, s16 y0, s16 x1, s16 y1) {
    s16 left = So2h_MapX(x0);
    s16 right = So2h_MapX(x1);

    if ((right <= left) || (y1 <= y0)) {
        return gfx;
    }
    return So2h_DrawTexRectRGBA32(gfx, texture, texW, texH, left, y0, right - left, y1 - y0);
}

/**
 * IA8 counterpart of So2h_DrawSkinRect, for MM's ocarina button glyphs, the heart tracker and
 * the lifted OOT hexagon tiles (all IA8 - the colour comes from the prim colour).
 */
static Gfx* So2h_DrawIARect(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, s16 x0, s16 y0, s16 x1, s16 y1) {
    s16 left = So2h_MapX(x0);
    s16 right = So2h_MapX(x1);
    s16 w;
    s16 h;

    if ((right <= left) || (y1 <= y0)) {
        return gfx;
    }
    w = right - left;
    h = y1 - y0;

    return Gfx_DrawTexRectIA8(gfx, texture, texW, texH, left, y0, w, h, (u16)(((u32)texW << 10) / (u32)w),
                              (u16)(((u32)texH << 10) / (u32)h));
}

static Gfx* So2h_SetupSkinMode(Gfx* gfx, u8 alpha) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, alpha);

    return gfx;
}

static Gfx* So2h_SetupTintMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 alpha) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, alpha);

    return gfx;
}

static Gfx* So2h_SetupIAMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 alpha) {
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

static Gfx* So2h_DrawPanel(Gfx* gfx, s16 x0, s16 y0, s16 x1, s16 y1, u8 alpha) {
    return So2h_DrawPanelEx(gfx, x0, y0, x1, y1, PANEL_CORNER, alpha);
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
        gfx = Gfx_DrawTexRectIA8(gfx, (TexturePtr)sCounterTextures[digits[i]], 8, 16, So2h_MapX(x), topY, 6, 12,
                                 (8 << 10) / 6, (16 << 10) / 12);
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
 * On-screen rect of a right-arm cell. The hexagon cells are radial, the rest are rows, so
 * everything is computed here rather than by index arithmetic in the caller.
 */
static void So2h_RightArmCellRect(s16 index, s16* x0, s16* y0, s16* w, s16* h) {
    s16 dx;
    s16 dy;
    s16 slot;

    if ((index >= SO2H_CELL_MEDALLION_FIRST) && (index < SO2H_CELL_MEDALLION_FIRST + 6)) {
        slot = index - SO2H_CELL_MEDALLION_FIRST;
        So2h_RingOffset(sMedallionAngles[slot], MEDALLION_RADIUS, &dx, &dy);
        *w = MEDALLION_ICON;
        *h = MEDALLION_ICON;
        *x0 = HEX_CX + dx - (MEDALLION_ICON / 2);
        *y0 = HEX_CY + dy - (MEDALLION_ICON / 2);
        return;
    }

    if ((index >= SO2H_CELL_REMAINS_FIRST) && (index < SO2H_CELL_REMAINS_FIRST + 4)) {
        slot = index - SO2H_CELL_REMAINS_FIRST;
        So2h_RingOffset(sRemainsAngles[slot], REMAINS_RADIUS, &dx, &dy);
        *w = REMAINS_ICON;
        *h = REMAINS_ICON;
        *x0 = HEX_CX + dx - (REMAINS_ICON / 2);
        *y0 = HEX_CY + dy - (REMAINS_ICON / 2);
        return;
    }

    if ((index >= SO2H_CELL_STONE_FIRST) && (index <= SO2H_CELL_STONE_OF_AGONY)) {
        slot = index - SO2H_CELL_STONE_FIRST;
        *x0 = ROW_A_X + (slot * ROW_A_CELL_W);
        *y0 = ROW_A_Y;
        *w = ROW_A_CELL_W - 2;
        *h = ROW_A_H;
        return;
    }

    if ((index >= SO2H_CELL_GERUDO_CARD) && (index <= SO2H_CELL_HEART_TRACKER)) {
        slot = index - SO2H_CELL_GERUDO_CARD;
        *x0 = ROW_B_X + (slot * ROW_B_CELL_W);
        *y0 = ROW_B_Y;
        *w = ROW_B_CELL_W - 2;
        *h = ROW_B_H;
        return;
    }

    // The two skulltula counters.
    slot = index - SO2H_CELL_SKULLTULA_OOT;
    *x0 = ROW_C_X + (slot * ROW_C_CELL_W);
    *y0 = ROW_C_Y;
    *w = ROW_C_CELL_W - 2;
    *h = ROW_C_H;
}

static void So2h_BottomArmCellRect(s16 index, s16* x0, s16* y0) {
    *x0 = BOTTOM_ARM_INNER_X + ((index % BOTTOM_ARM_COLS) * BOTTOM_ARM_CELL_W);
    *y0 = BOTTOM_ARM_INNER_Y + ((index / BOTTOM_ARM_COLS) * (BOTTOM_ARM_CELL_H + BOTTOM_ARM_ROW_GAP));
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
        So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_BOTTOM, BOTTOM_ARM_COLS - 1);
        return;
    }

    sBarCursorIndex = target;
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

s32 So2h_QuestBar_UpdateCursor(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
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
    col = sBarCursorIndex % BOTTOM_ARM_COLS;
    row = sBarCursorIndex / BOTTOM_ARM_COLS;

    if (pauseCtx->stickAdjX < -30) {
        if (col > 0) {
            sBarCursorIndex--;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
    } else if (pauseCtx->stickAdjX > 30) {
        if (col < (BOTTOM_ARM_COLS - 1)) {
            sBarCursorIndex++;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        } else {
            // Rightmost song column steps up into the right arm's bottom row.
            So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_RIGHT, SO2H_CELL_SKULLTULA_HOUSE);
        }
    } else if (pauseCtx->stickAdjY > 30) {
        if (row > 0) {
            sBarCursorIndex -= BOTTOM_ARM_COLS;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        } else {
            // Top row of the bottom arm returns to the page itself.
            KaleidoScope_MoveCursorFromSpecialPos(play);
            sBarCursorIndex = 0;
        }
    } else if (pauseCtx->stickAdjY < -30) {
        if (row < (BOTTOM_ARM_ROWS - 1)) {
            sBarCursorIndex += BOTTOM_ARM_COLS;
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
    s16 artX0 = HEX_CX - (HEX_ART_W / 2);
    s16 artY0 = HEX_CY - (HEX_ART_H / 2);
    s16 tileW = HEX_ART_W / 2;
    s16 tileH = HEX_ART_H / 3;
    s16 i;

    gfx = So2h_DrawPanelEx(gfx, HEX_WIN_X0, HEX_WIN_Y0, HEX_WIN_X1, HEX_WIN_Y1, HEX_PANEL_CORNER, alpha);

    gfx = So2h_SetupIAMode(gfx, sHexArtColor[0], sHexArtColor[1], sHexArtColor[2], alpha);
    for (i = 0; i < 6; i++) {
        const char* path = OotQuestArt_GetPath(sHexTileArt[i]);
        s16 tx = artX0 + ((i % 2) * tileW);
        s16 ty = artY0 + ((i / 2) * tileH);

        if (path == NULL) {
            continue;
        }
        gfx = So2h_DrawIARect(gfx, (TexturePtr)path, OOT_QUEST_ART_HEX_TILE_W, OOT_QUEST_ART_HEX_TILE_H, tx, ty,
                              tx + tileW, ty + tileH);
    }

    return So2h_RestoreBlendState(gfx);
}

/**
 * One collectible icon, drawn owned (full colour) or unowned (heavily dimmed, so the slot
 * still reads as a place a thing goes). Every icon here is real art - the old flat colour
 * swatches are gone.
 */
static Gfx* So2h_DrawCollectible(Gfx* gfx, TexturePtr tex, s16 texDim, s16 x0, s16 y0, s16 size, s32 owned, u8 alpha) {
    if (tex == NULL) {
        // No art available (OOT content not merged in): fall back to the sheet's recessed
        // slot so the layout doesn't collapse into a hole.
        gfx = So2h_SetupSkinMode(gfx, (u8)(alpha / 3));
        gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hSlotDarkTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, x0, y0, x0 + size,
                                y0 + size);
        return So2h_RestoreBlendState(gfx);
    }

    if (owned) {
        gfx = So2h_SetupSkinMode(gfx, alpha);
    } else {
        gfx = So2h_SetupTintMode(gfx, sCellUnownedColor[0] * 2, sCellUnownedColor[1] * 2, sCellUnownedColor[2] * 2,
                                 (u8)(alpha / 3));
    }
    gfx = So2h_DrawSkinRect(gfx, tex, texDim, texDim, x0, y0, x0 + size, y0 + size);

    return So2h_RestoreBlendState(gfx);
}

/**
 * The two concentric rings inside the hexagon window: OOT's six medallions on the hexagon's
 * vertices, MM's four boss remains on an inner ring sharing the same center.
 */
static Gfx* So2h_DrawRadialIcons(Gfx* gfx, u8 alpha) {
    s16 i;
    s16 x0;
    s16 y0;
    s16 w;
    s16 h;

    for (i = 0; i < 6; i++) {
        const char* path = OotQuestArt_GetPath(sMedallionArt[i]);

        So2h_RightArmCellRect(SO2H_CELL_MEDALLION_FIRST + i, &x0, &y0, &w, &h);
        gfx = So2h_DrawCollectible(gfx, (TexturePtr)path, OOT_QUEST_ART_ICON_DIM, x0, y0, w,
                                   So2h_OotQuestBit(OOT_QUEST_MEDALLION_FOREST + i), alpha);
    }

    for (i = 0; i < 4; i++) {
        So2h_RightArmCellRect(SO2H_CELL_REMAINS_FIRST + i, &x0, &y0, &w, &h);
        gfx = So2h_DrawCollectible(gfx, (TexturePtr)gItemIcons[ITEM_REMAINS_ODOLWA + i], ITEM_ICON_TEX, x0, y0, w,
                                   CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA + i) != 0, alpha);
    }

    return gfx;
}

/**
 * Rows A and B: the spiritual stones, Stone of Agony, Gerudo Card, Bomber's Notebook and the
 * heart tracker, each in a recessed sheet slot.
 */
static Gfx* So2h_DrawCollectibleRows(Gfx* gfx, u8 alpha) {
    s16 i;
    s16 x0;
    s16 y0;
    s16 w;
    s16 h;
    s16 iconX;
    s16 iconY;
    s16 heartPieces;

    for (i = SO2H_CELL_STONE_FIRST; i <= SO2H_CELL_HEART_TRACKER; i++) {
        So2h_RightArmCellRect(i, &x0, &y0, &w, &h);

        gfx = So2h_SetupSkinMode(gfx, alpha);
        gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hSlotDarkTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, x0, y0, x0 + w,
                                y0 + h);
        gfx = So2h_RestoreBlendState(gfx);

        iconX = x0 + ((w - ROW_ICON) / 2);
        iconY = y0 + ((h - ROW_ICON) / 2);

        if ((i >= SO2H_CELL_STONE_FIRST) && (i < SO2H_CELL_STONE_OF_AGONY)) {
            s16 stone = i - SO2H_CELL_STONE_FIRST;

            gfx = So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(sStoneArt[stone]), OOT_QUEST_ART_ICON_DIM,
                                       iconX, iconY, ROW_ICON,
                                       So2h_OotQuestBit(OOT_QUEST_KOKIRI_EMERALD + stone), alpha);
        } else if (i == SO2H_CELL_STONE_OF_AGONY) {
            gfx = So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(OOT_QUEST_ART_STONE_OF_AGONY),
                                       OOT_QUEST_ART_ICON_DIM, iconX, iconY, ROW_ICON,
                                       So2h_OotQuestBit(OOT_QUEST_STONE_OF_AGONY), alpha);
        } else if (i == SO2H_CELL_GERUDO_CARD) {
            gfx = So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(OOT_QUEST_ART_GERUDO_CARD),
                                       OOT_QUEST_ART_ICON_DIM, iconX, iconY, ROW_ICON,
                                       So2h_OotQuestBit(OOT_QUEST_GERUDO_CARD), alpha);
        } else if (i == SO2H_CELL_BOMBERS_NOTEBOOK) {
            gfx = So2h_DrawCollectible(gfx, (TexturePtr)gItemIconBombersNotebookTex, ITEM_ICON_TEX, iconX, iconY,
                                       ROW_ICON, CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK) != 0, alpha);
        } else {
            // Heart tracker: MM's own 48x48 IA8 heart-piece-fill icon, the same
            // gItemIcons[0x7A + count] the vanilla quest page uses, plus the heart total.
            heartPieces = (s16)((GET_SAVE_INVENTORY_QUEST_ITEMS & 0xF0000000) >> QUEST_HEART_PIECE_COUNT);

            gfx = So2h_SetupIAMode(gfx, 255, 255, 255, alpha);
            gfx = So2h_DrawIARect(gfx, (TexturePtr)gItemIcons[0x7A + heartPieces], HEART_TRACKER_TEX,
                                  HEART_TRACKER_TEX, iconX - 4, iconY, iconX - 4 + ROW_ICON, iconY + ROW_ICON);
            gfx = So2h_RestoreBlendState(gfx);

            gfx = So2h_DrawCount(gfx, (s16)(gSaveContext.save.saveInfo.playerData.healthCapacity / 16), x0 + w - 3,
                                 y0 + 3, alpha);
            gfx = So2h_RestoreBlendState(gfx);
        }
    }

    return gfx;
}

/**
 * Row C: the two skulltula counters. The first is OOT's Gold Skulltula token total, the
 * second the current (or last visited) MM spider house. Both use MM's own 24x24 skulltula
 * icon from icon_item_24_static_yar and MM's HUD counter digits - no OOT art needed.
 */
static Gfx* So2h_DrawSkulltulaCounters(Gfx* gfx, PlayState* play, u8 alpha) {
    s16 counts[2];
    s16 i;
    s16 x0;
    s16 y0;
    s16 w;
    s16 h;

    if ((play->sceneId == SCENE_KINSTA1) || (play->sceneId == SCENE_KINDAN2)) {
        sLastSpiderHouseScene = play->sceneId;
    }

    counts[0] = gSaveContext.save.shipSaveInfo.so2h.oot.inventory.gsTokens;
    counts[1] = Inventory_GetSkullTokenCount(sLastSpiderHouseScene);

    for (i = 0; i < 2; i++) {
        So2h_RightArmCellRect(SO2H_CELL_SKULLTULA_OOT + i, &x0, &y0, &w, &h);

        gfx = So2h_SetupSkinMode(gfx, alpha);
        gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hSlotDarkTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, x0, y0, x0 + w,
                                y0 + h);
        gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gQuestIconGoldSkulltulaTex, QUEST_ICON_TEX, QUEST_ICON_TEX, x0 + 2,
                                y0 + 1, x0 + 2 + (h - 2), y0 + 1 + (h - 2));
        gfx = So2h_RestoreBlendState(gfx);

        gfx = So2h_DrawCount(gfx, counts[i], x0 + w - 3, y0 + 1, alpha);
        gfx = So2h_RestoreBlendState(gfx);
    }

    return gfx;
}

// ---------------------------------------------------------------------------------------
// Bottom arm
// ---------------------------------------------------------------------------------------

static Gfx* So2h_DrawSongCell(Gfx* gfx, s16 index, u8 alpha) {
    So2hSongInfo* song = &sSongs[index];
    s16 x0;
    s16 y0;
    s16 noteX;
    s16 noteY;
    s32 owned = So2h_SongOwned(index);

    So2h_BottomArmCellRect(index, &x0, &y0);

    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hSlotDarkTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, x0, y0,
                            x0 + BOTTOM_ARM_CELL_W - 2, y0 + BOTTOM_ARM_CELL_H);

    noteX = x0 + ((BOTTOM_ARM_CELL_W - 2 - BOTTOM_ARM_NOTE) / 2);
    noteY = y0 + ((BOTTOM_ARM_CELL_H - BOTTOM_ARM_NOTE) / 2);

    if (owned) {
        gfx = So2h_SetupTintMode(gfx, song->color[0], song->color[1], song->color[2], alpha);
        gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hNoteWhiteTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, noteX, noteY,
                                noteX + BOTTOM_ARM_NOTE, noteY + BOTTOM_ARM_NOTE);
    } else {
        gfx = So2h_SetupTintMode(gfx, 255, 255, 255, (u8)(alpha * 3 / 4));
        gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hNoteLockedTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, noteX, noteY,
                                noteX + BOTTOM_ARM_NOTE, noteY + BOTTOM_ARM_NOTE);
    }

    return So2h_RestoreBlendState(gfx);
}

/**
 * The button-sequence strip under the song grid. 22 cells across 320 px leaves 28 px each,
 * nowhere near enough for eight 16x16 button glyphs per cell, so the sequence is shown once,
 * full size, for whichever song the cursor is on.
 *
 * Sequences come from MM's own gOcarinaSongButtons wherever MM knows the song; the six OOT
 * warp songs, which MM's ocarina code never learns, come from sWarpSongButtons.
 */
static Gfx* So2h_DrawSongButtonStrip(Gfx* gfx, PauseContext* pauseCtx, u8 alpha) {
    u8 buttons[WARP_SONG_MAX_BUTTONS];
    s16 numButtons = 0;
    So2hSongInfo* song;
    s16 songIndex;
    s16 totalW;
    s16 x;
    s16 i;

    // Clef flourish, always present so the strip never reads as an empty gap.
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hStaffClefTex, SKIN_CLEF_W_TEX, SKIN_CLEF_H_TEX, BOTTOM_ARM_INNER_X,
                            BTN_STRIP_Y - 1, BOTTOM_ARM_INNER_X + 8, BTN_STRIP_Y + BTN_STRIP_H + 1);
    gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hStaffLinesTex, SKIN_STAFF_TEX, SKIN_STAFF_TEX,
                            BOTTOM_ARM_INNER_X + 8, BTN_STRIP_Y + 2,
                            BOTTOM_ARM_INNER_X + (BOTTOM_ARM_COLS * BOTTOM_ARM_CELL_W) - 2,
                            BTN_STRIP_Y + BTN_STRIP_H - 2);
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

    totalW = (numButtons * BTN_GLYPH) + ((numButtons - 1) * BTN_GLYPH_GAP);
    x = ((SCREEN_WIDTH - totalW) / 2);

    for (i = 0; i < numButtons; i++) {
        u8 btn = buttons[i];

        if (btn >= 5) {
            // OCARINA_BTN_C_RIGHT_OR_C_LEFT / OCARINA_BTN_INVALID: nothing sensible to draw.
            x += BTN_GLYPH + BTN_GLYPH_GAP;
            continue;
        }
        gfx = So2h_SetupIAMode(gfx, sOcarinaButtonColors[btn][0], sOcarinaButtonColors[btn][1],
                               sOcarinaButtonColors[btn][2], alpha);
        gfx = So2h_DrawIARect(gfx, sOcarinaButtonGlyphs[btn], BTN_TEX, BTN_TEX, x, BTN_STRIP_Y, x + BTN_GLYPH,
                              BTN_STRIP_Y + BTN_GLYPH);
        x += BTN_GLYPH + BTN_GLYPH_GAP;
    }

    return So2h_RestoreBlendState(gfx);
}

// ---------------------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------------------

void So2h_QuestBar_Draw(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Gfx* gfx;
    s16 i;
    s16 cellX;
    s16 cellY;
    s16 w;
    s16 h;
    u8 alpha;
    f32 factor = So2h_PauseWindow_GetFactor();
    s16 slideY;
    s16 slideX;

    if (!So2h_PauseWindow_IsActive()) {
        return;
    }

    alpha = (u8)(255.0f * factor);
    // Slide the two arms in from off-frame as the window settles.
    slideX = (s16)((1.0f - factor) * (SCREEN_WIDTH - BAR_SPLIT_X));
    slideY = (s16)((1.0f - factor) * (SCREEN_HEIGHT - BAR_SPLIT_Y));

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL39_Overlay(play->state.gfxCtx);

    gfx = OVERLAY_DISP;

    gDPPipeSync(gfx++);
    // The pause pages render through a shrunken viewport, which leaves the scissor clipped
    // to the window rect (View_ApplyLetterbox scissors to view->viewport). Restore the full
    // screen before drawing anything outside the window.
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);

    // --- Right arm panel (collectibles) ---
    gfx = So2h_DrawPanel(gfx, BAR_SPLIT_X + slideX, 0, SCREEN_WIDTH + slideX, BAR_SPLIT_Y, alpha);
    // --- Bottom arm panel (songs). Now the full screen width: the old bottom-right song
    //     preview window is gone, the 22 song cells need every pixel of it. ---
    gfx = So2h_DrawPanel(gfx, 0, BAR_SPLIT_Y + slideY, SCREEN_WIDTH, SCREEN_HEIGHT + slideY, alpha);

    // Only populate the arms once they have arrived, so nothing streaks across the screen.
    if (factor > 0.98f) {
        // ----------------------------- Right arm: merged quest page ---------------------
        gfx = So2h_DrawHexWindow(gfx, alpha);
        gfx = So2h_DrawRadialIcons(gfx, alpha);
        gfx = So2h_DrawCollectibleRows(gfx, alpha);
        gfx = So2h_DrawSkulltulaCounters(gfx, play, alpha);

        // ----------------------------- Bottom arm: 22 songs -----------------------------
        // Staff lines run the full width of each row, so each row reads as one stave instead
        // of eleven unrelated boxes.
        gfx = So2h_SetupSkinMode(gfx, alpha);
        for (i = 0; i < BOTTOM_ARM_ROWS; i++) {
            s16 staffY = BOTTOM_ARM_INNER_Y + (i * (BOTTOM_ARM_CELL_H + BOTTOM_ARM_ROW_GAP));

            gfx = So2h_DrawSkinRect(gfx, (TexturePtr)gSo2hStaffLinesTex, SKIN_STAFF_TEX, SKIN_STAFF_TEX,
                                    BOTTOM_ARM_INNER_X - 2, staffY + 3,
                                    BOTTOM_ARM_INNER_X + (BOTTOM_ARM_COLS * BOTTOM_ARM_CELL_W),
                                    staffY + BOTTOM_ARM_CELL_H - 3);
        }
        gfx = So2h_RestoreBlendState(gfx);

        for (i = 0; i < BOTTOM_ARM_CELLS; i++) {
            gfx = So2h_DrawSongCell(gfx, i, alpha);
        }

        gfx = So2h_DrawSongButtonStrip(gfx, pauseCtx, alpha);

        // ----------------------------- Cursor -------------------------------------------
        // The vanilla KaleidoScope_DrawCursor lives in the pause 3D space and would be trapped
        // inside the shrunken window, so while the cursor is in an arm the bar draws its own
        // 2D highlight instead and the caller suppresses the 3D one.
        if (So2h_QuestBar_IsCursorInBar(pauseCtx) && (pauseCtx->state == PAUSE_STATE_MAIN)) {
            u8 pulse;

            sBarCursorPhase += 0x400;
            pulse = (u8)(160.0f + (95.0f * Math_SinS(sBarCursorPhase)));

            if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) {
                So2h_RightArmCellRect(sBarCursorIndex, &cellX, &cellY, &w, &h);
            } else {
                So2h_BottomArmCellRect(sBarCursorIndex, &cellX, &cellY);
                w = BOTTOM_ARM_CELL_W - 2;
                h = BOTTOM_ARM_CELL_H;
            }

            // Four one-pixel edges instead of a filled quad, so the cell contents stay readable.
            gfx = So2h_FillRect(gfx, cellX, cellY, cellX + w, cellY + 1, 255, 255, 160, pulse);
            gfx = So2h_FillRect(gfx, cellX, cellY + h - 1, cellX + w, cellY + h, 255, 255, 160, pulse);
            gfx = So2h_FillRect(gfx, cellX, cellY, cellX + 1, cellY + h, 255, 255, 160, pulse);
            gfx = So2h_FillRect(gfx, cellX + w - 1, cellY, cellX + w, cellY + h, 255, 255, 160, pulse);
        }
    }

    gDPPipeSync(gfx++);
    OVERLAY_DISP = gfx;

    CLOSE_DISPS(play->state.gfxCtx);
}
