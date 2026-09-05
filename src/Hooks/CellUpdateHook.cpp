#include "CellUpdateHook.h"

namespace Hooks::CellUpdate
{
    void Install()
    {
        Update::Install();
        logger::info("Hooked call inside TESObjectCELL::RunAnimations (RELOCATION_ID 18458, 18889)"sv);
    }
}
