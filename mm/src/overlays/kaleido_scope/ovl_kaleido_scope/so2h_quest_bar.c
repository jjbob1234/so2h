/*
 * File: so2h_quest_bar.c
 * Overlay: ovl_kaleido_scope
 * Description: SO2H [Menu] backwards-L quest / song bar (see so2h_quest_bar.h)
 */

#include "z_kaleido_scope.h"
#include "so2h_quest_bar.h"
#include "2s2h/Menu/so2h_pause_window.h"
#include "BenPort.h"
#include "interface/parameter_static/parameter_static.h"
#include "archives/icon_item_static/icon_item_static_yar.h"

// 2S2H [Port] the digit textures were made global specifically so the kaleido files could
// reach them (see the comment above sCounterTextures in z_parameter.c).
extern const char* sCounterTextures[];

// ---------------------------------------------------------------------------------------
// Layout, N64 320x240 screen space. Horizontal values are fanned out through So2h_MapX so
// the bar hugs the real screen edges on non-4:3 displays.
// ---------------------------------------------------------------------------------------

#define BAR_SPLIT_X SO2H_WINDOW_RIGHT_X  // 192, left edge of the right arm
#define BAR_SPLIT_Y SO2H_WINDOW_BOTTOM_Y // 168, top edge of the bottom arm

#define RIGHT_ARM_COLS 3
#define RIGHT_ARM_ROWS 7
#define RIGHT_ARM_CELLS (RIGHT_ARM_COLS * RIGHT_ARM_ROWS) // 21

#define RIGHT_ARM_INNER_X 198
#define RIGHT_ARM_INNER_Y 8
#define RIGHT_ARM_CELL_W 39
#define RIGHT_ARM_CELL_H 22
#define RIGHT_ARM_ICON 18

#define BOTTOM_ARM_COLS 6
#define BOTTOM_ARM_ROWS 2
#define BOTTOM_ARM_CELLS (BOTTOM_ARM_COLS * BOTTOM_ARM_ROWS) // 12

#define BOTTOM_ARM_INNER_X 6
#define BOTTOM_ARM_INNER_Y 174
#define BOTTOM_ARM_CELL_W 30
#define BOTTOM_ARM_CELL_H 30

// Placeholder art regions (see header). Distinct colours so the three seams where real
// border art will later drop in are obvious on screen.
static u8 sBarRegionRightArm[3] = { 46, 38, 30 };  // region B
static u8 sBarRegionBottomArm[3] = { 38, 32, 46 }; // region A
static u8 sBarRegionCorner[3] = { 58, 44, 26 };    // region C
static u8 sBarCellBg[3] = { 20, 18, 16 };

// Right arm cell indices. Order is fixed by the approved plan.
typedef enum So2hQuestBarCell {
    /*  0 */ SO2H_CELL_MM_HEART_PIECES,
    /*  1 */ SO2H_CELL_MM_HEART_CONTAINERS,
    /*  2 */ SO2H_CELL_MM_REMAINS_FIRST, // 2..5 Odolwa / Goht / Gyorg / Twinmold
    /*  6 */ SO2H_CELL_MM_QUIVER = 6,
    /*  7 */ SO2H_CELL_MM_BOMB_BAG,
    /*  8 */ SO2H_CELL_MM_WALLET,
    /*  9 */ SO2H_CELL_OOT_MEDALLION_FIRST, // 9..14
    /* 15 */ SO2H_CELL_OOT_STONE_FIRST = 15, // 15..17
    /* 18 */ SO2H_CELL_OOT_STONE_OF_AGONY = 18,
    /* 19 */ SO2H_CELL_OOT_GERUDO_CARD,
    /* 20 */ SO2H_CELL_MM_BOMBERS_NOTEBOOK
} So2hQuestBarCell;

// OOT questItems bit layout, matching the OoT save format the OotInventory mirror uses.
#define OOT_QUEST_MEDALLION_FOREST 0
#define OOT_QUEST_STONE_KOKIRI 6
#define OOT_QUEST_STONE_OF_AGONY 9
#define OOT_QUEST_GERUDO_CARD 10

// Swatch colours for the OOT cells. MM's icon_item_static archive contains no OoT medallion
// or spiritual stone art (verified: no Medallion/Emerald/Ruby/Sapphire symbols in
// icon_item_static_yar.h), and OotItemIcons_GetItemIconPath tops out at OOT_ITEM_ICON_MAX_ID
// 0x2C which is below the OoT medallion item ids, so these render as colour swatches driven
// by the real save bit until dedicated art is added.
static u8 sOotMedallionColors[6][3] = {
    { 60, 190, 80 },   // Forest
    { 230, 90, 40 },   // Fire
    { 70, 140, 235 },  // Water
    { 240, 170, 50 },  // Spirit
    { 150, 90, 210 },  // Shadow
    { 240, 235, 150 }, // Light
};
static u8 sOotStoneColors[3][3] = {
    { 80, 210, 120 },  // Kokiri Emerald
    { 220, 70, 70 },   // Goron Ruby
    { 90, 170, 230 },  // Zora Sapphire
};
static u8 sOotAgonyColor[3] = { 170, 170, 180 };
static u8 sOotGerudoCardColor[3] = { 200, 120, 190 };
static u8 sCellUnownedColor[3] = { 52, 48, 44 };

// Local copy of the twelve song colours, mirroring the sQuestSongsPrimRed/Green/Blue tables
// in z_kaleido_collect.c. Copied rather than shared because those live inside
// KaleidoScope_DrawQuestStatus as function statics, and z_kaleido_collect.c is deliberately
// left untouched so the old quest page can be restored or reused.
static u8 sSongColors[12][3] = {
    { 150, 255, 100 }, // QUEST_SONG_SONATA
    { 255, 80, 40 },   // QUEST_SONG_LULLABY
    { 100, 150, 255 }, // QUEST_SONG_BOSSA_NOVA
    { 255, 160, 0 },   // QUEST_SONG_ELEGY
    { 255, 100, 255 }, // QUEST_SONG_OATH
    { 255, 240, 100 }, // QUEST_SONG_SARIA
    { 255, 255, 255 }, // QUEST_SONG_TIME
    { 255, 255, 255 }, // QUEST_SONG_HEALING
    { 255, 255, 255 }, // QUEST_SONG_EPONA
    { 255, 255, 255 }, // QUEST_SONG_SOARING
    { 255, 255, 255 }, // QUEST_SONG_STORMS
    { 255, 255, 255 }, // QUEST_SONG_SUN
};

// ---------------------------------------------------------------------------------------
// State. The arm-internal cursor index deliberately lives here and not in PauseContext, so
// no save struct or [PAUSE_PAGE_MAX] array has to grow for it.
// ---------------------------------------------------------------------------------------

static s16 sBarCursorIndex = 0;
static s16 sBarCursorPhase = 0;

void So2h_QuestBar_Reset(void) {
    sBarCursorIndex = 0;
    sBarCursorPhase = 0;
}

s32 So2h_QuestBar_IsCursorInBar(PauseContext* pauseCtx) {
    return (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) ||
           (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_BOTTOM);
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
                So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_RIGHT, 0);
                return true;
            }
        }
        return false;
    }

    if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) {
        col = sBarCursorIndex % RIGHT_ARM_COLS;
        row = sBarCursorIndex / RIGHT_ARM_COLS;

        if (pauseCtx->stickAdjX < -30) {
            if (col > 0) {
                sBarCursorIndex--;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            } else {
                // Leftmost column: fall back out onto the page-right marker.
                So2h_QuestBar_Exit(play, PAUSE_CURSOR_PAGE_RIGHT);
            }
        } else if (pauseCtx->stickAdjX > 30) {
            if (col < (RIGHT_ARM_COLS - 1)) {
                sBarCursorIndex++;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            }
        } else if (pauseCtx->stickAdjY > 30) {
            if (row > 0) {
                sBarCursorIndex -= RIGHT_ARM_COLS;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            }
        } else if (pauseCtx->stickAdjY < -30) {
            if (row < (RIGHT_ARM_ROWS - 1)) {
                sBarCursorIndex += RIGHT_ARM_COLS;
                Audio_PlaySfx(NA_SE_SY_CURSOR);
            } else {
                // Bottom row of the right arm drops into the bottom arm, around the corner.
                So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_BOTTOM, BOTTOM_ARM_COLS - 1);
            }
        }

        if (sBarCursorIndex < 0) {
            sBarCursorIndex = 0;
        } else if (sBarCursorIndex >= RIGHT_ARM_CELLS) {
            sBarCursorIndex = RIGHT_ARM_CELLS - 1;
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
            So2h_QuestBar_Enter(play, PAUSE_CURSOR_QUEST_BAR_RIGHT, RIGHT_ARM_CELLS - RIGHT_ARM_COLS);
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

static Gfx* So2h_SetupIconMode(Gfx* gfx, u8 alpha) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, alpha);

    return gfx;
}

static Gfx* So2h_SetupIAMode(Gfx* gfx, u8 r, u8 g, u8 b, u8 alpha) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, alpha);

    return gfx;
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

static void So2h_RightArmCellRect(s16 index, s16* x0, s16* y0) {
    *x0 = RIGHT_ARM_INNER_X + ((index % RIGHT_ARM_COLS) * RIGHT_ARM_CELL_W);
    *y0 = RIGHT_ARM_INNER_Y + ((index / RIGHT_ARM_COLS) * RIGHT_ARM_CELL_H);
}

static void So2h_BottomArmCellRect(s16 index, s16* x0, s16* y0) {
    *x0 = BOTTOM_ARM_INNER_X + ((index % BOTTOM_ARM_COLS) * BOTTOM_ARM_CELL_W);
    *y0 = BOTTOM_ARM_INNER_Y + ((index / BOTTOM_ARM_COLS) * BOTTOM_ARM_CELL_H);
}

// ---------------------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------------------

void So2h_QuestBar_Draw(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    OotInventory* ootInv = &gSaveContext.save.shipSaveInfo.so2h.oot.inventory;
    Gfx* gfx;
    s16 i;
    s16 cellX;
    s16 cellY;
    s16 iconX;
    s16 iconY;
    s16 value;
    u8 alpha;
    u8 owned;
    u8* swatch;
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

    // --- Placeholder art region B: right arm panel ---
    gfx = So2h_FillRect(gfx, BAR_SPLIT_X + slideX, 0, SCREEN_WIDTH + slideX, BAR_SPLIT_Y, sBarRegionRightArm[0],
                        sBarRegionRightArm[1], sBarRegionRightArm[2], alpha);
    // --- Placeholder art region A: bottom arm panel ---
    gfx = So2h_FillRect(gfx, 0, BAR_SPLIT_Y + slideY, BAR_SPLIT_X, SCREEN_HEIGHT + slideY, sBarRegionBottomArm[0],
                        sBarRegionBottomArm[1], sBarRegionBottomArm[2], alpha);
    // --- Placeholder art region C: bottom-right corner ---
    gfx = So2h_FillRect(gfx, BAR_SPLIT_X + slideX, BAR_SPLIT_Y + slideY, SCREEN_WIDTH + slideX,
                        SCREEN_HEIGHT + slideY, sBarRegionCorner[0], sBarRegionCorner[1], sBarRegionCorner[2], alpha);

    // Only populate the arms once they have arrived, so nothing streaks across the screen.
    if (factor > 0.98f) {
        // ----------------------------- Right arm: quest progress -----------------------------
        for (i = 0; i < RIGHT_ARM_CELLS; i++) {
            TexturePtr icon = NULL;

            So2h_RightArmCellRect(i, &cellX, &cellY);
            iconX = cellX + ((RIGHT_ARM_CELL_W - RIGHT_ARM_ICON) / 2);
            iconY = cellY + ((RIGHT_ARM_CELL_H - RIGHT_ARM_ICON) / 2);

            gfx = So2h_FillRect(gfx, cellX, cellY, cellX + RIGHT_ARM_CELL_W - 2, cellY + RIGHT_ARM_CELL_H - 2,
                                sBarCellBg[0], sBarCellBg[1], sBarCellBg[2], 150);

            owned = false;
            swatch = sCellUnownedColor;
            value = -1;

            switch (i) {
                case SO2H_CELL_MM_HEART_PIECES:
                    value = (s16)GET_QUEST_HEART_PIECE_COUNT;
                    owned = value > 0;
                    if (value == 1) {
                        icon = (TexturePtr)gItemIconHeartPiece1Tex;
                    } else if (value == 2) {
                        icon = (TexturePtr)gItemIconHeartPiece2Tex;
                    } else if (value >= 3) {
                        icon = (TexturePtr)gItemIconHeartPiece3Tex;
                    }
                    break;

                case SO2H_CELL_MM_HEART_CONTAINERS:
                    // No dedicated heart-container icon exists in icon_item_static; shown as a
                    // count only until real art is wired in.
                    value = gSaveContext.save.saveInfo.playerData.healthCapacity / 16;
                    owned = value > 0;
                    swatch = sOotStoneColors[1]; // reuse the red swatch for hearts
                    break;

                case SO2H_CELL_MM_QUIVER: {
                    static TexturePtr sQuiverIcons[3] = { gItemIconQuiver30Tex, gItemIconQuiver40Tex,
                                                          gItemIconQuiver50Tex };
                    s16 upg = (s16)GET_CUR_UPG_VALUE(UPG_QUIVER);

                    if (upg > 0) {
                        owned = true;
                        icon = sQuiverIcons[(upg > 3) ? 2 : (upg - 1)];
                    }
                    break;
                }

                case SO2H_CELL_MM_BOMB_BAG: {
                    static TexturePtr sBombBagIcons[3] = { gItemIconBombBag20Tex, gItemIconBombBag30Tex,
                                                           gItemIconBombBag40Tex };
                    s16 upg = (s16)GET_CUR_UPG_VALUE(UPG_BOMB_BAG);

                    if (upg > 0) {
                        owned = true;
                        icon = sBombBagIcons[(upg > 3) ? 2 : (upg - 1)];
                    }
                    break;
                }

                case SO2H_CELL_MM_WALLET: {
                    static TexturePtr sWalletIcons[3] = { gItemIconDefaultWalletTex, gItemIconAdultsWalletTex,
                                                          gItemIconGiantsWalletTex };
                    s16 upg = (s16)GET_CUR_UPG_VALUE(UPG_WALLET);

                    owned = true;
                    icon = sWalletIcons[(upg > 2) ? 2 : upg];
                    break;
                }

                case SO2H_CELL_OOT_STONE_OF_AGONY:
                    owned = (ootInv->questItems & (1 << OOT_QUEST_STONE_OF_AGONY)) != 0;
                    swatch = sOotAgonyColor;
                    break;

                case SO2H_CELL_OOT_GERUDO_CARD:
                    owned = (ootInv->questItems & (1 << OOT_QUEST_GERUDO_CARD)) != 0;
                    swatch = sOotGerudoCardColor;
                    break;

                case SO2H_CELL_MM_BOMBERS_NOTEBOOK:
                    owned = CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK) != 0;
                    if (owned) {
                        icon = (TexturePtr)gItemIconBombersNotebookTex;
                    }
                    break;

                default:
                    if ((i >= SO2H_CELL_MM_REMAINS_FIRST) && (i < (SO2H_CELL_MM_REMAINS_FIRST + 4))) {
                        s16 remain = i - SO2H_CELL_MM_REMAINS_FIRST;

                        owned = CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA + remain) != 0;
                        if (owned) {
                            icon = (TexturePtr)gItemIcons[ITEM_REMAINS_ODOLWA + remain];
                        }
                    } else if ((i >= SO2H_CELL_OOT_MEDALLION_FIRST) && (i < (SO2H_CELL_OOT_MEDALLION_FIRST + 6))) {
                        s16 med = i - SO2H_CELL_OOT_MEDALLION_FIRST;

                        owned = (ootInv->questItems & (1 << (OOT_QUEST_MEDALLION_FOREST + med))) != 0;
                        swatch = sOotMedallionColors[med];
                    } else if ((i >= SO2H_CELL_OOT_STONE_FIRST) && (i < (SO2H_CELL_OOT_STONE_FIRST + 3))) {
                        s16 stone = i - SO2H_CELL_OOT_STONE_FIRST;

                        owned = (ootInv->questItems & (1 << (OOT_QUEST_STONE_KOKIRI + stone))) != 0;
                        swatch = sOotStoneColors[stone];
                    }
                    break;
            }

            if (icon != NULL) {
                gfx = So2h_SetupIconMode(gfx, alpha);
                gfx = So2h_DrawTexRectRGBA32(gfx, icon, 32, 32, So2h_MapX(iconX), iconY, RIGHT_ARM_ICON,
                                             RIGHT_ARM_ICON);
                gDPPipeSync(gfx++);
                gDPSetCycleType(gfx++, G_CYC_1CYCLE);
                gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
            } else {
                gfx = So2h_FillRect(gfx, iconX + 3, iconY + 3, iconX + RIGHT_ARM_ICON - 3,
                                    iconY + RIGHT_ARM_ICON - 3, owned ? swatch[0] : sCellUnownedColor[0],
                                    owned ? swatch[1] : sCellUnownedColor[1],
                                    owned ? swatch[2] : sCellUnownedColor[2], alpha);
            }

            if (value >= 0) {
                gfx = So2h_DrawCount(gfx, value, cellX + RIGHT_ARM_CELL_W - 4, cellY + 5, alpha);
                gDPPipeSync(gfx++);
                gDPSetCycleType(gfx++, G_CYC_1CYCLE);
                gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
            }
        }

        // ----------------------------- Bottom arm: songs (display only) ----------------------
        for (i = 0; i < BOTTOM_ARM_CELLS; i++) {
            So2h_BottomArmCellRect(i, &cellX, &cellY);

            gfx = So2h_FillRect(gfx, cellX, cellY, cellX + BOTTOM_ARM_CELL_W - 2, cellY + BOTTOM_ARM_CELL_H - 2,
                                sBarCellBg[0], sBarCellBg[1], sBarCellBg[2], 150);

            owned = CHECK_QUEST_ITEM(QUEST_SONG_SONATA + i) != 0;
            if ((i == (QUEST_SONG_LULLABY - QUEST_SONG_SONATA)) && !owned &&
                (CHECK_QUEST_ITEM(QUEST_SONG_LULLABY_INTRO) != 0)) {
                // Matches the vanilla quest page: the Lullaby Intro also lights the Lullaby slot.
                owned = true;
            }

            if (owned) {
                gfx = So2h_SetupIAMode(gfx, sSongColors[i][0], sSongColors[i][1], sSongColors[i][2], alpha);
                gfx = Gfx_DrawTexRectIA8(gfx, (TexturePtr)gItemIconSongNoteTex, 16, 24,
                                         So2h_MapX(cellX + ((BOTTOM_ARM_CELL_W - 12) / 2)), cellY + 4, 12, 18,
                                         (16 << 10) / 12, (24 << 10) / 18);
                gDPPipeSync(gfx++);
                gDPSetCycleType(gfx++, G_CYC_1CYCLE);
                gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
            } else {
                gfx = So2h_FillRect(gfx, cellX + 10, cellY + 8, cellX + BOTTOM_ARM_CELL_W - 12,
                                    cellY + BOTTOM_ARM_CELL_H - 10, sCellUnownedColor[0], sCellUnownedColor[1],
                                    sCellUnownedColor[2], alpha);
            }
        }

        // ----------------------------- Cursor -------------------------------------------------
        // The vanilla KaleidoScope_DrawCursor lives in the pause 3D space and would be trapped
        // inside the shrunken window, so while the cursor is in an arm the bar draws its own
        // 2D highlight instead and the caller suppresses the 3D one.
        if (So2h_QuestBar_IsCursorInBar(pauseCtx) && (pauseCtx->state == PAUSE_STATE_MAIN)) {
            s16 w;
            s16 h;
            u8 pulse;

            sBarCursorPhase += 0x400;
            pulse = (u8)(160.0f + (95.0f * Math_SinS(sBarCursorPhase)));

            if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_QUEST_BAR_RIGHT) {
                So2h_RightArmCellRect(sBarCursorIndex, &cellX, &cellY);
                w = RIGHT_ARM_CELL_W - 2;
                h = RIGHT_ARM_CELL_H - 2;
            } else {
                So2h_BottomArmCellRect(sBarCursorIndex, &cellX, &cellY);
                w = BOTTOM_ARM_CELL_W - 2;
                h = BOTTOM_ARM_CELL_H - 2;
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
