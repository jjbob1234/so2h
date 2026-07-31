#ifndef OOT_SAVE_INFO_H
#define OOT_SAVE_INFO_H

/**
 * so2h [Save] patch 0004/0005 - OOT item/equipment save data helpers.
 *
 * `OotSaveInfo` (z64save.h, nested at gSaveContext.save.shipSaveInfo.so2h.oot) backs the
 * OOT Item/Equip pause pages. These helpers are the single place that initializes or
 * repairs that struct, so every call site (new save, debug save, and after loading a
 * save from disk) goes through the same magic/version-guarded logic instead of each
 * reimplementing it.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "global.h"

// Zero-fills and re-stamps `oot` with a fresh, empty-but-valid OotSaveInfo (no items,
// no equipment, magic/version set). Used for brand new saves.
void OotSaveInfo_InitDefault(OotSaveInfo* oot);

// Called after loading any save (new file, owl save, or from-disk JSON load). If
// `magic`/`version` don't match the current build's expectations - e.g. the save
// predates patch 0004, or came from a so2h build with an incompatible OotSaveInfo
// layout - resets `oot` to a fresh default instead of trusting stale/foreign data.
// Returns true if the existing data was kept as-is, false if it was reset.
bool OotSaveInfo_Validate(OotSaveInfo* oot);

// Used by DeveloperTools' 100%-debug-save creation to fully complete the OOT side
// (every item/equipment slot maxed), mirroring how the debug save already completes
// MM's own inventory.
void OotSaveInfo_FillDebugComplete(OotSaveInfo* oot);

#ifdef __cplusplus
}
#endif

#endif // OOT_SAVE_INFO_H
