# Task: OOT pause page real icon/name/quantity art (this session)

## Findings (confirmed via source read)
- OOT icon textures live in merged mm.o2r under top-folder-prefixed path:
  `__OTR__ootr_textures/icon_item_static/<gItemIcon*Tex symbol>` (32x32 RGBA32).
  Source symbol list: reference `env/soh/soh/assets/textures/icon_item_static/icon_item_static.h`.
- OOT item name (ENG) textures: `__OTR__ootr_textures/item_name_static/<g*ItemNameENGTex symbol>`
  (128x16 IA4). Source: `env/soh/soh/assets/textures/item_name_static/item_name_static.h`.
- `env/soh/soh/src/code/z_inventory.c` `gItemIcons[]` is indexed directly by `ItemID` (0x00
  ITEM_STICK .. 0x2C ITEM_SOLD_OUT), 45 entries, 1:1 order match with icon_item_static.h's
  declaration order AND with z_kaleido_scope_PAL.c's `iconNameTextures[LANGUAGE_ENG][]` order.
  `mm/include/z64save.h` OotInventory.items[] "mirrors soh Inventory.items" -> stores this same
  ItemID byte directly (OOT_ITEM_NONE 0xFF already used correctly in kaleido_compat_menu.c).
- Equipment icons/names are NOT in that same ItemID-indexed array for the tiers we need generically;
  used dedicated per-(EquipmentType,tier) symbols instead (KokiriSword/MasterSword/BiggoronSword,
  ShieldDeku/Hylian/Mirror, TunicKokiri/Goron/Zora, BootsKokiri/Iron/Hover) - both icon + ENG name
  variants confirmed to exist for all 12.
- BUG FOUND: `OotAssets::GetOotIconVariant()` (mm/2s2h/OotAssets.cpp) prefixes the *basename*
  with `ootr_`, but the real merge tool (`O2RMerger.cpp` `WithPrefixedTopFolder`) prefixes the
  *first path component* (top folder), e.g. `textures/...` -> `ootr_textures/...`. Since OOT and
  MM don't even share top-folder names (MM icons live under `icon_item_static_yar/`, OOT under
  `textures/icon_item_static/`), the old function's "originalPath is an MM path, tweak its
  basename" design was unusable for this anyway. Fixing it to take the OOT-side relative path
  and prefix the top folder correctly (this session's OotAssets.cpp/h edit).

## Plan
1. Fix `OotAssets::GetOotIconVariant` -> rename/refactor to `OotAssets::ResolveOotPath(relPath)`
   taking an OOT-relative resource path (e.g. `textures/icon_item_static/gItemIconDekuStickTex`,
   no `__OTR__`) and returning the full `__OTR__ootr_<toplevel>/...` runtime path, matching
   `WithPrefixedTopFolder` exactly. Keep old name as a thin deprecated wrapper only if cheap;
   otherwise just fix in place (no other caller exists yet per header comment).
2. New `mm/2s2h/OotItemIcons.h/.cpp`: static tables (45 items + 12 equipment pieces) of
   {icon relpath, ENG name relpath}, resolved once via `OotAssets::ResolveOotPath` into cached
   `std::string`s, exposed to C via `extern "C"` accessor functions returning `const char*`
   (TexturePtr-compatible): `OotItemIcons_GetItemIconPath(u8 itemId)`,
   `OotItemIcons_GetItemNamePath(u8 itemId)`, `OotItemIcons_GetEquipIconPath(u8 equipType, u8 tier)`,
   `OotItemIcons_GetEquipNamePath(u8 equipType, u8 tier)`. Return NULL/fallback for out-of-range
   or when `OotAssets::IsOotContentAvailable()` is false (base-game-only install safety).
3. Edit `kaleido_compat_menu.c`:
   - `KaleidoScope_DrawItemSelectOot`: replace outline-box draw with
     `KaleidoScope_DrawTexQuadRGBA32(gfxCtx, iconPath, 32, 32, 0)` when a resolved path exists,
     falling back to the existing outline box otherwise (keeps base-game safety + acts as a
     visible "missing art" indicator instead of crashing).
   - `KaleidoScope_DrawEquipmentOot`: same swap using equip icon path.
   - Add name+quantity overlay draw (new small helper) for both pages: when the cursor is on a
     valid slot, draw the resolved 128x16 IA4 name texture via `Gfx_DrawTexQuad4b` at the same
     screen position MM's native name panel uses (mirrors `KaleidoScope_UpdateNamePanel` /
     `Kaleido_LoadItemNameStatic` pattern, but simpler: no nameSegment DMA staging needed since
     these are already static resource paths, not a two-step load+draw).
   - Ammo/quantity count: OOT ammo-bearing items map via OOT's own `SLOT_*`/ammo array
     (`OotInventory.ammo[16]`) - reuse `KaleidoScope_DrawAmmoCount`-style digit draw
     (`gAmmoDigitTextures`/existing MM digit textures - no OOT-specific digit art needed, digits
     are just 0-9, MM's own digit textures are fine to reuse here) for slots that carry ammo.
4. Build/commit/push per established workflow; update handover once pushed.

## Status: implementation done locally, not yet committed/pushed/built.

Files changed:
- mm/2s2h/OotAssets.h/.cpp - GetOotIconVariant -> ResolveOotPath (fixed top-folder-prefix bug).
- mm/2s2h/DeveloperTools/DeveloperTools.cpp - updated the one caller to the new function/paths.
- mm/2s2h/OotItemIcons.h/.cpp - NEW. 45-entry item table + 4x5 equipment table, cached
  resolved paths, extern "C" accessors.
- mm/2s2h/Compat/Menu/kaleido_compat_menu.c - real icon draw (with outline fallback), ammo
  digit overlay (own vertex-space helper, NOT Gfx_DrawTexRectIA8 - different coord space),
  cursorItem[] feed for name panel, Kaleido_LoadItemNameStaticOot/Kaleido_LoadEquipNameStaticOot.
- mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h - prototypes for the two
  new Load*NameStaticOot functions.
- mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c -
  KaleidoScope_UpdateNamePanel now branches on PAUSE_ITEM_OOT/PAUSE_EQUIP_OOT.

Not done / risks:
- Never locally compiled (no local build attempted - this repo's build is CI-only per
  established workflow; user watches CI personally). Reviewed by hand multiple times for
  signature/type/coordinate-space correctness (esp. the vertex-space vs screen-pixel-space
  mixup that was caught and fixed for the ammo digits).
- Equip page's row order (row 0-3) is assumed to be EquipmentType order (sword,shield,tunic,
  boots) matching soh's enum - not independently re-verified against how OotSaveInfo's
  `equipment` bitfield rows were originally populated in an earlier session; if that ordering
  differs, `OotItemIcons_GetEquipIconPath(row, tier)`'s `row` arg would need remapping.
- No new save-field/gameplay logic touched, per scope.

Next: commit + push, then (per established workflow) wait for user's explicit "check"/
"status" before polling CI, then deliver a new build the same way as before.
