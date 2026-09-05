#pragma once

#include "pch.h"

namespace Settings
{
    void Load();

    [[nodiscard]] float GetMaxDistance();

    [[nodiscard]] float GetMinDistance();

    [[nodiscard]] bool IsLineOfSightCullingEnabled();
}
