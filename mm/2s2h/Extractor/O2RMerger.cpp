#include "O2RMerger.h"

#include <zip.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace O2RMerger {

// Infra-only escape hatch (see header comment). Empty today: in an unmodded base game there
// is no OOT item content available to display via pause-menu icons, so nothing needs an
// "ootv_" variant yet. Left as a real, working table (not a stub) so a future consumer can
// add a basename here with zero merge-logic changes.
// A single nullptr placeholder keeps this a valid array on MSVC (which, unlike GCC/Clang,
// rejects zero-size arrays with C2466); kOotvEscapeHatchBasenamesCount stays 0 so the
// placeholder is never iterated over or dereferenced.
const char* const kOotvEscapeHatchBasenames[] = { nullptr };
const size_t kOotvEscapeHatchBasenamesCount = 0;

bool IsOotvEscapeHatchBasename(const std::string& basename) {
    for (size_t i = 0; i < kOotvEscapeHatchBasenamesCount; i++) {
        if (basename == kOotvEscapeHatchBasenames[i]) {
            return true;
        }
    }
    return false;
}

namespace {

std::string DirnameOf(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

std::string BasenameOf(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

// Top-level ZAPD/.o2r export folders that hold inert binary assets (models, images, audio,
// animation/skeleton data, etc.) as opposed to gameplay-affecting text/scene/code-adjacent
// resources. Per the user's rule: resource/non-code files always get merged in (ootr_),
// never silently dropped on collision.
//
// IMPORTANT: several MM subsystems enumerate an entire top-level folder via
// ResourceMgr_ListFiles()/LoadDirectory() and index the results by an ID embedded in the file
// content, not by filename (e.g. AudioLoad_Init's ResourceMgr_ListFiles("audio/sequences*") /
// ("audio/fonts*"), which writes into gSequenceMap[seqNumber] and the fixed-size
// seqCachePolicyMap[MAX_AUTHENTIC_SEQID] using a seqNumber/fntIndex read straight out of the
// resource file). MM and OOT independently reuse the same small ID ranges for their own audio
// tables, so leaving an OOT file inside the same top-level folder (even under a renamed
// basename) lets it silently collide with and overwrite a real MM slot at load time -- this is
// exactly what caused the AudioPlayback_NoteInitForLayer crash on every scene transition.
// Fix: OOT resource-noncode content is placed under an "ootr_"-prefixed TOP FOLDER (see
// WithPrefixedTopFolder), not just a prefixed basename, so it never lands inside a folder MM's
// existing glob/index-by-embedded-ID loaders scan. This also keeps the door open for future
// two-way world-crossover work (a world-aware resolver can explicitly scan "ootr_audio/*" etc.
// and, symmetrically, a future oot.o2r merge could isolate MM content under "mmr_<folder>/")
// without any rework of this isolation.
const char* const kResourceNonCodeFolders[] = { "objects", "textures", "audio", "background",
                                                 "backgrounds", "skeletons", "animations",
                                                 "materials", "matrices" };

bool HasPathComponent(const std::string& path, const std::string& folder) {
    // Match "<folder>/" as a path component at any depth, e.g. "objects/foo/bar.bin" or
    // "some/nested/textures/x.bin".
    return path.rfind(folder + "/", 0) == 0 || path.find("/" + folder + "/") != std::string::npos;
}

bool IsResourceNonCode(const std::string& path) {
    for (const char* folder : kResourceNonCodeFolders) {
        if (HasPathComponent(path, folder)) {
            return true;
        }
    }
    return false;
}

std::string WithPrefixedBasename(const std::string& path, const std::string& prefix) {
    std::string dir = DirnameOf(path);
    std::string base = BasenameOf(path);
    if (dir.empty()) {
        return prefix + base;
    }
    return dir + "/" + prefix + base;
}

// Prefixes the FIRST path component (the top-level folder), not the basename, e.g.
// "audio/sequences/Foo" -> "ootr_audio/sequences/Foo". Used for resource-noncode content so it
// never lands inside a top-level folder MM's own code enumerates via a directory glob (see the
// kResourceNonCodeFolders comment for why that matters).
std::string WithPrefixedTopFolder(const std::string& path, const std::string& prefix) {
    auto pos = path.find('/');
    if (pos == std::string::npos) {
        return prefix + path;
    }
    return prefix + path.substr(0, pos) + path.substr(pos);
}

std::string MakeTempSiblingPath(const std::string& finalPath) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char* hex = "0123456789abcdef";
    std::string suffix;
    for (int i = 0; i < 8; i++) {
        suffix += hex[dist(rng)];
    }
    return finalPath + ".merge-" + suffix + ".tmp";
}

bool SetError(std::string* outError, const std::string& msg) {
    if (outError != nullptr) {
        *outError = msg;
    }
    return false;
}

// Reads every entry name present in an already-open zip archive into a set, for O(1)
// collision lookups against mm.o2r while iterating oot.o2r's entries.
std::unordered_set<std::string> ListEntryNames(zip_t* archive) {
    std::unordered_set<std::string> names;
    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    names.reserve(static_cast<size_t>(std::max<zip_int64_t>(numEntries, 0)));
    for (zip_int64_t i = 0; i < numEntries; i++) {
        const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
        if (name != nullptr) {
            names.insert(name);
        }
    }
    return names;
}

// Copies a single entry (by index) from `src` into `dst` under `destName`. Reads the whole
// entry into memory; .o2r entries are individual game assets (small/medium files), not
// multi-gigabyte blobs, so this is fine and keeps the logic simple/auditable.
bool CopyEntry(zip_t* src, zip_uint64_t srcIndex, zip_t* dst, const std::string& destName,
               std::string* outError) {
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(src, srcIndex, 0, &stat) != 0) {
        return SetError(outError, "zip_stat_index failed for source entry index " + std::to_string(srcIndex));
    }
    if ((stat.valid & ZIP_STAT_SIZE) == 0) {
        return SetError(outError, "source entry missing size info: " + destName);
    }

    zip_file_t* zf = zip_fopen_index(src, srcIndex, 0);
    if (zf == nullptr) {
        return SetError(outError, "zip_fopen_index failed for: " + destName);
    }

    std::vector<char> buffer(static_cast<size_t>(stat.size));
    if (stat.size > 0) {
        zip_int64_t read = zip_fread(zf, buffer.data(), stat.size);
        if (read < 0 || static_cast<zip_uint64_t>(read) != stat.size) {
            zip_fclose(zf);
            return SetError(outError, "short/failed read for: " + destName);
        }
    }
    zip_fclose(zf);

    // zip_source_buffer(archive, data, len, freep): with freep=1, libzip takes ownership of
    // `data` and will free() it itself, so the buffer must be a plain malloc'd allocation
    // that outlives this function (buffer.data() from std::vector does not qualify).
    void* owned = malloc(buffer.size());
    if (buffer.size() > 0) {
        if (owned == nullptr) {
            return SetError(outError, "out of memory copying entry: " + destName);
        }
        memcpy(owned, buffer.data(), buffer.size());
    }
    zip_source_t* source = zip_source_buffer(dst, owned, buffer.size(), 1 /* freep: dst takes ownership */);
    if (source == nullptr) {
        free(owned);
        return SetError(outError, "zip_source_buffer failed for: " + destName);
    }

    zip_int64_t destIndex = zip_file_add(dst, destName.c_str(), source, ZIP_FL_OVERWRITE);
    if (destIndex < 0) {
        zip_source_free(source);
        return SetError(outError, "zip_file_add failed for: " + destName + " (" + zip_strerror(dst) + ")");
    }
    return true;
}

} // namespace

bool MergeOotIntoMm(const std::string& mmPath, const std::string& ootPath, const std::string& mergedPath,
                    std::string* outError) {
    if (!std::filesystem::exists(mmPath)) {
        return SetError(outError, "mmPath does not exist: " + mmPath);
    }
    if (!std::filesystem::exists(ootPath)) {
        return SetError(outError, "ootPath does not exist: " + ootPath);
    }

    std::string tempPath = MakeTempSiblingPath(mergedPath);
    // Start the merged archive as a byte-for-byte copy of mm.o2r (base = mm.o2r unchanged),
    // then only add to it. Copying the file first lets us reuse zip_open in "add/modify" mode
    // without needing to re-write every MM entry through libzip ourselves.
    std::error_code copyEc;
    std::filesystem::copy_file(mmPath, tempPath, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        return SetError(outError, "failed to stage merged archive copy: " + copyEc.message());
    }

    int mmErr = 0;
    zip_t* mmZip = zip_open(mmPath.c_str(), ZIP_RDONLY, &mmErr);
    if (mmZip == nullptr) {
        std::filesystem::remove(tempPath);
        return SetError(outError, "failed to open mmPath for reading: " + mmPath);
    }
    std::unordered_set<std::string> mmEntryNames = ListEntryNames(mmZip);
    zip_close(mmZip);

    int ootErr = 0;
    zip_t* ootZip = zip_open(ootPath.c_str(), ZIP_RDONLY, &ootErr);
    if (ootZip == nullptr) {
        std::filesystem::remove(tempPath);
        return SetError(outError, "failed to open ootPath for reading: " + ootPath);
    }

    int mergedErr = 0;
    zip_t* mergedZip = zip_open(tempPath.c_str(), 0, &mergedErr);
    if (mergedZip == nullptr) {
        zip_close(ootZip);
        std::filesystem::remove(tempPath);
        return SetError(outError, "failed to open staged merged archive for writing: " + tempPath);
    }

    bool ok = true;
    std::string err;
    zip_int64_t numEntries = zip_get_num_entries(ootZip, 0);
    for (zip_int64_t i = 0; ok && i < numEntries; i++) {
        const char* rawName = zip_get_name(ootZip, static_cast<zip_uint64_t>(i), 0);
        if (rawName == nullptr) {
            continue;
        }
        std::string name(rawName);

        // Skip directory entries; libzip re-creates parent directories implicitly when a
        // file path containing '/' is added.
        if (!name.empty() && name.back() == '/') {
            continue;
        }

        // Special cases (root-level files only).
        if (name == "portVersion") {
            continue; // never preserved
        }
        if (name == "version") {
            ok = CopyEntry(ootZip, static_cast<zip_uint64_t>(i), mergedZip, "oot_version", &err);
            continue;
        }

        bool collides = mmEntryNames.find(name) != mmEntryNames.end();

        if (IsResourceNonCode(name)) {
            // Resource/non-code asset (models, textures, audio, animation data, ...): always
            // merged in. MM's own copy is never touched/replaced (it's untouched in the
            // base-copied archive). The OOT copy is added under an "ootr_"-prefixed TOP FOLDER
            // (not just a prefixed basename) so it lands outside any top-level folder MM's own
            // code enumerates via a directory glob and indexes by an embedded file ID -- see
            // the kResourceNonCodeFolders comment above for why a basename-only prefix caused
            // real MM audio slots to get silently overwritten.
            std::string destName = WithPrefixedTopFolder(name, "ootr_");
            ok = CopyEntry(ootZip, static_cast<zip_uint64_t>(i), mergedZip, destName, &err);
            continue;
        }

        if (!collides) {
            // No collision on a code/text-adjacent path: include as-is.
            ok = CopyEntry(ootZip, static_cast<zip_uint64_t>(i), mergedZip, name, &err);
            continue;
        }

        // Collision on a code/text-adjacent path (e.g. "text/", scene/actor data): the MM
        // file is never replaced. The OOT variant is only additionally included, under
        // "ootv_", when it's on the explicit "important for the current project" escape-hatch
        // list. Otherwise it's skipped silently and MM's copy (already present via the
        // base-copy) wins.
        std::string base = BasenameOf(name);
        if (IsOotvEscapeHatchBasename(base)) {
            std::string destName = WithPrefixedBasename(name, "ootv_");
            ok = CopyEntry(ootZip, static_cast<zip_uint64_t>(i), mergedZip, destName, &err);
        }
    }

    if (ok) {
        if (zip_close(mergedZip) != 0) {
            ok = false;
            err = "zip_close failed for merged archive";
        }
    } else {
        zip_discard(mergedZip);
    }
    zip_close(ootZip);

    if (!ok) {
        std::filesystem::remove(tempPath);
        return SetError(outError, err);
    }

    std::error_code renameEc;
    std::filesystem::rename(tempPath, mergedPath, renameEc);
    if (renameEc) {
        // Cross-filesystem fallback.
        std::filesystem::copy_file(tempPath, mergedPath, std::filesystem::copy_options::overwrite_existing, renameEc);
        std::filesystem::remove(tempPath);
        if (renameEc) {
            return SetError(outError, "failed to finalize merged archive at: " + mergedPath);
        }
    }

    return true;
}

} // namespace O2RMerger
