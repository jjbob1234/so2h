#pragma once

#include <string>

// Shared merge logic used both by the in-game "Extract ROM" flow (BenPort.cpp) and by the
// standalone merge_o2r_cli debug tool. No reimplementation between the two consumers.
//
// Merge algorithm (final, user-specified):
//   Base = mmPath unchanged. MM's own files are NEVER replaced/overwritten by the merge,
//   whether or not a same-named OOT file exists.
//   For every file in ootPath:
//     - path is a resource/non-code asset (under "objects/", "textures/", "audio/",
//       "background(s)/", "skeletons/", "animations/", "materials/", or "matrices/", at any
//       depth) -> always included, with the basename prefixed "ootr_", whether or not it
//       collides with an MM path. Directory portion of the path is unchanged.
//     - else if the path does not exist in mmPath -> included as-is.
//     - else (collision on a code/text-adjacent path, e.g. "text/" message tables or scene
//       data):
//         - by default, skipped (MM's file wins, untouched).
//         - if basename is present in kOotvEscapeHatchBasenames (i.e. it's important for the
//           current project) -> ALSO included, in addition to the MM version, under the same
//           directory with basename prefixed "ootv_".
//   Special cases:
//     - root-level file named "version" is always included as "oot_version" (a provenance
//       marker string; not consumed by any loader, never collides with MM's own "version").
//     - root-level file named "portVersion" is always skipped (not preserved at all).
namespace O2RMerger {

// Basenames (original OOT basename, before any prefixing) that should additionally be kept
// under an "ootv_" prefix even though they collide with an MM path that otherwise wins.
// This is currently infra-only: no unmodded-MM code path consumes these yet (see
// OotAssets.h). Kept as a named, documented escape hatch for future consumers rather than
// silently dropping OOT variants of files MM also ships.
extern const char* const kOotvEscapeHatchBasenames[];
extern const size_t kOotvEscapeHatchBasenamesCount;

// Returns true when `basename` (no directory component) is in the ootv escape-hatch list.
bool IsOotvEscapeHatchBasename(const std::string& basename);

// Merges oot.o2r into mm.o2r producing mergedPath, per the algorithm above.
// mmPath must exist. ootPath must exist. mergedPath is overwritten if it already exists.
// Returns true on success; on failure returns false and never leaves a partially-written
// file at mergedPath (writes to a temp file and renames on success).
bool MergeOotIntoMm(const std::string& mmPath, const std::string& ootPath, const std::string& mergedPath,
                    std::string* outError = nullptr);

} // namespace O2RMerger
