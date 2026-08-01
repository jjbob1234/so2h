#include "OotAssets.h"

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>

namespace OotAssets {

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

std::string ResolveOotPath(const std::string& ootRelativePath) {
    // Mirrors O2RMerger::WithPrefixedTopFolder exactly: prefix the FIRST path component (the
    // top-level folder), not the basename, then add the "__OTR__" scheme prefix MM's own
    // texture path strings embed (see e.g. mm/assets/*/icon_item_static/*.h's
    // `"__OTR__icon_item_static_yar/..."` pattern).
    auto pos = ootRelativePath.find('/');
    std::string withTopFolderPrefixed;
    if (pos == std::string::npos) {
        withTopFolderPrefixed = "ootr_" + ootRelativePath;
    } else {
        withTopFolderPrefixed = "ootr_" + ootRelativePath.substr(0, pos) + ootRelativePath.substr(pos);
    }
    return "__OTR__" + withTopFolderPrefixed;
}

bool OotFileExists(const std::string& resolvedOrEntryPath) {
    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    if (resourceManager == nullptr) {
        return false;
    }
    auto archiveManager = resourceManager->GetArchiveManager();
    if (archiveManager == nullptr) {
        return false;
    }

    // ArchiveManager::HasFile wants the raw archive entry name, so strip the "__OTR__" scheme
    // prefix if the caller passed a ResolveOotPath() result (same pattern as
    // DeveloperTools::ValidateOotIconAssetsFor100PercentSave).
    static const std::string kScheme = "__OTR__";
    if (resolvedOrEntryPath.compare(0, kScheme.size(), kScheme) == 0) {
        return archiveManager->HasFile(resolvedOrEntryPath.substr(kScheme.size()));
    }
    return archiveManager->HasFile(resolvedOrEntryPath);
}

} // namespace OotAssets
