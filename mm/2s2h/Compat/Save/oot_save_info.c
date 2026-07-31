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

void OotSaveInfo_FillDebugComplete(OotSaveInfo* oot) {
    // SO2H TODO (patch 0004/0005 follow-up): this fills every slot/bit generically so the
    // OOT Item/Equip pages have real, non-empty content to scaffold/test against. It does
    // NOT yet assign real per-slot OOT item ids (that requires porting reference/soh's
    // item table into KaleidoScope_DrawItemSelectOot first, see kaleido_compat_menu.c) -
    // once that table exists, replace the item/ammo loops below with the same slot
    // assignments SoH's own debug save uses so the grid shows correct icons, not just
    // "some item is present in every slot."
    OotSaveInfo_InitDefault(oot);

    memset(oot->inventory.items, 0, sizeof(oot->inventory.items)); // 0 = first valid OOT item id in every slot
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
