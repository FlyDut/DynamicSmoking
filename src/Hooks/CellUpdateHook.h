#pragma once

#include "pch.h"

#include "../SmokeManager.h"

namespace Hooks::CellUpdate
{
    struct Update
    {
        static void thunk(RE::TESObjectCELL* a_cell)
        {
            func(a_cell);

            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* playerCell = player ? player->GetParentCell() : nullptr;
            if (a_cell && a_cell == playerCell) {
                SmokeManager::GetSingleton()->UpdateVisibility();
            }
        }
        static inline REL::Relocation<decltype(thunk)> func;

        static void Install()
        {
            REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(18458, 18889), 0x52 };  // TESObjectCELL::RunAnimations
            func = target.write_call<5>(thunk);
        }
    };

    void Install();
}
