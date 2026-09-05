#include "Settings.h"

#include "SimpleIni.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    constexpr std::string_view kIniPath = R"(Data\SKSE\Plugins\DynamicSmoking.ini)";
    constexpr const char*      kSection = "Settings";
    constexpr const char*      kKeyMaxDistance = "fMaxDistance";
    constexpr const char*      kKeyMinDistance = "fMinDistance";
    constexpr const char*      kKeyLineOfSight = "bEnableLineOfSightCulling";
    constexpr float            kDefaultMaxDistance = 900.0f;
    constexpr float            kDefaultMinDistance = 400.0f;
    constexpr float            kMaxDistanceLimit = 100000.0f;
}

namespace Settings
{
    namespace
    {
        float maxDistance = kDefaultMaxDistance;
        float minDistance = kDefaultMinDistance;
        bool  lineOfSightCulling = true;
    }

    void Load()
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.SetAllowKeyOnly();

        const std::filesystem::path path{ kIniPath };

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            if (ec) {
                logger::warn("[DynamicSmoking] exists check failed '{}': {}",
                    path.string(),
                    ec.message());
                return;
            }

            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                logger::error("[DynamicSmoking] failed to create directory '{}': {}",
                    path.parent_path().string(),
                    ec.message());
                return;
            }

            ini.SetValue(kSection, kKeyMaxDistance, fmt::format("{}", kDefaultMaxDistance).c_str());
            ini.SetValue(kSection, kKeyMinDistance, fmt::format("{}", kDefaultMinDistance).c_str());
            ini.SetValue(kSection, kKeyLineOfSight, "false");
            if (ini.SaveFile(path.string().c_str()) < 0) {
                logger::error("[DynamicSmoking] failed to write default config: {}", path.string());
            }
            return;
        }

        if (ini.LoadFile(path.string().c_str()) < 0) {
            logger::warn("[DynamicSmoking] failed to read config '{}', using default fMaxDistance={}",
                path.string(),
                kDefaultMaxDistance);
            return;
        }

        const double maxParsed = ini.GetDoubleValue(kSection, kKeyMaxDistance, kDefaultMaxDistance);
        maxDistance = std::isfinite(maxParsed) ? std::clamp(static_cast<float>(maxParsed), 0.0f, kMaxDistanceLimit) : kDefaultMaxDistance;
        const double minParsed = ini.GetDoubleValue(kSection, kKeyMinDistance, kDefaultMinDistance);
        minDistance = std::isfinite(minParsed) ? std::clamp(static_cast<float>(minParsed), 0.0f, kMaxDistanceLimit) : kDefaultMinDistance;
        lineOfSightCulling = ini.GetBoolValue(kSection, kKeyLineOfSight, false);

        logger::info("[DynamicSmoking] loaded fMaxDistance={} losCulling={} from {}", maxDistance, lineOfSightCulling, path.string());
    }

    float GetMaxDistance()
    {
        return maxDistance;
    }

    float GetMinDistance()
    {
        return minDistance;
    }

    bool IsLineOfSightCullingEnabled()
    {
        return lineOfSightCulling;
    }
}
