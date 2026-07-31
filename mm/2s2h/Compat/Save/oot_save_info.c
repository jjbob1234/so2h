/**
 * so2h [Save] patch 0004/0005 - OOT item/equipment save data helpers.
 * See oot_save_info.h for the rationale.
 */

#include "oot_save_info.h"
#include <string.h>

#define ITEM_NONE_OOT 0xFF // mirrors reference/soh's ITEM_NONE (0xFF); OOT items are a separate id space from MM's

void OotSaveInfo_InitDefault(OotSaveInfo* oot) {
    memset(oot, 0, sizeof(OotSaveInfo));

    oot->magic = OOT_SAVE_INFO_MAGIC;
    oot->version = OOT_SAVE_INFO_VERSION;

    memset(oot->equips.buttonItems, ITEM_NONE_OOT, sizeof(oot->equips.buttonItems));
    memset(oot->inventory.items, ITEM_NONE_OOT, sizeof(oot->inventory.items));
}

bool OotSaveInfo_Validate(OotSaveInfo* oot) {
    if (oot->magic != OOT_SAVE_INFO_MAGIC || oot->version != OOT_SAVE_INFO_VERSION) {
        OotSaveInfo_InitDefault(oot);
        return false;
    }

    return true;
}

// Real per-slot OOT item ids for the 24-slot item grid, ported verbatim from reference/soh's
// SaveManager::InitFileDebug() `sItems` array (soh/SaveManager.cpp) so the debug-complete save
// shows the exact same varied inventory (Deku Stick, Deku Nut, Bomb, Bow, ... Weird Egg) that
// SoH itself uses, instead of every slot resolving to item id 0x00 (Deku Stick). Indices line up
// 1:1 with OotItemIcons.cpp's `kOotItemArt` table (both are indexed by soh's raw ItemID enum).
// ITEM_POCKET_EGG (slot 22) is 0x2D, one past OOT_ITEM_ICON_MAX_ID (0x2C), so it has no icon art
// yet and falls back to the pre-existing outline-box draw - same documented fallback behavior
// as any other id outside the known range, not a bug.
static const u8 kOotDebugCompleteItems[24] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, // ITEM_STICK, ITEM_NUT, ITEM_BOMB, ITEM_BOW, ITEM_ARROW_FIRE, ITEM_DINS_FIRE
    0x06, 0x07, 0x09, 0x0A, 0x0C, 0x0D, // ITEM_SLINGSHOT, ITEM_OCARINA_FAIRY, ITEM_BOMBCHU, ITEM_HOOKSHOT, ITEM_ARROW_ICE, ITEM_FARORES_WIND
    0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, // ITEM_BOOMERANG, ITEM_LENS, ITEM_BEAN, ITEM_HAMMER, ITEM_ARROW_LIGHT, ITEM_NAYRUS_LOVE
    0x14, 0x15, 0x16, 0x17, 0x2D, 0x21, // ITEM_BOTTLE, ITEM_POTION_RED, ITEM_POTION_GREEN, ITEM_POTION_BLUE, ITEM_POCKET_EGG, ITEM_WEIRD_EGG
};

void OotSaveInfo_FillDebugComplete(OotSaveInfo* oot) {
    OotSaveInfo_InitDefault(oot);

    memcpy(oot->inventory.items, kOotDebugCompleteItems, sizeof(oot->inventory.items));
    memset(oot->inventory.ammo, 99, sizeof(oot->inventory.ammo));
    oot->inventory.equipment = 0xFFFF;
    oot->inventory.upgrades = 0xFFFFFFFF;
    oot->inventory.questItems = 0xFFFFFFFF;
    memset(oot->inventory.dungeonItems, 0xFF, sizeof(oot->inventory.dungeonItems));
    memset(oot->inventory.dungeonKeys, 99, sizeof(oot->inventory.dungeonKeys));

    memset(oot->equips.buttonItems, 0, sizeof(oot->equips.buttonItems));
    oot->equips.cButtonSlots[0] = 1;
    oot->equips.cButtonSlots[1] = 2;
    oot->equips.cButtonSlots[2] = 3;
    oot->equips.equipment = 0x1111; // first (best available) piece of every equipment type
}
