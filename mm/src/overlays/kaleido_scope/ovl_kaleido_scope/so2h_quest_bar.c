/*
 * File: so2h_quest_bar.c
 * Overlay: ovl_kaleido_scope
 * Description: SO2H [Menu] backwards-L merged OOT + MM quest / song bar (see so2h_quest_bar.h)
 */

#include "z_kaleido_scope.h"
#include "so2h_quest_bar.h"
#include "2s2h/Menu/so2h_pause_window.h"
#include "2s2h/Menu/so2h_pause_menu.h"
#include "2s2h/Menu/so2h_ui.h"
#include "2s2h/Menu/so2h_ui_scene_pause.h"
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
// Contents.
//
// This file no longer lays anything out. so2h_ui owns every rect on the pause page, solved
// from the generated descriptor table (mm/2s2h/Menu/so2h_ui_scene_pause.c, produced from
// tools/scenes/pause.py); what lives here is the CONTENT that gets painted into those rects -
// the save-file reads, the icon choices and the ownership rules - attached to node ids by
// So2h_QuestBar_Bind() at the bottom of the file.
//
// So2h_QuestBar_Draw, the two arm cell-index spaces, the neighbour table and the whole
// so2h_quest_layout.c solver are gone with the hand-rolled bar they belonged to. Adding a new
// readout is now one bind line plus one callback, with nothing in the layout, clipping,
// navigation or asset code touched - which is the acceptance test the engine was built to.
//
// The page holds the merged quest content: OOT's spiritual stones / Stone of Agony / Gerudo
// Card / skulltula tokens plus MM's Bomber's Notebook and heart tracker, and the songs.
// Equipment (sword, shield, quiver, bomb bag, wallet) is deliberately NOT here - MM's
// equipment screen owns it. The medallions and boss remains belong to the hexagon block,
// which is its own consumer and lands next.
//
// Rects arrive as So2hUiRect in screen space - the widescreen-extended N64 rect space that
// gSPWideTextureRectangle consumes. Two rules follow and both are load-bearing:
//
//   1. Nothing here may hard-code a 320-relative x coordinate.
//   2. Nothing may push a coordinate through a widescreen fan-out. The engine already applied
//      it, exactly once. Double-mapping is the easiest way to silently break every wide rect
//      on a non-4:3 display.
// ---------------------------------------------------------------------------------------

// The 22 songs are a fixed set; how many of them the page shows is the scene's business.
#define SO2H_SONG_COUNT 22
#define BOTTOM_ARM_CELLS SO2H_SONG_COUNT

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

static u8 sStoneArt[3] = {
    OOT_QUEST_ART_STONE_KOKIRI,
    OOT_QUEST_ART_STONE_GORON,
    OOT_QUEST_ART_STONE_ZORA,
};
// The 2x3 hexagon block, in row-major order (top-left, top-right, mid-left, ...).

// Ring angles, degrees, screen-space (y grows downward so the sine is subtracted). The
// medallions sit on the hexagon's six vertices starting at the top and running clockwise in
// OOT's own order; the four boss remains sit on a concentric inner ring, offset 45 degrees so
// they land in the hexagon's gaps and can never collide with a medallion.

 // Odolwa, Goht, Gyorg, Twinmold

static u8 sCellUnownedColor[3] = { 52, 48, 44 };
// The lifted OOT hexagon tiles are IA8, i.e. monochrome - this is the tint they take.

// ---------------------------------------------------------------------------------------
// State. The arm-internal cursor index deliberately lives here and not in PauseContext, so
// no save struct or [PAUSE_PAGE_MAX] array has to grow for it.
// ---------------------------------------------------------------------------------------

static s16 sBarCursorIndex = 0;

// Remembers which spider house the player was last standing in, so the second skulltula
// counter still shows something meaningful once they have left it.

void So2h_QuestBar_Reset(void) {
    sBarCursorIndex = 0;

    // Cheapest place to guarantee the content binder is installed: Reset runs every frame the
    // pause menu is off or still opening, and So2h_PauseMenu_SetBindHook is a no-op once the
    // pointer already matches. Doing it here rather than at a call site in
    // z_kaleido_scope_NES.c keeps the overlay's diff to the four calls it already had.
    So2h_PauseMenu_SetBindHook(So2h_QuestBar_Bind);
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

static s32 So2h_GfxRoom(Gfx* gfx, s32 need) {
    // The tail is the engine's now - content only ever emits from inside So2h_Ui_Draw, and
    // that is exactly the window in which So2h_Ui_SetGfxBudget is armed. Keeping one owner
    // for the arena tail means content cannot disagree with the engine about where it ends.
    Gfx* end = So2h_Ui_GetGfxBudget();

    return (end == NULL) || ((gfx + need + SO2H_GFX_RESERVE) < end);
}

/**
 * Rounds a screen-space float to the integer the rect commands take. Layout maths runs in
 * f32 all the way down and only collapses to integers here, so a region and the art inside
 * it can never disagree by a pixel because they rounded at different times.
 */
static s16 So2h_Rnd(f32 v) {
    return (s16)((v < 0.0f) ? (v - 0.5f) : (v + 0.5f));
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
static Gfx* So2h_DrawSkinRectR(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, const So2hUiRect* r) {
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
static Gfx* So2h_DrawIARectR(Gfx* gfx, TexturePtr texture, s16 texW, s16 texH, const So2hUiRect* r) {
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
 * A recessed slot. Used square for cells and stretched along x for the data-screen slats.
 */
static Gfx* So2h_DrawRecess(Gfx* gfx, const So2hUiRect* r, u8 alpha) {
    if (alpha == 0) {
        return gfx;
    }
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hSlotRecessTex, SKIN_CELL_TEX, SKIN_CELL_TEX, r);

    return So2h_RestoreBlendState(gfx);
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
 * One collectible icon, drawn owned (full colour) or unowned (heavily dimmed, so the slot
 * still reads as a place a thing goes). Every icon here is real art - the old flat colour
 * swatches are gone.
 */
static Gfx* So2h_DrawCollectible(Gfx* gfx, TexturePtr tex, s16 texDim, const So2hUiRect* rect, s32 owned, u8 alpha) {
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

/**
 * Centred square inside a cell, `frac` of the cell's shorter axis. Icons are square art and
 * cells are not, so nothing may just fill its cell or every icon stretches.
 */
static void So2h_IconInRect(const So2hUiRect* cell, f32 frac, So2hUiRect* out) {
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

// ---------------------------------------------------------------------------------------
// Cursor
//
// There is no neighbour table any more. so2h_ui solves a move geometrically from the solved
// rects, so a cell added to tools/scenes/pause.py is navigable the moment it exists - which
// is the whole acceptance test for the engine. sRightArmNav, So2h_QuestBar_Navigate and the
// two arm cell-index spaces are gone with the hand-rolled bar they described.
//
// The boundary contract with z_kaleido_scope_NES.c is unchanged: a zero return from
// So2h_Ui_Navigate means the cursor walked off the edge of the tree, and that is handed back
// to vanilla rather than swallowed.
// ---------------------------------------------------------------------------------------
s32 So2h_QuestBar_UpdateCursor(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s32 moved;

    // The controller draws the focus highlight and cannot see the two special positions that
    // mean "in the menu" - they are overlay-private - so the answer is pushed to it here.
    So2h_PauseMenu_SetCursorInMenu(So2h_QuestBar_IsCursorInBar(pauseCtx));

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
    //   - push right again while parked on the R (page right) marker -> the menu
    //   - push down while parked on either page marker               -> the menu
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

    moved = false;

    if (pauseCtx->stickAdjX < -30) {
        moved = So2h_Ui_Navigate(SO2H_UI_LEFT);
        if (!moved) {
            So2h_QuestBar_Exit(play, PAUSE_CURSOR_PAGE_RIGHT);
            return true;
        }
    } else if (pauseCtx->stickAdjX > 30) {
        moved = So2h_Ui_Navigate(SO2H_UI_RIGHT);
    } else if (pauseCtx->stickAdjY > 30) {
        moved = So2h_Ui_Navigate(SO2H_UI_UP);
        if (!moved) {
            // Off the top of the menu is back to the page itself.
            KaleidoScope_MoveCursorFromSpecialPos(play);
            sBarCursorIndex = 0;
            return true;
        }
    } else if (pauseCtx->stickAdjY < -30) {
        moved = So2h_Ui_Navigate(SO2H_UI_DOWN);
    }

    if (moved) {
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Content
//
// Everything below is a So2hUiDrawFn bound to a node id by So2h_QuestBar_Bind(). None of it
// computes geometry: a callback is handed the node's own solved rect and may read nothing
// else. That is the whole reason the bar can be re-laid-out in tools/scenes/pause.py without
// this file changing - and the reason adding a new readout is one bind line plus one
// callback, which is the acceptance test for the engine.
//
// Binding is a RUNTIME hand-off rather than a `draw` column in the generated table because
// the table is static const in mm/2s2h/Menu and must not name symbols that live out here in
// the kaleido overlay.
// ---------------------------------------------------------------------------------------

// How much of its slot an icon fills, leaving the slot's bevel visible as a rim.
#define CONTENT_SLOT_ICON_FRAC 0.80f
#define CONTENT_NOTEBOOK_ICON_FRAC 0.68f
#define CONTENT_HEART_ICON_FRAC 0.66f
#define CONTENT_SONG_CROSS_FRAC 0.92f

// The six quest slots, in scene order: SO2H_PAUSE_QUEST_SLOT00..02 then 10..12.
#define CONTENT_QUEST_SLOT_COUNT 6
// Songs declared on the visible page. 12..21 and the page arrows are a follow-up item.
#define CONTENT_SONG_PAGE_CELLS 12

/**
 * The node's resolved alpha, including its reveal. A draw callback is not handed one - the
 * signature deliberately carries geometry only - so content reads it back off the solved
 * node, which is the same value the engine used for the node's own art this frame.
 */
static u8 So2h_ContentAlpha(So2hUiId node) {
    const So2hUiNode* n = So2h_Ui_GetNode(node);
    f32 a;

    if (n == NULL) {
        return 0;
    }
    a = n->alpha;
    if (a <= 0.0f) {
        return 0;
    }
    if (a > 1.0f) {
        a = 1.0f;
    }
    return (u8)(a * 255.0f);
}

/**
 * The six merged quest slots.
 *   row 0: Kokiri Emerald, Goron Ruby, Zora Sapphire
 *   row 1: Stone of Agony, Gerudo Card, Gold Skulltula token count
 * The medallions are not here - they belong to the hexagon block, which is its own consumer.
 */
static Gfx* So2h_Content_QuestSlot(Gfx* gfx, So2hUiId node, const So2hUiRect* rect, void* user) {
    s32 slot = (s32)node - (s32)SO2H_PAUSE_QUEST_SLOT00;
    u8 alpha = So2h_ContentAlpha(node);
    So2hUiRect icon;

    (void)user;

    if ((alpha == 0) || (slot < 0) || (slot >= CONTENT_QUEST_SLOT_COUNT)) {
        return gfx;
    }

    So2h_IconInRect(rect, CONTENT_SLOT_ICON_FRAC, &icon);

    if (slot < 3) {
        return So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(sStoneArt[slot]), OOT_QUEST_ART_ICON_DIM,
                                    &icon, So2h_OotQuestBit((u8)(OOT_QUEST_KOKIRI_EMERALD + slot)), alpha);
    }
    if (slot == 3) {
        return So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(OOT_QUEST_ART_STONE_OF_AGONY),
                                    OOT_QUEST_ART_ICON_DIM, &icon, So2h_OotQuestBit(OOT_QUEST_STONE_OF_AGONY), alpha);
    }
    if (slot == 4) {
        return So2h_DrawCollectible(gfx, (TexturePtr)OotQuestArt_GetPath(OOT_QUEST_ART_GERUDO_CARD),
                                    OOT_QUEST_ART_ICON_DIM, &icon, So2h_OotQuestBit(OOT_QUEST_GERUDO_CARD), alpha);
    }

    // Gold Skulltula tokens: MM's own 24x24 skulltula icon plus MM's HUD counter digits, so
    // no OOT art is needed for the count. The icon sits left so the digits get the right half.
    icon.x1 = icon.x0 + (icon.y1 - icon.y0);
    gfx = So2h_SetupSkinMode(gfx, alpha);
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gQuestIconGoldSkulltulaTex, QUEST_ICON_TEX, QUEST_ICON_TEX, &icon);
    gfx = So2h_RestoreBlendState(gfx);
    gfx = So2h_DrawCount(gfx, (s16)gSaveContext.save.shipSaveInfo.so2h.oot.inventory.gsTokens,
                         (s16)(So2h_Rnd(rect->x1) - 2), (s16)(So2h_Rnd(rect->y1) - 14), alpha);

    return So2h_RestoreBlendState(gfx);
}

/**
 * The heart tracker: MM's own 48x48 IA8 heart-piece-fill icon, the same gItemIcons[0x7A + n]
 * the vanilla quest page uses, plus the heart total.
 */
static Gfx* So2h_Content_Heart(Gfx* gfx, So2hUiId node, const So2hUiRect* rect, void* user) {
    u8 alpha = So2h_ContentAlpha(node);
    So2hUiRect icon;
    s16 heartPieces;

    (void)user;

    if (alpha == 0) {
        return gfx;
    }

    So2h_IconInRect(rect, CONTENT_HEART_ICON_FRAC, &icon);

    heartPieces = (s16)((GET_SAVE_INVENTORY_QUEST_ITEMS & 0xF0000000) >> QUEST_HEART_PIECE_COUNT);

    gfx = So2h_SetupIAMode(gfx, 255, 255, 255, alpha);
    gfx = So2h_DrawIARectR(gfx, (TexturePtr)gItemIcons[0x7A + heartPieces], HEART_TRACKER_TEX, HEART_TRACKER_TEX,
                           &icon);
    gfx = So2h_RestoreBlendState(gfx);

    gfx = So2h_DrawCount(gfx, (s16)(gSaveContext.save.saveInfo.playerData.healthCapacity / 16),
                         (s16)(So2h_Rnd(rect->x1) - 4), (s16)(So2h_Rnd(rect->y1) - 15), alpha);

    return So2h_RestoreBlendState(gfx);
}

/**
 * Bomber's Notebook. The one element that sits ON the page rather than in it, which is why
 * the scene gives it the raised BigButton square and every other slot a recessed one.
 */
static Gfx* So2h_Content_Notebook(Gfx* gfx, So2hUiId node, const So2hUiRect* rect, void* user) {
    u8 alpha = So2h_ContentAlpha(node);
    So2hUiRect icon;

    (void)user;

    if (alpha == 0) {
        return gfx;
    }

    So2h_IconInRect(rect, CONTENT_NOTEBOOK_ICON_FRAC, &icon);

    return So2h_DrawCollectible(gfx, (TexturePtr)gItemIconBombersNotebookTex, ITEM_ICON_TEX, &icon,
                                CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK) != 0, alpha);
}

/**
 * A song cell. The note glyph and its per-song colour are the SCENE's - they are baked into
 * the descriptor row and are part of the approved render - so content deliberately adds
 * nothing for a song the player owns. An unlearned song gets a scrim and the red cross over
 * the top, which is the only state the save file can tell us about.
 */
static Gfx* So2h_Content_SongCell(Gfx* gfx, So2hUiId node, const So2hUiRect* rect, void* user) {
    s32 index = (s32)node - (s32)SO2H_PAUSE_SONG00;
    u8 alpha = So2h_ContentAlpha(node);
    So2hUiRect cross;

    (void)user;

    if ((alpha == 0) || (index < 0) || (index >= CONTENT_SONG_PAGE_CELLS)) {
        return gfx;
    }
    if (So2h_SongOwned((s16)index)) {
        return gfx;
    }

    So2h_IconInRect(rect, CONTENT_SONG_CROSS_FRAC, &cross);

    // Scrim first: the note underneath is full colour, and dimming it is the only way to
    // read "not learned" without the scene having to author a second, locked note slice.
    gfx = So2h_FillRect(gfx, So2h_Rnd(rect->x0), So2h_Rnd(rect->y0), So2h_Rnd(rect->x1), So2h_Rnd(rect->y1),
                        sCellUnownedColor[0], sCellUnownedColor[1], sCellUnownedColor[2], (u8)(alpha * 3 / 4));
    gfx = So2h_SetupSkinMode(gfx, (u8)(alpha * 2 / 5));
    gfx = So2h_DrawSkinRectR(gfx, (TexturePtr)gSo2hCrossRedTex, SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, &cross);

    return So2h_RestoreBlendState(gfx);
}

/**
 * The ocarina button sequence for whichever song the cursor is on, laid along the staff.
 *
 * The staff art itself belongs to the scene (SO2H_PAUSE_SONG_STAFF draws the song backdrop
 * as a RUN), so content adds only the glyphs. Sequences come from MM's own gOcarinaSongButtons
 * wherever MM knows the song; the six OOT warp songs, which MM's ocarina code never learns,
 * come from sWarpSongButtons.
 */
static Gfx* So2h_Content_SongStaff(Gfx* gfx, So2hUiId node, const So2hUiRect* rect, void* user) {
    u8 alpha = So2h_ContentAlpha(node);
    u8 buttons[WARP_SONG_MAX_BUTTONS];
    So2hUiId focus;
    So2hSongInfo* song;
    So2hUiRect glyphRect;
    s32 songIndex;
    s16 numButtons = 0;
    s16 i;
    f32 glyph;
    f32 gap;
    f32 avail;
    f32 totalW;
    f32 x;
    f32 cy;

    (void)user;

    if (alpha == 0) {
        return gfx;
    }

    // Which song is previewed is the ENGINE's focus, not a bar-local index. That is what lets
    // the preview follow a cell the scene moved without this function knowing it moved.
    focus = So2h_Ui_GetFocus();
    songIndex = (s32)focus - (s32)SO2H_PAUSE_SONG00;
    if ((songIndex < 0) || (songIndex >= CONTENT_SONG_PAGE_CELLS)) {
        return gfx;
    }
    if (!So2h_SongOwned((s16)songIndex)) {
        // An unlearned song keeps its sequence secret, exactly like the vanilla quest page.
        return gfx;
    }

    song = &sSongs[songIndex];
    if (song->warpSong != SONG_WARP_NONE) {
        numButtons = (s16)sWarpSongButtonCount[song->warpSong];
        for (i = 0; i < numButtons; i++) {
            buttons[i] = sWarpSongButtons[song->warpSong][i];
        }
    } else if (song->ocarinaSong != SONG_OCARINA_NONE) {
        numButtons = (s16)gOcarinaSongButtons[song->ocarinaSong].numButtons;
        if (numButtons > WARP_SONG_MAX_BUTTONS) {
            numButtons = WARP_SONG_MAX_BUTTONS;
        }
        for (i = 0; i < numButtons; i++) {
            buttons[i] = gOcarinaSongButtons[song->ocarinaSong].buttonIndex[i];
        }
    }

    if (numButtons <= 0) {
        return gfx;
    }

    cy = (rect->y0 + rect->y1) * 0.5f;
    glyph = (rect->y1 - rect->y0) * 0.46f;
    if (glyph < 4.0f) {
        glyph = 4.0f;
    }
    gap = glyph * 0.22f;

    // Shrink the run rather than overflow the staff if eight glyphs will not fit.
    avail = (rect->x1 - rect->x0) * 0.88f;
    totalW = ((f32)numButtons * glyph) + ((f32)(numButtons - 1) * gap);
    if ((totalW > avail) && (totalW > 0.0f)) {
        f32 shrink = avail / totalW;

        glyph *= shrink;
        gap *= shrink;
        totalW = avail;
    }

    x = ((rect->x0 + rect->x1) * 0.5f) - (totalW * 0.5f);

    for (i = 0; i < numButtons; i++) {
        u8 btn = buttons[i];

        if (btn < 5) {
            glyphRect.x0 = x;
            glyphRect.x1 = x + glyph;
            glyphRect.y0 = cy - (glyph * 0.5f);
            glyphRect.y1 = glyphRect.y0 + glyph;

            gfx = So2h_SetupSkinMode(gfx, alpha);
            gfx = So2h_DrawSkinRectR(gfx, sOcarinaButtonGlyphs[btn], SKIN_GLYPH_TEX, SKIN_GLYPH_TEX, &glyphRect);
        }
        // OCARINA_BTN_C_RIGHT_OR_C_LEFT / OCARINA_BTN_INVALID have nothing sensible to draw,
        // but still take their slot so the run does not close up around a hole.
        x += glyph + gap;
    }

    return So2h_RestoreBlendState(gfx);
}

/**
 * The whole content manifest. This table IS the acceptance test: a new readout is one row
 * here and one callback above, and nothing in the layout, clipping, navigation or asset code
 * moves. Called once per So2h_Ui_Init - bindings survive So2h_Ui_Reset by design.
 */
void So2h_QuestBar_Bind(void) {
    s32 i;

    for (i = 0; i < CONTENT_QUEST_SLOT_COUNT; i++) {
        So2h_Ui_BindDraw((So2hUiId)(SO2H_PAUSE_QUEST_SLOT00 + i), So2h_Content_QuestSlot, NULL);
    }
    for (i = 0; i < CONTENT_SONG_PAGE_CELLS; i++) {
        So2h_Ui_BindDraw((So2hUiId)(SO2H_PAUSE_SONG00 + i), So2h_Content_SongCell, NULL);
    }

    So2h_Ui_BindDraw(SO2H_PAUSE_HEART_WIN, So2h_Content_Heart, NULL);
    So2h_Ui_BindDraw(SO2H_PAUSE_NOTEBOOK, So2h_Content_Notebook, NULL);
    So2h_Ui_BindDraw(SO2H_PAUSE_SONG_STAFF, So2h_Content_SongStaff, NULL);
}
