#pragma once

#include <string>

// Infra-only helpers for consuming OOT-derived assets that O2RMerger placed into the merged
// mm.o2r under an "ootr_"/"ootv_" basename prefix (see Extractor/O2RMerger.h for the merge
// algorithm). In an unmodded base game there is no OOT item content available to actually
// display via pause-menu icons yet, so nothing calls GetOotIconVariant() today - this exists
// so a future consumer (e.g. real OOT item content in the inventory) has a ready-made,
// already-wired lookup instead of reinventing the "ootr_" prefix convention ad hoc.
namespace OotAssets {

// True if the merged archive contains any OOT-derived resource, i.e. the current mm.o2r was
// produced by (or migrated through) O2RMerger::MergeOotIntoMm. Cheap/cached after first call.
bool IsOotContentAvailable();

// Given the ORIGINAL (unprefixed) resource path as it exists in MM/OOT's own o2r (e.g.
// "textures/icon_item_static/icon_item_static"), returns the path to the OOT-derived variant
// that O2RMerger placed alongside it ("textures/icon_item_static/ootr_icon_item_static"),
// prefixing only the basename per the merge algorithm. Does not check existence; callers that
// care should combine this with ResourceManager's own HasFile/LoadResource-returns-null checks.
std::string GetOotIconVariant(const std::string& originalPath);

} // namespace OotAssets
