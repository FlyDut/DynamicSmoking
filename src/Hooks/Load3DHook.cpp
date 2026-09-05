#include "Load3DHook.h"

namespace Hooks::Load3D
{
    void Install()
    {
        stl::write_vfunc<RE::TESObjectREFR, Load3D<RE::TESObjectREFR>>();
        logger::info("Hooked RE::TESObjectREFR::Load3D (vfunc {:#x})"sv, Load3D<RE::TESObjectREFR>::idx);
    }
}
