/**
 * so2h [Scene] patch 0003 - scene-origin (OOT/MM) tagging system.
 * See z_scene_origin.h for the design rationale (why OOT scenes get their own
 * independent enum instead of being appended to MM's SceneId).
 */

#include "global.h"
#include "2s2h/Compat/Scene/z_scene_origin.h"

// MM's own "Human readable name" column (scene_table.h DEFINE_SCENE arg 8) is NOT
// stored on gSceneTable itself (confirmed by reading z_scene_table.c - the column is
// discarded there) and BetterMapSelect.c's tables that DO use it are private, and not
// indexed by SceneId (DEFINE_SCENE_UNSET entries are skipped, so array index != SceneId
// value). Re-derive a SceneId-indexed table here the same mechanical way, filling
// unset slots with NULL, instead of guessing at an existing accessor.
#define DEFINE_SCENE(_name, _enumValue, _textId, _drawConfig, _restrictionFlags, _persistentCycleFlags, \
                     _entranceSceneId, _betterMapSelectIndex, humanName)                                 \
    humanName,
#define DEFINE_SCENE_UNSET(_enumValue) NULL,

static const char* sSceneMmHumanNames[SCENE_MAX] = {
#include "tables/scene_table.h"
};

#undef DEFINE_SCENE
#undef DEFINE_SCENE_UNSET

// Parallel human-readable name table, indexed by SceneIdOot. Used by the
// dual Map/Quest pages (patches 0006/0007) for display purposes.
static const char* sSceneOotHumanNames[SCENE_OOT_MAX] = {
    /* SCENE_OOT_DEKU_TREE */ "Deku Tree",
    /* SCENE_OOT_DODONGOS_CAVERN */ "Dodongos Cavern",
    /* SCENE_OOT_JABU_JABU */ "Jabu Jabu",
    /* SCENE_OOT_FOREST_TEMPLE */ "Forest Temple",
    /* SCENE_OOT_FIRE_TEMPLE */ "Fire Temple",
    /* SCENE_OOT_WATER_TEMPLE */ "Water Temple",
    /* SCENE_OOT_SPIRIT_TEMPLE */ "Spirit Temple",
    /* SCENE_OOT_SHADOW_TEMPLE */ "Shadow Temple",
    /* SCENE_OOT_BOTTOM_OF_THE_WELL */ "Bottom Of The Well",
    /* SCENE_OOT_ICE_CAVERN */ "Ice Cavern",
    /* SCENE_OOT_GANONS_TOWER */ "Ganons Tower",
    /* SCENE_OOT_GERUDO_TRAINING_GROUND */ "Gerudo Training Ground",
    /* SCENE_OOT_THIEVES_HIDEOUT */ "Thieves Hideout",
    /* SCENE_OOT_INSIDE_GANONS_CASTLE */ "Inside Ganons Castle",
    /* SCENE_OOT_GANONS_TOWER_COLLAPSE_INTERIOR */ "Ganons Tower Collapse Interior",
    /* SCENE_OOT_INSIDE_GANONS_CASTLE_COLLAPSE */ "Inside Ganons Castle Collapse",
    /* SCENE_OOT_TREASURE_BOX_SHOP */ "Treasure Box Shop",
    /* SCENE_OOT_DEKU_TREE_BOSS */ "Deku Tree Boss",
    /* SCENE_OOT_DODONGOS_CAVERN_BOSS */ "Dodongos Cavern Boss",
    /* SCENE_OOT_JABU_JABU_BOSS */ "Jabu Jabu Boss",
    /* SCENE_OOT_FOREST_TEMPLE_BOSS */ "Forest Temple Boss",
    /* SCENE_OOT_FIRE_TEMPLE_BOSS */ "Fire Temple Boss",
    /* SCENE_OOT_WATER_TEMPLE_BOSS */ "Water Temple Boss",
    /* SCENE_OOT_SPIRIT_TEMPLE_BOSS */ "Spirit Temple Boss",
    /* SCENE_OOT_SHADOW_TEMPLE_BOSS */ "Shadow Temple Boss",
    /* SCENE_OOT_GANONDORF_BOSS */ "Ganondorf Boss",
    /* SCENE_OOT_GANONS_TOWER_COLLAPSE_EXTERIOR */ "Ganons Tower Collapse Exterior",
    /* SCENE_OOT_MARKET_ENTRANCE_DAY */ "Market Entrance Day",
    /* SCENE_OOT_MARKET_ENTRANCE_NIGHT */ "Market Entrance Night",
    /* SCENE_OOT_MARKET_ENTRANCE_RUINS */ "Market Entrance Ruins",
    /* SCENE_OOT_BACK_ALLEY_DAY */ "Back Alley Day",
    /* SCENE_OOT_BACK_ALLEY_NIGHT */ "Back Alley Night",
    /* SCENE_OOT_MARKET_DAY */ "Market Day",
    /* SCENE_OOT_MARKET_NIGHT */ "Market Night",
    /* SCENE_OOT_MARKET_RUINS */ "Market Ruins",
    /* SCENE_OOT_TEMPLE_OF_TIME_EXTERIOR_DAY */ "Temple Of Time Exterior Day",
    /* SCENE_OOT_TEMPLE_OF_TIME_EXTERIOR_NIGHT */ "Temple Of Time Exterior Night",
    /* SCENE_OOT_TEMPLE_OF_TIME_EXTERIOR_RUINS */ "Temple Of Time Exterior Ruins",
    /* SCENE_OOT_KNOW_IT_ALL_BROS_HOUSE */ "Know It All Bros House",
    /* SCENE_OOT_TWINS_HOUSE */ "Twins House",
    /* SCENE_OOT_MIDOS_HOUSE */ "Midos House",
    /* SCENE_OOT_SARIAS_HOUSE */ "Sarias House",
    /* SCENE_OOT_KAKARIKO_CENTER_GUEST_HOUSE */ "Kakariko Center Guest House",
    /* SCENE_OOT_BACK_ALLEY_HOUSE */ "Back Alley House",
    /* SCENE_OOT_BAZAAR */ "Bazaar",
    /* SCENE_OOT_KOKIRI_SHOP */ "Kokiri Shop",
    /* SCENE_OOT_GORON_SHOP */ "Goron Shop",
    /* SCENE_OOT_ZORA_SHOP */ "Zora Shop",
    /* SCENE_OOT_POTION_SHOP_KAKARIKO */ "Potion Shop Kakariko",
    /* SCENE_OOT_POTION_SHOP_MARKET */ "Potion Shop Market",
    /* SCENE_OOT_BOMBCHU_SHOP */ "Bombchu Shop",
    /* SCENE_OOT_HAPPY_MASK_SHOP */ "Happy Mask Shop",
    /* SCENE_OOT_LINKS_HOUSE */ "Links House",
    /* SCENE_OOT_DOG_LADY_HOUSE */ "Dog Lady House",
    /* SCENE_OOT_STABLE */ "Stable",
    /* SCENE_OOT_IMPAS_HOUSE */ "Impas House",
    /* SCENE_OOT_LAKESIDE_LABORATORY */ "Lakeside Laboratory",
    /* SCENE_OOT_CARPENTERS_TENT */ "Carpenters Tent",
    /* SCENE_OOT_GRAVEKEEPERS_HUT */ "Gravekeepers Hut",
    /* SCENE_OOT_GREAT_FAIRYS_FOUNTAIN_MAGIC */ "Great Fairys Fountain Magic",
    /* SCENE_OOT_FAIRYS_FOUNTAIN */ "Fairys Fountain",
    /* SCENE_OOT_GREAT_FAIRYS_FOUNTAIN_SPELLS */ "Great Fairys Fountain Spells",
    /* SCENE_OOT_GROTTOS */ "Grottos",
    /* SCENE_OOT_REDEAD_GRAVE */ "Redead Grave",
    /* SCENE_OOT_GRAVE_WITH_FAIRYS_FOUNTAIN */ "Grave With Fairys Fountain",
    /* SCENE_OOT_ROYAL_FAMILYS_TOMB */ "Royal Familys Tomb",
    /* SCENE_OOT_SHOOTING_GALLERY */ "Shooting Gallery",
    /* SCENE_OOT_TEMPLE_OF_TIME */ "Temple Of Time",
    /* SCENE_OOT_CHAMBER_OF_THE_SAGES */ "Chamber Of The Sages",
    /* SCENE_OOT_CASTLE_COURTYARD_GUARDS_DAY */ "Castle Courtyard Guards Day",
    /* SCENE_OOT_CASTLE_COURTYARD_GUARDS_NIGHT */ "Castle Courtyard Guards Night",
    /* SCENE_OOT_CUTSCENE_MAP */ "Cutscene Map",
    /* SCENE_OOT_WINDMILL_AND_DAMPES_GRAVE */ "Windmill And Dampes Grave",
    /* SCENE_OOT_FISHING_POND */ "Fishing Pond",
    /* SCENE_OOT_CASTLE_COURTYARD_ZELDA */ "Castle Courtyard Zelda",
    /* SCENE_OOT_BOMBCHU_BOWLING_ALLEY */ "Bombchu Bowling Alley",
    /* SCENE_OOT_LON_LON_BUILDINGS */ "Lon Lon Buildings",
    /* SCENE_OOT_MARKET_GUARD_HOUSE */ "Market Guard House",
    /* SCENE_OOT_POTION_SHOP_GRANNY */ "Potion Shop Granny",
    /* SCENE_OOT_GANON_BOSS */ "Ganon Boss",
    /* SCENE_OOT_HOUSE_OF_SKULLTULA */ "House Of Skulltula",
    /* SCENE_OOT_HYRULE_FIELD */ "Hyrule Field",
    /* SCENE_OOT_KAKARIKO_VILLAGE */ "Kakariko Village",
    /* SCENE_OOT_GRAVEYARD */ "Graveyard",
    /* SCENE_OOT_ZORAS_RIVER */ "Zoras River",
    /* SCENE_OOT_KOKIRI_FOREST */ "Kokiri Forest",
    /* SCENE_OOT_SACRED_FOREST_MEADOW */ "Sacred Forest Meadow",
    /* SCENE_OOT_LAKE_HYLIA */ "Lake Hylia",
    /* SCENE_OOT_ZORAS_DOMAIN */ "Zoras Domain",
    /* SCENE_OOT_ZORAS_FOUNTAIN */ "Zoras Fountain",
    /* SCENE_OOT_GERUDO_VALLEY */ "Gerudo Valley",
    /* SCENE_OOT_LOST_WOODS */ "Lost Woods",
    /* SCENE_OOT_DESERT_COLOSSUS */ "Desert Colossus",
    /* SCENE_OOT_GERUDOS_FORTRESS */ "Gerudos Fortress",
    /* SCENE_OOT_HAUNTED_WASTELAND */ "Haunted Wasteland",
    /* SCENE_OOT_HYRULE_CASTLE */ "Hyrule Castle",
    /* SCENE_OOT_DEATH_MOUNTAIN_TRAIL */ "Death Mountain Trail",
    /* SCENE_OOT_DEATH_MOUNTAIN_CRATER */ "Death Mountain Crater",
    /* SCENE_OOT_GORON_CITY */ "Goron City",
    /* SCENE_OOT_LON_LON_RANCH */ "Lon Lon Ranch",
    /* SCENE_OOT_OUTSIDE_GANONS_CASTLE */ "Outside Ganons Castle",
    /* SCENE_OOT_TEST01 */ "Test01",
    /* SCENE_OOT_BESITU */ "Besitu",
    /* SCENE_OOT_DEPTH_TEST */ "Depth Test",
    /* SCENE_OOT_SYOTES */ "Syotes",
    /* SCENE_OOT_SYOTES2 */ "Syotes2",
    /* SCENE_OOT_SUTARU */ "Sutaru",
    /* SCENE_OOT_HAIRAL_NIWA2 */ "Hairal Niwa2",
    /* SCENE_OOT_SASATEST */ "Sasatest",
    /* SCENE_OOT_TESTROOM */ "Testroom",
};

const char* Scene_GetHumanName(CompatSceneRef ref) {
    if (Scene_RefIsOot(ref)) {
        if (ref.index < 0 || ref.index >= SCENE_OOT_MAX) {
            return "Unknown OOT Scene";
        }
        return sSceneOotHumanNames[ref.index];
    }

    // MM side: SceneId-indexed table re-derived above from scene_table.h's own
    // "Human readable name" column (DEFINE_SCENE arg 8) via the same macro-glob
    // mechanism z_scene_table.c and BetterMapSelect.c use.
    if (ref.index < 0 || ref.index >= SCENE_MAX) {
        return "Unknown MM Scene";
    }
    if (sSceneMmHumanNames[ref.index] == NULL) {
        return "Unset MM Scene";
    }
    return sSceneMmHumanNames[ref.index];
}

SceneOrigin Scene_GetCurrentOrigin(PlayState* play) {
    return gSaveContext.save.shipSaveInfo.so2h.currentWorld;
}

void Scene_SetCurrentOrigin(PlayState* play, SceneOrigin origin) {
    gSaveContext.save.shipSaveInfo.so2h.currentWorld = origin;
}
