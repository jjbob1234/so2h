#pragma once

#include <ship/resource/Resource.h>
#include <ship/resource/ResourceFactoryBinary.h>
#include <ship/resource/ResourceFactoryXML.h>

// Ported from reference/soh/soh/soh/resource/importer/TextFactory.h.
// Registers factories for the generic "Text" resource type (OTXT / SOH_Text), distinct from MM's
// own TextMM (OTXM). Nothing in env/2s2h currently produces or requires this type after the
// archive-merge fix (MM's own TextMM entries always win at colliding paths), but it closes the
// "no import factory for resource of type OTXT" failure mode that previously crashed
// z_message_OTR.cpp when oot.o2r's staff_message_data_static (typed OTXT by ZAPD) silently
// overwrote MM's TextMM entry at the identical path.
namespace SOH {
class ResourceFactoryBinaryTextV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

class ResourceFactoryXMLTextV0 final : public Ship::ResourceFactoryXML {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};
} // namespace SOH
