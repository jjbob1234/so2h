#include "DeveloperTools.h"
#include "BenPort.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/OotAssets.h"

void ValidateOotIconAssetsFor100PercentSave();

extern "C" {
#include "macros.h"
#include "z64actor.h"
#include "z64save.h"
#include "variables.h"
#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"
#include "overlays/gamestates/ovl_select/z_select.h"

void Sram_InitDebugSave(void);
void Flags_SetWeekEventReg(s32 flag);
void Inventory_ChangeUpgrade(s16 upgrade, u32 value);
void Inventory_SetWorldMapCloudVisibility(s16 tingleIndex);

extern u16 sPersistentCycleWeekEventRegs[ARRAY_COUNT(gSaveContext.save.saveInfo.weekEventReg)];
}

#define CVAR_DEBUG_MODE_NAME "gDeveloperTools.DebugEnabled"
#define CVAR_DEBUG_MODE CVarGetInteger(CVAR_DEBUG_MODE_NAME, 0)

#define CVAR_SAVE_FILE_MODE_NAME "gDeveloperTools.DebugSaveFileMode"
#define CVAR_SAVE_FILE_MODE CVarGetInteger(CVAR_SAVE_FILE_MODE_NAME, DEBUG_SAVE_INFO_NONE)

void SetSaveFileInfo() {
    u8 playerName[8];

    // Copy player name and set back after debug save init
    memcpy(playerName, gSaveContext.save.saveInfo.playerData.playerName, sizeof(playerName));

    Sram_InitDebugSave();

    memcpy(gSaveContext.save.saveInfo.playerData.playerName, playerName, sizeof(playerName));

    // Place link at entrance from clock tower
    gSaveContext.save.entrance = ENTRANCE(SOUTH_CLOCK_TOWN, 0);
    gSaveContext.save.cutsceneIndex = 0;
    gSaveContext.save.saveInfo.checksum = 1;

    // Prevent first Song of Time reset from forcing Deku Link and having to learn Song of Healing
    gSaveContext.save.saveInfo.playerData.threeDayResetCount = 1;

    if (CVAR_SAVE_FILE_MODE == DEBUG_SAVE_INFO_COMPLETE) {
        gSaveContext.save.saveInfo.playerData.doubleDefense = true;
        gSaveContext.save.saveInfo.playerData.health = 20 * 0x10;
        gSaveContext.save.saveInfo.playerData.healthCapacity = 20 * 0x10;
        gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired = true;
        gSaveContext.save.saveInfo.playerData.magicLevel = 2;
        gSaveContext.save.saveInfo.playerData.magic = MAGIC_DOUBLE_METER;
        gSaveContext.save.saveInfo.playerData.owlActivationFlags = (1 << (OWL_WARP_STONE_TOWER + 1)) - 1;
        gSaveContext.save.saveInfo.playerData.owlWarpId = OWL_WARP_CLOCK_TOWN;

        gSaveContext.save.saveInfo.inventory.defenseHearts = 20;
        gSaveContext.save.saveInfo.regionsVisited = (1 << REGION_MAX) - 1;
        gSaveContext.magicCapacity = MAGIC_DOUBLE_METER;

        Inventory_ChangeUpgrade(UPG_WALLET, 2);
        Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3);
        Inventory_ChangeUpgrade(UPG_QUIVER, 3);

        for (int32_t i = 0; i < TINGLE_MAP_MAX; i++) {
            Inventory_SetWorldMapCloudVisibility(i);
        }

        gSaveContext.save.saveInfo.playerData.rupees = CUR_CAPACITY(UPG_WALLET);
        AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
        AMMO(ITEM_BOMB) = AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
        AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
        AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
        AMMO(ITEM_MAGIC_BEANS) = 20;
        AMMO(ITEM_POWDER_KEG) = 1;

        SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
        SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_GILDED);
        BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_B) = ITEM_SWORD_GILDED;

        for (int32_t i = SLOT_BOTTLE_1; i <= SLOT_BOTTLE_6; i++) {
            if (gSaveContext.save.saveInfo.inventory.items[i] == ITEM_NONE) {
                gSaveContext.save.saveInfo.inventory.items[i] = ITEM_BOTTLE;
            }
        }

        for (int32_t i = QUEST_REMAINS_ODOLWA; i <= QUEST_BOMBERS_NOTEBOOK; i++) {
            if (i != QUEST_SHIELD && i != QUEST_SWORD && i != QUEST_SONG_SARIA && i != QUEST_SONG_SUN) {
                SET_QUEST_ITEM(i);
            }
        }

        // Use the persistent cycle events to set what a 100% save would normally keep
        for (int32_t i = 0; i < ARRAY_COUNT(sPersistentCycleWeekEventRegs); i++) {
            u16 isPersistentBits = sPersistentCycleWeekEventRegs[i];

            // Force all bits on
            gSaveContext.save.saveInfo.weekEventReg[i] = 0xFF;

            // Then unset any bits that aren't persistent
            for (int32_t j = 0; j < 8; j++) {
                if (!(isPersistentBits & 3)) {
                    gSaveContext.save.saveInfo.weekEventReg[i] =
                        gSaveContext.save.saveInfo.weekEventReg[i] & (0xFF ^ (1 << j));
                }
                isPersistentBits >>= 2;
            }
        }

        ValidateOotIconAssetsFor100PercentSave();
    }
}

// Scope note (surfaced to user): this does NOT add any real pause-menu UI display for OOT
// items - in an unmodded base game there's no OOT item content in the 100% debug save to
// show there (all OOT-derived pause-menu icon/map textures merged in under "ootr_" are
// currently unreferenced by any consumer). This only validates/logs that the merge actually
// placed those "ootr_" icon assets where OotAssets.h's future consumers expect them, so a
// broken merge is caught here instead of silently at first real use.
void ValidateOotIconAssetsFor100PercentSave() {
    if (!OotAssets::IsOotContentAvailable()) {
        SPDLOG_INFO("DeveloperTools: 100% debug save - no merged OOT content present (unmerged "
                    "mm.o2r); skipping ootr_ pause-menu icon validation.");
        return;
    }

    // Sample of real per-texture OOT relative paths (not folder-level guesses) now that
    // OotItemIcons.cpp is a real consumer - one icon, one item name, one equipment icon.
    static const char* const kPauseMenuIconOriginalPaths[] = {
        "textures/icon_item_static/gItemIconDekuStickTex",
        "textures/item_name_static/gDekuStickItemNameENGTex",
        "textures/icon_item_static/gItemIconSwordKokiriTex",
    };

    auto archiveManager = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    for (const char* originalPath : kPauseMenuIconOriginalPaths) {
        // ResolveOotPath() returns a TexturePtr-style "__OTR__..." string; ArchiveManager::HasFile
        // expects a raw zip entry name (no "__OTR__" scheme prefix), so strip it back off here.
        std::string texturePtrPath = OotAssets::ResolveOotPath(originalPath);
        std::string entryName = texturePtrPath.substr(7); // 7 == length of the "__OTR__" scheme prefix
        if (archiveManager == nullptr || !archiveManager->HasFile(entryName)) {
            SPDLOG_WARN("DeveloperTools: 100% debug save - expected merged OOT pause-menu icon "
                        "asset missing: {}",
                        entryName);
        } else {
            SPDLOG_INFO("DeveloperTools: 100% debug save - merged OOT pause-menu icon asset "
                        "present: {}",
                        entryName);
        }
    }
}

void RegisterDebugSaveCreate() {
    COND_HOOK(OnSaveInit, CVAR_SAVE_FILE_MODE != DEBUG_SAVE_INFO_NONE && CVAR_DEBUG_MODE,
              [](s16 fileNum) { SetSaveFileInfo(); });
}

#define CVAR_PREVENT_ACTOR_UPDATE_NAME "gDeveloperTools.PreventActorUpdate"
#define CVAR_PREVENT_ACTOR_UPDATE CVarGetInteger(CVAR_PREVENT_ACTOR_UPDATE_NAME, 0)

void RegisterPreventActorUpdateHooks() {
    COND_HOOK(ShouldActorUpdate, CVAR_PREVENT_ACTOR_UPDATE && CVAR_DEBUG_MODE,
              [](Actor* actor, bool* result) { *result = false; });
}

#define CVAR_PREVENT_ACTOR_DRAW_NAME "gDeveloperTools.PreventActorDraw"
#define CVAR_PREVENT_ACTOR_DRAW CVarGetInteger(CVAR_PREVENT_ACTOR_DRAW_NAME, 0)

void RegisterPreventActorDrawHooks() {
    COND_HOOK(ShouldActorDraw, CVAR_PREVENT_ACTOR_DRAW && CVAR_DEBUG_MODE,
              [](Actor* actor, bool* result) { *result = false; });
}

#define CVAR_PREVENT_ACTOR_INIT_NAME "gDeveloperTools.PreventActorInit"
#define CVAR_PREVENT_ACTOR_INIT CVarGetInteger(CVAR_PREVENT_ACTOR_INIT_NAME, 0)

void RegisterPreventActorInitHooks() {
    static HOOK_ID hookId = 0;
    if (hookId != 0) {
        GameInteractor::Instance->UnregisterGameHookForFilter<GameInteractor::ShouldActorInit>(hookId);
        hookId = 0;
    }

    if (CVAR_PREVENT_ACTOR_INIT && CVAR_DEBUG_MODE) {
        hookId = GameInteractor::Instance->RegisterGameHookForFilter<GameInteractor::ShouldActorInit>(
            GameInteractor::HookFilter::SActorNotPlayer, [](Actor* actor, bool* result) { *result = false; });
    }
}

void RegisterDebugMode() {
    // Disable various debug options when toggled off
    if (!CVAR_DEBUG_MODE) {
        CVarClear(CVAR_SAVE_FILE_MODE_NAME);
        CVarClear(CVAR_PREVENT_ACTOR_UPDATE_NAME);
        CVarClear(CVAR_PREVENT_ACTOR_DRAW_NAME);
        CVarClear(CVAR_PREVENT_ACTOR_INIT_NAME);
        CVarClear("gDeveloperTools.DisableObjectDependency");

        if (gPlayState != NULL) {
            gPlayState->frameAdvCtx.enabled = false;
        }
    }

    COND_HOOK(OnGameStateMainStart, CVAR_DEBUG_MODE, []() {
        Input* input = CONTROLLER1(gGameState);

        auto mask = CVarGetInteger("gDeveloperTools.MapSelectBtn", BTN_Z | BTN_L | BTN_R);

        if (CHECK_BTN_ANY(gGameState->input[0].press.button, mask) &&
            CHECK_BTN_ALL(gGameState->input[0].cur.button, mask)) {
            STOP_GAMESTATE(gGameState);
            gSaveContext.gameMode = GAMEMODE_NORMAL;
            gSaveContext.nextDayTime = NEXT_TIME_NONE;
            gSaveContext.nextTransitionType = TRANS_NEXT_TYPE_DEFAULT;
            gSaveContext.prevHudVisibility = HUD_VISIBILITY_ALL;
            SET_NEXT_GAMESTATE(gGameState, MapSelect_Init, sizeof(MapSelectState));
        }
    });

    // For lack of a better place, sticking this here now
    COND_HOOK(OnGameStateMainStart, true, []() {
        Input* input = CONTROLLER1(gGameState);

        auto mask = CVarGetInteger("gSettings.ResetBtn", BTN_CUSTOM_MODIFIER2);

        if (CHECK_BTN_ANY(gGameState->input[0].press.button, mask) &&
            CHECK_BTN_ALL(gGameState->input[0].cur.button, mask)) {
            std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
                ->Dispatch("reset");
        }
    });
}

RegisterShipInitFunc initFuncDebugMode(RegisterDebugMode, { CVAR_DEBUG_MODE_NAME });
RegisterShipInitFunc initFuncSaveFile(RegisterDebugSaveCreate, { CVAR_SAVE_FILE_MODE_NAME, CVAR_DEBUG_MODE_NAME });
RegisterShipInitFunc initFuncActorUpdate(RegisterPreventActorUpdateHooks,
                                         { CVAR_PREVENT_ACTOR_UPDATE_NAME, CVAR_DEBUG_MODE_NAME });
RegisterShipInitFunc initFuncActorDraw(RegisterPreventActorDrawHooks,
                                       { CVAR_PREVENT_ACTOR_DRAW_NAME, CVAR_DEBUG_MODE_NAME });
RegisterShipInitFunc initFuncActorInit(RegisterPreventActorInitHooks,
                                       { CVAR_PREVENT_ACTOR_INIT_NAME, CVAR_DEBUG_MODE_NAME });
