#pragma once

#include "pch.h"

namespace EngineAPI
{
    using AttachAddonNodes_t = bool (*)(RE::NiNode*);

    [[nodiscard]] inline AttachAddonNodes_t GetAttachAddonNodes()
    {
        static REL::Relocation<AttachAddonNodes_t> func{ RELOCATION_ID(19206, 19632) };
        return func.get();
    }
}
