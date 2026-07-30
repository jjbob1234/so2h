#pragma once

#include <stdint.h>
#include <vector>
#include <ship/resource/Resource.h>
#include <libultraship/libultra/types.h>

namespace SOH {
// Generic "Text" resource type (OTXT / SOH_Text). Ported from reference/soh so BenPort.cpp can
// register a factory for it. MM's own message tables use the distinct TextMM (OTXM) type; this
// type exists as defensive hardening in case a future OOT-sourced Text resource is intentionally
// consumed (e.g. via an ootv_-prefixed path) rather than falling back to an unregistered-factory
// failure.
class MessageEntry {
  public:
    uint16_t id;
    uint8_t textboxType;
    uint8_t textboxYPos;
    std::string msg;
};

class Text : public Ship::Resource<MessageEntry> {
  public:
    using Resource::Resource;

    Text() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
    }

    MessageEntry* GetPointer();
    size_t GetPointerSize();

    std::vector<MessageEntry> messages;
};
}; // namespace SOH
