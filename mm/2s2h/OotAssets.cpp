#include "OotAssets.h"

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>

namespace OotAssets {

namespace {

std::string BasenameOf(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string DirnameOf(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

} // namespace

bool IsOotContentAvailable() {
    // "oot_version" is the provenance marker O2RMerger always writes at the archive root when
    // a merge happened (see O2RMerger.h "Special cases"). Its presence is a reliable, cheap
    // signal that this mm.o2r was produced by/migrated through the merge, without needing to
    // probe for any specific ootr_/ootv_ asset.
    auto archiveManager = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    if (archiveManager == nullptr) {
        return false;
    }
    return archiveManager->HasFile("oot_version");
}

std::string GetOotIconVariant(const std::string& originalPath) {
    std::string dir = DirnameOf(originalPath);
    std::string base = BasenameOf(originalPath);
    if (dir.empty()) {
        return "ootr_" + base;
    }
    return dir + "/ootr_" + base;
}

} // namespace OotAssets
