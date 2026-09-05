#pragma once

#include "pch.h"

#include "../SmokeManager.h"

namespace Hooks::Load3D
{
    template <class T>
    struct Load3D
    {
        static RE::NiAVObject* thunk(T* a_this, bool a_backgroundLoading)
        {
            auto* node = func(a_this, a_backgroundLoading);
            if (node) {
                if (auto* baseObject = a_this->GetObjectReference()) {
                    try {
                        SmokeManager::GetSingleton()->OnRefLoad3D(a_this, baseObject, node);
                    } catch (...) {
                        logger::error("OnRefLoad3D threw, attach skipped for this ref");
                    }
                }
            }
            return node;
        }

        static inline REL::Relocation<decltype(thunk)> func;
        static constexpr std::size_t                   idx{ 0x6A };
    };

    void Install();
}
