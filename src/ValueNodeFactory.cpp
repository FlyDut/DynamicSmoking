#include "ValueNodeFactory.h"

#include "RE/B/BSValueNode.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiNode.h"

namespace DSC
{
    RE::BSValueNode* CreateValueNode(std::uint16_t a_childrenCapacity)
    {
        auto* node = RE::malloc_runtime<RE::BSValueNode>(0x138, 0x160);
        if (!node) {
            return nullptr;
        }

        static REL::Relocation<RE::NiNode* (RE::NiNode::*)(std::uint16_t)> ctor{
            RELOCATION_ID(68936, 70287)
        };
        ctor(static_cast<RE::NiNode*>(node), a_childrenCapacity);

        static REL::Relocation<std::uintptr_t> vtbl{ RE::BSValueNode::VTABLE[0] };
        *reinterpret_cast<std::uintptr_t*>(node) = vtbl.address();

        static_assert(sizeof(RE::BSValueNode) == 0x138);
        std::memset(reinterpret_cast<std::uint8_t*>(node) + 0x128, 0, 0x10);

        return node;
    }
}
