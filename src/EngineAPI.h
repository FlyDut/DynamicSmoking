#pragma once

#include "pch.h"

namespace EngineAPI
{
    using AttachAddonNodes_t = bool (*)(RE::NiNode*);

    using ToggleMasterParticleAddonNodes_t = bool (*)(const RE::NiNode*, bool);

    [[nodiscard]] inline AttachAddonNodes_t GetAttachAddonNodes()
    {
        static REL::Relocation<AttachAddonNodes_t> func{ RELOCATION_ID(19206, 19632) };
        return func.get();
    }

    [[nodiscard]] inline ToggleMasterParticleAddonNodes_t GetToggleMasterParticleAddonNodes()
    {
        static REL::Relocation<ToggleMasterParticleAddonNodes_t> func{ RELOCATION_ID(13260, 13411) };
        return func.get();
    }
}
