#pragma once

#include <string>

// Infra-only helpers for consuming OOT-derived assets that O2RMerger placed into the merged
// mm.o2r archive (see Extractor/O2RMerger.h/.cpp for the merge algorithm).
//
// SO2H FIX (this pass): the merge tool actually prefixes the resource path's TOP-LEVEL FOLDER
// with "ootr_" (`O2RMerger::WithPrefixedTopFolder`, e.g. "textures/icon_item_static/gFooTex" ->
// "ootr_textures/icon_item_static/gFooTex"), NOT the basename - and OOT/MM don't even share
// top-folder names for the same kind of asset (MM's own item icons live under
// "icon_item_static_yar/", OOT's under "textures/icon_item_static/"), so there is no shared
// "originalPath" to begin with. `ResolveOotPath()` below replaces the old (unused, and
// incorrect for this reason) `GetOotIconVariant()` with the right shape: callers pass the
// OOT-side relative resource path as it exists in OOT/soh's own o2r (no "__OTR__", no "ootr_"),
// e.g. "textures/icon_item_static/gItemIconDekuStickTex", and get back the full runtime
// TexturePtr-style path ("__OTR__ootr_textures/icon_item_static/gItemIconDekuStickTex") to
// embed directly wherever MM's own code would otherwise embed a static texture path string
// (see OotItemIcons.h for the first real consumer).
namespace OotAssets {

// True if the merged archive contains any OOT-derived resource, i.e. the current mm.o2r was
// produced by (or migrated through) O2RMerger::MergeOotIntoMm. Cheap/cached after first call.
bool IsOotContentAvailable();

// Given an OOT-relative resource path exactly as it appears in OOT/soh's own o2r (e.g.
// "textures/icon_item_static/gItemIconDekuStickTex"), returns the full runtime path to the
// OOT-derived copy that O2RMerger placed into the merged mm.o2r under an "ootr_"-prefixed TOP
// FOLDER, including the "__OTR__" scheme prefix MM's own texture path strings use (e.g.
// "__OTR__ootr_textures/icon_item_static/gItemIconDekuStickTex"). Does not check existence;
// callers that care should combine this with ResourceManager's own
// HasFile/LoadResource-returns-null checks (or gate on IsOotContentAvailable() first, as
// OotItemIcons.cpp does).
std::string ResolveOotPath(const std::string& ootRelativePath);

} // namespace OotAssets
