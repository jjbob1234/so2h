#include "OotItemIcons.h"
#include "OotAssets.h"

#include <array>
#include <string>

#include <spdlog/spdlog.h>

// Source of truth for the relative paths below:
//   env/soh/soh/assets/textures/icon_item_static/icon_item_static.h  (icons, 32x32 RGBA32)
//   env/soh/soh/assets/textures/item_name_static/item_name_static.h  (ENG names, 128x16 IA4)
// Order/positions mirror env/soh/soh's `TexturePtr gItemIcons[]` (z_inventory.c) and
// `iconNameTextures[LANGUAGE_ENG][]` (z_kaleido_scope_PAL.c), which are both indexed directly
// by soh's `ItemID` enum (z64item.h) - the exact same byte `OotInventory.items[]` stores
// (mm/include/z64save.h: "mirrors soh Inventory.items"). Index 0x00 == ITEM_STICK ..
// 0x2C == ITEM_SOLD_OUT (OOT_ITEM_ICON_MAX_ID), 45 entries total.
namespace {

struct OotItemArt {
    const char* iconRelPath;
    const char* nameRelPath;
};

// clang-format off
const OotItemArt kOotItemArt[OOT_ITEM_ICON_MAX_ID + 1] = {
    /* 0x00 ITEM_STICK           */ {"textures/icon_item_static/gItemIconDekuStickTex",        "textures/item_name_static/gDekuStickItemNameENGTex"},
    /* 0x01 ITEM_NUT             */ {"textures/icon_item_static/gItemIconDekuNutTex",          "textures/item_name_static/gDekuNutItemNameENGTex"},
    /* 0x02 ITEM_BOMB            */ {"textures/icon_item_static/gItemIconBombTex",              "textures/item_name_static/gBombItemNameENGTex"},
    /* 0x03 ITEM_BOW             */ {"textures/icon_item_static/gItemIconBowTex",               "textures/item_name_static/gFairyBowItemNameENGTex"},
    /* 0x04 ITEM_ARROW_FIRE      */ {"textures/icon_item_static/gItemIconArrowFireTex",         "textures/item_name_static/gFireArrowItemNameENGTex"},
    /* 0x05 ITEM_DINS_FIRE       */ {"textures/icon_item_static/gItemIconDinsFireTex",          "textures/item_name_static/gDinsFireItemNameENGTex"},
    /* 0x06 ITEM_SLINGSHOT       */ {"textures/icon_item_static/gItemIconSlingshotTex",         "textures/item_name_static/gFairySlingshotItemNameENGTex"},
    /* 0x07 ITEM_OCARINA_FAIRY   */ {"textures/icon_item_static/gItemIconOcarinaFairyTex",      "textures/item_name_static/gFairyOcarinaItemNameENGTex"},
    /* 0x08 ITEM_OCARINA_TIME    */ {"textures/icon_item_static/gItemIconOcarinaOfTimeTex",     "textures/item_name_static/gOcarinaOfTimeItemNameENGTex"},
    /* 0x09 ITEM_BOMBCHU         */ {"textures/icon_item_static/gItemIconBombchuTex",           "textures/item_name_static/gBombchuItemNameENGTex"},
    /* 0x0A ITEM_HOOKSHOT        */ {"textures/icon_item_static/gItemIconHookshotTex",          "textures/item_name_static/gHookshotItemNameENGTex"},
    /* 0x0B ITEM_LONGSHOT        */ {"textures/icon_item_static/gItemIconLongshotTex",          "textures/item_name_static/gLongshotItemNameENGTex"},
    /* 0x0C ITEM_ARROW_ICE       */ {"textures/icon_item_static/gItemIconArrowIceTex",          "textures/item_name_static/gIceArrowItemNameENGTex"},
    /* 0x0D ITEM_FARORES_WIND    */ {"textures/icon_item_static/gItemIconFaroresWindTex",       "textures/item_name_static/gFaroresWindItemNameENGTex"},
    /* 0x0E ITEM_BOOMERANG       */ {"textures/icon_item_static/gItemIconBoomerangTex",         "textures/item_name_static/gBoomerangItemNameENGTex"},
    /* 0x0F ITEM_LENS            */ {"textures/icon_item_static/gItemIconLensOfTruthTex",       "textures/item_name_static/gLensItemNameENGTex"},
    /* 0x10 ITEM_BEAN            */ {"textures/icon_item_static/gItemIconMagicBeanTex",         "textures/item_name_static/gMagicBeansItemNameENGTex"},
    /* 0x11 ITEM_HAMMER          */ {"textures/icon_item_static/gItemIconHammerTex",            "textures/item_name_static/gMegatonHammerItemNameENGTex"},
    /* 0x12 ITEM_ARROW_LIGHT     */ {"textures/icon_item_static/gItemIconArrowLightTex",        "textures/item_name_static/gLightArrowItemNameENGTex"},
    /* 0x13 ITEM_NAYRUS_LOVE     */ {"textures/icon_item_static/gItemIconNayrusLoveTex",        "textures/item_name_static/gNayrusLoveItemNameENGTex"},
    /* 0x14 ITEM_BOTTLE          */ {"textures/icon_item_static/gItemIconBottleEmptyTex",       "textures/item_name_static/gEmptyBottleItemNameENGTex"},
    /* 0x15 ITEM_POTION_RED      */ {"textures/icon_item_static/gItemIconBottlePotionRedTex",   "textures/item_name_static/gRedPotionItemNameENGTex"},
    /* 0x16 ITEM_POTION_GREEN    */ {"textures/icon_item_static/gItemIconBottlePotionGreenTex", "textures/item_name_static/gGreenPotionItemNameENGTex"},
    /* 0x17 ITEM_POTION_BLUE     */ {"textures/icon_item_static/gItemIconBottlePotionBlueTex",  "textures/item_name_static/gBluePotionItemNameENGTex"},
    /* 0x18 ITEM_FAIRY           */ {"textures/icon_item_static/gItemIconBottleFairyTex",       "textures/item_name_static/gBottledFairyItemNameENGTex"},
    /* 0x19 ITEM_FISH            */ {"textures/icon_item_static/gItemIconBottleFishTex",        "textures/item_name_static/gFishItemNameENGTex"},
    /* 0x1A ITEM_MILK            */ {"textures/icon_item_static/gItemIconBottleMilkFullTex",    "textures/item_name_static/gFullMilkItemNameENGTex"},
    /* 0x1B ITEM_LETTER_RUTO     */ {"textures/icon_item_static/gItemIconBottleRutosLetterTex", "textures/item_name_static/gRutosLetterItemNameENGTex"},
    /* 0x1C ITEM_BLUE_FIRE       */ {"textures/icon_item_static/gItemIconBottleBlueFireTex",    "textures/item_name_static/gBlueFireItemNameENGTex"},
    /* 0x1D ITEM_BUG             */ {"textures/icon_item_static/gItemIconBottleBugTex",         "textures/item_name_static/gBugItemNameENGTex"},
    /* 0x1E ITEM_BIG_POE         */ {"textures/icon_item_static/gItemIconBottleBigPoeTex",      "textures/item_name_static/gBigPoeItemNameENGTex"},
    /* 0x1F ITEM_MILK_HALF       */ {"textures/icon_item_static/gItemIconBottleMilkHalfTex",    "textures/item_name_static/gHalfMilkItemNameENGTex"},
    /* 0x20 ITEM_POE             */ {"textures/icon_item_static/gItemIconBottlePoeTex",         "textures/item_name_static/gPoeItemNameENGTex"},
    /* 0x21 ITEM_WEIRD_EGG       */ {"textures/icon_item_static/gItemIconWeirdEggTex",          "textures/item_name_static/gWeirdEggItemNameENGTex"},
    /* 0x22 ITEM_CHICKEN         */ {"textures/icon_item_static/gItemIconChickenTex",           "textures/item_name_static/gCuccoItemNameENGTex"},
    /* 0x23 ITEM_LETTER_ZELDA    */ {"textures/icon_item_static/gItemIconZeldasLetterTex",      "textures/item_name_static/gZeldasLetterItemNameENGTex"},
    /* 0x24 ITEM_MASK_KEATON     */ {"textures/icon_item_static/gItemIconMaskKeatonTex",        "textures/item_name_static/gKeatonMaskItemNameENGTex"},
    /* 0x25 ITEM_MASK_SKULL      */ {"textures/icon_item_static/gItemIconMaskSkullTex",         "textures/item_name_static/gSkullMaskItemNameENGTex"},
    /* 0x26 ITEM_MASK_SPOOKY     */ {"textures/icon_item_static/gItemIconMaskSpookyTex",        "textures/item_name_static/gSpookyMaskItemNameENGTex"},
    /* 0x27 ITEM_MASK_BUNNY      */ {"textures/icon_item_static/gItemIconMaskBunnyHoodTex",     "textures/item_name_static/gBunnyHoodItemNameENGTex"},
    /* 0x28 ITEM_MASK_GORON      */ {"textures/icon_item_static/gItemIconMaskGoronTex",         "textures/item_name_static/gGoronMaskItemNameENGTex"},
    /* 0x29 ITEM_MASK_ZORA       */ {"textures/icon_item_static/gItemIconMaskZoraTex",          "textures/item_name_static/gZoraMaskItemNameENGTex"},
    /* 0x2A ITEM_MASK_GERUDO     */ {"textures/icon_item_static/gItemIconMaskGerudoTex",        "textures/item_name_static/gGerudoMaskItemNameENGTex"},
    /* 0x2B ITEM_MASK_TRUTH      */ {"textures/icon_item_static/gItemIconMaskTruthTex",         "textures/item_name_static/gMaskofTruthItemNameENGTex"},
    /* 0x2C ITEM_SOLD_OUT        */ {"textures/icon_item_static/gItemIconSoldOutTex",           "textures/item_name_static/gSOLDOUTItemNameENGTex"},
};
// clang-format on

// EquipmentType order (0=sword,1=shield,2=tunic,3=boots) x tier (1-based; index 0 unused/"none").
// Sword goes up to tier 4 (Broken Giant's Knife, EQUIP_INV_SWORD_BROKENGIANTKNIFE in soh's
// EquipInvSword) since EQUIP_OOT_GRID_COLS is 4; the other three rows only ever populate tiers
// 1-3, so their tier-4 slot is intentionally left {nullptr, nullptr} (never owned, never drawn).
// Source: same two soh headers as above, `Sword`/`Shield`/`Tunic`/`Boots` symbol families.
const OotItemArt kOotEquipArt[4][5] = {
    // sword: tier1 Kokiri, tier2 Master, tier3 Biggoron, tier4 Broken Giant's Knife
    {
        {nullptr, nullptr},
        {"textures/icon_item_static/gItemIconSwordKokiriTex", "textures/item_name_static/gKokiriSwordItemNameENGTex"},
        {"textures/icon_item_static/gItemIconSwordMasterTex", "textures/item_name_static/gMasterSwordItemNameENGTex"},
        {"textures/icon_item_static/gItemIconSwordBiggoronTex", "textures/item_name_static/gBiggoronsSwordItemNameENGTex"},
        {"textures/icon_item_static/gItemIconBrokenGoronsSwordTex", "textures/item_name_static/gBrokenGoronsSwordItemNameENGTex"},
    },
    // shield: tier1 Deku, tier2 Hylian, tier3 Mirror
    {
        {nullptr, nullptr},
        {"textures/icon_item_static/gItemIconShieldDekuTex", "textures/item_name_static/gDekuShieldItemNameENGTex"},
        {"textures/icon_item_static/gItemIconShieldHylianTex", "textures/item_name_static/gHylianShieldItemNameENGTex"},
        {"textures/icon_item_static/gItemIconShieldMirrorTex", "textures/item_name_static/gMirrorShieldItemNameENGTex"},
        {nullptr, nullptr},
    },
    // tunic: tier1 Kokiri, tier2 Goron, tier3 Zora
    {
        {nullptr, nullptr},
        {"textures/icon_item_static/gItemIconTunicKokiriTex", "textures/item_name_static/gKokiriTunicItemNameENGTex"},
        {"textures/icon_item_static/gItemIconTunicGoronTex", "textures/item_name_static/gGoronTunicItemNameENGTex"},
        {"textures/icon_item_static/gItemIconTunicZoraTex", "textures/item_name_static/gZoraTunicItemNameENGTex"},
        {nullptr, nullptr},
    },
    // boots: tier1 Kokiri, tier2 Iron, tier3 Hover
    {
        {nullptr, nullptr},
        {"textures/icon_item_static/gItemIconBootsKokiriTex", "textures/item_name_static/gKokiriBootsItemNameENGTex"},
        {"textures/icon_item_static/gItemIconBootsIronTex", "textures/item_name_static/gIronBootsItemNameENGTex"},
        {"textures/icon_item_static/gItemIconBootsHoverTex", "textures/item_name_static/gHoverBootsItemNameENGTex"},
        {nullptr, nullptr},
    },
};

// Resolved-path cache: OotAssets::ResolveOotPath just does string concatenation, but callers
// (drawn every frame the pause menu is open) get a plain persistent `const char*` back instead
// of paying for + owning a fresh std::string each call.
//
// SO2H [Menu] crash fix: resolution alone is NOT enough. `IsOotContentAvailable()` only says a
// merge happened; individual folders (notably textures/icon_item_24_static and the
// gPauseQuestStatus* tiles) may still be absent from the user's oot.o2r. A resolvable-looking
// path for a non-existent entry used to be handed to the renderer as a texture pointer, which
// is fatal. Every lookup now verifies the entry exists once and caches the verdict.
//
// The cache can't use `empty()` as "not looked up yet" any more, because "looked up, missing"
// is also empty - hence the parallel state array.
enum CacheState : unsigned char {
    CACHE_UNRESOLVED = 0,
    CACHE_PRESENT = 1,
    CACHE_MISSING = 2,
};

// Resolves + existence-checks once, then answers from cache. Returns nullptr when the relative
// path is null or the entry is not in the merged archive.
const char* ResolveChecked(const char* relPath, std::string& cache, unsigned char& state) {
    if (state == CACHE_UNRESOLVED) {
        if (relPath == nullptr) {
            state = CACHE_MISSING;
        } else {
            std::string resolved = OotAssets::ResolveOotPath(relPath);
            if (OotAssets::OotFileExists(resolved)) {
                cache = resolved;
                state = CACHE_PRESENT;
            } else {
                state = CACHE_MISSING;
            }
        }
    }
    return (state == CACHE_PRESENT) ? cache.c_str() : nullptr;
}

} // namespace

extern "C" const char* OotItemIcons_GetItemIconPath(unsigned char itemId) {
    if (!OotAssets::IsOotContentAvailable() || itemId > OOT_ITEM_ICON_MAX_ID) {
        return nullptr;
    }
    static std::array<std::string, OOT_ITEM_ICON_MAX_ID + 1> sCache;
    static std::array<unsigned char, OOT_ITEM_ICON_MAX_ID + 1> sState{};
    return ResolveChecked(kOotItemArt[itemId].iconRelPath, sCache[itemId], sState[itemId]);
}

extern "C" const char* OotItemIcons_GetItemNamePath(unsigned char itemId) {
    if (!OotAssets::IsOotContentAvailable() || itemId > OOT_ITEM_ICON_MAX_ID) {
        return nullptr;
    }
    static std::array<std::string, OOT_ITEM_ICON_MAX_ID + 1> sCache;
    static std::array<unsigned char, OOT_ITEM_ICON_MAX_ID + 1> sState{};
    return ResolveChecked(kOotItemArt[itemId].nameRelPath, sCache[itemId], sState[itemId]);
}

extern "C" const char* OotItemIcons_GetEquipIconPath(unsigned char equipType, unsigned char tier) {
    if (!OotAssets::IsOotContentAvailable() || equipType >= 4 || tier == 0 || tier > 4) {
        return nullptr;
    }
    static std::array<std::array<std::string, 5>, 4> sCache;
    static std::array<std::array<unsigned char, 5>, 4> sState{};
    return ResolveChecked(kOotEquipArt[equipType][tier].iconRelPath, sCache[equipType][tier],
                          sState[equipType][tier]);
}

extern "C" const char* OotItemIcons_GetEquipNamePath(unsigned char equipType, unsigned char tier) {
    if (!OotAssets::IsOotContentAvailable() || equipType >= 4 || tier == 0 || tier > 4) {
        return nullptr;
    }
    static std::array<std::array<std::string, 5>, 4> sCache;
    static std::array<std::array<unsigned char, 5>, 4> sState{};
    return ResolveChecked(kOotEquipArt[equipType][tier].nameRelPath, sCache[equipType][tier],
                          sState[equipType][tier]);
}

// ---------------------------------------------------------------------------------------
// SO2H merged quest bar art. See the OotQuestArtId comment block in OotItemIcons.h for why
// this is a separate table from kOotItemArt above.
//
// Sources:
//   env/soh/soh/assets/textures/icon_item_24_static/icon_item_24_static.h  (24x24 RGBA32)
//   env/soh/soh/assets/textures/icon_item_static/icon_item_static.h        (80x32 IA8 tiles,
//     drawn by soh at z_kaleido_scope_PAL.c:1399/1412 as the quest page background. The name
//     digits are <column><row>, not <row><column> - see OotItemIcons.h.)
// Order must match the OotQuestArtId enum exactly.
namespace {

// clang-format off
const char* const kOotQuestArt[OOT_QUEST_ART_MAX] = {
    /*  0 MEDALLION_FOREST */ "textures/icon_item_24_static/gQuestIconMedallionForestTex",
    /*  1 MEDALLION_FIRE   */ "textures/icon_item_24_static/gQuestIconMedallionFireTex",
    /*  2 MEDALLION_WATER  */ "textures/icon_item_24_static/gQuestIconMedallionWaterTex",
    /*  3 MEDALLION_SPIRIT */ "textures/icon_item_24_static/gQuestIconMedallionSpiritTex",
    /*  4 MEDALLION_SHADOW */ "textures/icon_item_24_static/gQuestIconMedallionShadowTex",
    /*  5 MEDALLION_LIGHT  */ "textures/icon_item_24_static/gQuestIconMedallionLightTex",
    /*  6 STONE_KOKIRI     */ "textures/icon_item_24_static/gQuestIconKokiriEmeraldTex",
    /*  7 STONE_GORON      */ "textures/icon_item_24_static/gQuestIconGoronRubyTex",
    /*  8 STONE_ZORA       */ "textures/icon_item_24_static/gQuestIconZoraSapphireTex",
    /*  9 STONE_OF_AGONY   */ "textures/icon_item_24_static/gQuestIconStoneOfAgonyTex",
    /* 10 GERUDO_CARD      */ "textures/icon_item_24_static/gQuestIconGerudosCardTex",
    /* 11 GOLD_SKULLTULA   */ "textures/icon_item_24_static/gQuestIconGoldSkulltulaTex",
    /* 12 HEART_CONTAINER  */ "textures/icon_item_24_static/gQuestIconHeartContainerTex",
    /* 13 HEART_PIECE      */ "textures/icon_item_24_static/gQuestIconHeartPieceTex",
    /* 14 HEX_TILE_10      */ "textures/icon_item_static/gPauseQuestStatus10Tex",
    /* 15 HEX_TILE_20      */ "textures/icon_item_static/gPauseQuestStatus20Tex",
    /* 16 HEX_TILE_11      */ "textures/icon_item_static/gPauseQuestStatus11Tex",
    /* 17 HEX_TILE_21      */ "textures/icon_item_static/gPauseQuestStatus21Tex",
    /* 18 HEX_TILE_12      */ "textures/icon_item_static/gPauseQuestStatus12Tex",
    /* 19 HEX_TILE_22      */ "textures/icon_item_static/gPauseQuestStatus22Tex",
    /* 20 HEX_TILE_13      */ "textures/icon_item_static/gPauseQuestStatus13Tex",
    /* 21 HEX_TILE_23      */ "textures/icon_item_static/gPauseQuestStatus23Tex",
    /* 22 HEX_TILE_10_ENG  */ "textures/icon_item_static/gPauseQuestStatus10ENGTex",
};
// clang-format on

static_assert(sizeof(kOotQuestArt) / sizeof(kOotQuestArt[0]) == OOT_QUEST_ART_MAX,
              "kOotQuestArt[] must have exactly one entry per OotQuestArtId");

} // namespace

extern "C" const char* OotQuestArt_GetPath(int artId) {
    if (!OotAssets::IsOotContentAvailable() || artId < 0 || artId >= OOT_QUEST_ART_MAX) {
        return nullptr;
    }
    static std::array<std::string, OOT_QUEST_ART_MAX> sCache;
    static std::array<unsigned char, OOT_QUEST_ART_MAX> sState{};
    return ResolveChecked(kOotQuestArt[artId], sCache[artId], sState[artId]);
}

// ---------------------------------------------------------------------------------------
// Diagnostic. Blank quest slots look identical whether the layout is wrong or the art is simply
// not in the merged o2r (OotQuestArt_GetPath returns nullptr and the bar falls back to an empty
// recess). This prints the verdict for every id, once per run.
extern "C" void OotQuestArt_LogAudit(void) {
    static bool sDone = false;
    if (sDone) {
        return;
    }
    sDone = true;

    if (!OotAssets::IsOotContentAvailable()) {
        SPDLOG_WARN("[SO2H][QuestArt] OOT content is NOT available - every quest icon will be blank.");
        return;
    }

    int missing = 0;
    for (int i = 0; i < OOT_QUEST_ART_MAX; i++) {
        const char* rel = kOotQuestArt[i];
        std::string resolved = (rel != nullptr) ? OotAssets::ResolveOotPath(rel) : std::string("<null>");
        bool present = (rel != nullptr) && OotAssets::OotFileExists(resolved);
        if (!present) {
            missing++;
        }
        SPDLOG_INFO("[SO2H][QuestArt] {:2d} {:<8} {}", i, present ? "PRESENT" : "MISSING", resolved);
    }
    SPDLOG_INFO("[SO2H][QuestArt] {} of {} entries missing from the merged archive.", missing,
                (int)OOT_QUEST_ART_MAX);
}
