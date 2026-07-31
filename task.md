# so2h-oot-menu-scaffold progress

Plan: /home/user/plan.md (approved). Branch: so2h-oot-menu-scaffold in /home/user/ProjectZ/env/2s2h.

## Done
- [x] z64save.h: added OotItemEquips/OotInventory/OotSaveInfo (magic+version guarded) nested
      in So2hSaveInfo as `.oot`, replacing the old TODO comment. Mirrors reference/soh's
      ItemEquips (buttonItems[4]+cButtonSlots[3]+equipment) / Inventory (items[24], ammo[16],
      equipment, upgrades, questItems, dungeonItems[20], dungeonKeys[19]) 1:1.
- [x] Confirmed kaleido_compat_menu.c stubs (4 funcs, all no-op, patch 0004/0005 TODOs).
- [x] Confirmed KaleidoScope_SetVertices layout (z_kaleido_scope_NES.c ~2549-3013): item grid
      alloc gated `pageIndex != PAUSE_QUEST`, mask grid gated `pageIndex != PAUSE_MAP`, uses
      ITEM_GRID_ROWS/COLS=4/6, MASK_GRID_ROWS/COLS=4/6 (z64pause_menu.h:266-272).
- [x] Confirmed so2h struct access pattern: gSaveContext.save.shipSaveInfo.so2h.* ; init sites
      Sram_InitNewSave (z_sram_NES.c:992) and Sram_InitDebugSave (z_sram_NES.c:1213), both set
      shipSaveInfo.* fields directly around lines 1014-1021 / 1246-1253.

## Next (in order)
1. Add OotSaveInfo_InitDefault()/OotSaveInfo_Validate() helper (new file under
   mm/2s2h/Compat/Save/, mirroring z_scene_origin.c style) — Validate checks magic/version,
   zero-inits+re-stamps if mismatched (old-save-safe default).
2. Call OotSaveInfo_InitDefault() from Sram_InitNewSave + Sram_InitDebugSave; call
   OotSaveInfo_Validate() from wherever saves are loaded from flash (find Sram_OpenSave /
   the load path - NOT YET LOCATED, need to grep).
3. Add itemOotPageVtx/itemOotVtx alloc block (mirror itemPageVtx/itemVtx, gate on
   `pageIndex != PAUSE_ITEM_OOT`) and equipOotPageVtx/equipOotVtx alloc block (mirror
   maskPageVtx/maskVtx, gate on `pageIndex != PAUSE_EQUIP_OOT`) in KaleidoScope_SetVertices.
4. Implement real KaleidoScope_DrawItemSelectOot/UpdateItemCursorOot/DrawEquipmentOot/
   UpdateEquipCursorOot in kaleido_compat_menu.c reading from gSaveContext...so2h.oot,
   reusing 2s2h's existing item-icon draw primitives + OotAssets.h icon resolution
   (mm/2s2h/OotAssets.h/.cpp — NOT YET OPENED, open before writing draw code).
5. tools/check_save_field_offsets.py — parse z64save.h field offsets (regex on `/* 0x.. */`
   comments), fail if any two fields in the same struct overlap or a new field isn't
   strictly after the previous one's end offset.
6. mm/2s2h/DeveloperTools/DeveloperTools.cpp RegisterDebugSaveCreate — extend to also fully
   fill gSaveContext...so2h.oot (all items/equipment maxed), NOT YET OPENED this session.
7. Remove a3d2e334e blank-MM-placeholder fallback in z_kaleido_scope_NES.c (~807-840,
   920-955, 987-1020, 1148-1180) now real itemOotPageVtx/equipOotPageVtx content exists.
8. Build (per prior CI workflow), commit, push. Report only when user asks for status.

## Key facts
- OOT item grid = 24 slots (soh Inventory.items[24]) — same size as MM's own ITEM grid
  (ITEM_GRID_ROWS*COLS=24), so OOT Item page can reuse the exact same 4x6 grid geometry.
- reference/soh path: /home/user/ProjectZ/reference/soh (soh/include/z64item.h, z64save.h).
- Doll render explicitly OUT of scope this pass (leave playerSegment/playerSkelAnime
  unallocated, TODO marker only).
