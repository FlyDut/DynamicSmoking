#pragma once

#include "pch.h"

#include "ConfigData.h"

#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

class SmokeManager
{
public:
    static SmokeManager* GetSingleton();

    bool ReadConfigs();

    void InitFromData();

    void OnRefLoad3D(RE::TESObjectREFR* a_refr, RE::TESBoundObject* a_base, RE::NiAVObject* a_root);

    void UpdateVisibility();

private:
    SmokeManager() = default;

    struct IndexEntry
    {
        std::size_t      fileIdx = 0;
        std::size_t      nodeIdx = 0;
        std::uint32_t    value = 0;
        Config::NodeRule rule;
    };

    struct TrackedSmoke
    {
        RE::ObjectRefHandle                refHandle;
        RE::NiPointer<RE::BSValueNode>     anchor;
        std::string                        anchorName;
        bool                               visible = true;
        bool                               hiding = false;
        std::uint32_t                      hiddenSinceMS = 0;
    };

    void BuildIndex();

    std::optional<std::uint32_t> ResolveFormId(const std::string& a_formId);

    void AttachRules(RE::TESObjectREFR* a_refr, RE::NiAVObject* a_root, const std::vector<IndexEntry>& a_entries);

    void RegisterTracked(RE::TESObjectREFR* a_refr, const std::string& a_anchorName, RE::BSValueNode* a_anchor);

    static bool ToggleEmitter(RE::NiAVObject* a_node, bool a_enable);

    static bool IsAnchorUnderRoot(RE::NiAVObject* a_node, RE::NiNode* a_root);

    void ReregisterEmitter(RE::NiAVObject* a_node);

    static void DetachAnchor(RE::BSValueNode* a_node, RE::NiNode* a_root);

    void DetachTrackedAnchor(TrackedSmoke& a_entry, RE::NiNode* a_root = nullptr);

    std::map<std::string, Config::TargetSet> rawConfigs;

    using RuleIndex =
        std::unordered_map<std::string, std::vector<IndexEntry>, Config::StringHash, Config::StringIequal>;

    RuleIndex runtimeIndex;

    RuleIndex runtimeIndexByName;

    std::unordered_map<std::string, std::optional<std::uint32_t>> formIdCache;

    std::vector<TrackedSmoke> tracked;

    std::uint32_t lastUpdateMS{ 0 };

    RE::FormID lastPlayerCellID{ 0 };

    bool indexReady{ false };

    std::mutex lock;
};
