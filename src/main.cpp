#include "pch.h"

#include "Hooks/CellUpdateHook.h"
#include "Hooks/Load3DHook.h"
#include "Settings.h"
#include "SmokeManager.h"

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            SmokeManager::GetSingleton()->InitFromData();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    SKSE::log::info("DynamicSmoking loaded");

    // 为CellUpdate hook提供一个64字节的跳板内存地址
    SKSE::AllocTrampoline(64);

    if (!SmokeManager::GetSingleton()->ReadConfigs()) {
        SKSE::log::warn("no configs loaded, plugin inert"sv);
    }

    Settings::Load();

    Hooks::Load3D::Install();
    Hooks::CellUpdate::Install();

    const auto* messaging = SKSE::GetMessagingInterface();
    if (messaging && messaging->RegisterListener(OnSKSEMessage)) {
        SKSE::log::info("Registered SKSE messaging listener (kDataLoaded)"sv);
    } else {
        SKSE::log::error("Failed to register SKSE messaging listener"sv);
    }

    return true;
}
