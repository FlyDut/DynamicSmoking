#include "SmokeManager.h"

#include "EngineAPI.h"
#include "Settings.h"
#include "Util.h"
#include "ValueNodeFactory.h"

#include <cctype>
#include <charconv>
#include <fstream>

namespace
{
    constexpr std::uint32_t kMaxDumpDepth = 12;

    constexpr std::uint32_t kVisibilityUpdateIntervalMS = 1000u;

    constexpr std::uint32_t kVisibilityHideDelayMS = 2000u;

    const char* GetBaseModelPath(RE::TESBoundObject* a_base)
    {
        RE::TESModel* model = nullptr;
        if (auto* stat = a_base->As<RE::TESObjectSTAT>()) {
            model = stat;
        }
        // 保留以下注释，理论上来说蜡烛应该只是静态物体
        // else if (auto* acti = a_base->As<RE::TESObjectACTI>()) {
        //     model = acti;
        // } else if (auto* furn = a_base->As<RE::TESFurniture>()) {
        //     model = furn;
        // } else if (auto* cont = a_base->As<RE::TESObjectCONT>()) {
        //     model = cont;
        // } else if (auto* misc = a_base->As<RE::TESObjectMISC>()) {
        //     model = misc;
        // }
        return model ? model->GetModel() : nullptr;
    }

    float GameRuntimeSeconds()
    {
        return static_cast<float>(RE::BSTimer::GetSingleton()->runTimeMS) * 0.001f;
    }
}  // namespace

SmokeManager* SmokeManager::GetSingleton()
{
    static SmokeManager singleton;
    return &singleton;
}

void SmokeManager::InitFromData()
{
    // 这里存在竞态，为了保证 ReadConfigs -> InitFromData -> OnRefLoad3D 按预期执行
    std::lock_guard<std::mutex> guard(lock);

    BuildIndex();
    // 确保 OnRefLoad3D 执行时 InitFromData 已经把表单数据准备好
    indexReady = true;
}

bool SmokeManager::ReadConfigs()
{
    // 这里存在竞态，为了保证 ReadConfigs -> InitFromData -> OnRefLoad3D 按预期执行
    std::lock_guard<std::mutex> guard(lock);

    const std::filesystem::path dir{ R"(Data\DynamicSmoking)" };
    if (std::error_code ec; !std::filesystem::exists(dir, ec)) {
        logger::info("Data\\DynamicSmoking folder not found ({})", ec.message());
        return false;
    }

    std::error_code iterEc;
    auto            iter = std::filesystem::recursive_directory_iterator(
        dir, std::filesystem::directory_options::skip_permission_denied, iterEc);
    if (iterEc) {
        logger::error("directory iteration failed: {}", iterEc.message());
        return false;
    }

    try {
        for (const auto& dirEntry : iter) {
            if (dirEntry.is_directory()) {
                continue;
            }

            auto ext = dirEntry.path().extension().string();
            for (auto& c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext != ".json"sv) {
                continue;
            }

            const std::string path = dirEntry.path().string();

            auto rel = dirEntry.path().lexically_relative(dir);
            rel.replace_extension();
            const std::string id = rel.string();

            logger::info("Reading {}...", path);
            std::string   buffer;
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                logger::error("\t{} cannot open file", id);
                continue;
            }
            buffer.assign(std::istreambuf_iterator<char>(file), {});

            const auto firstNonWs = buffer.find_first_not_of(" \t\r\n");
            if (firstNonWs != std::string::npos && buffer[firstNonWs] == '[') {
                std::vector<Config::TargetSet> sets;
                if (auto err = glz::read_json(sets, buffer)) {
                    logger::error("\t{} parse error:{}", id, glz::format_error(err, buffer));
                    continue;
                }
                for (std::size_t i = 0; i < sets.size(); ++i) {
                    rawConfigs[fmt::format("{}[{}]", id, i)] = std::move(sets[i]);
                }
                if (sets.empty()) {
                    logger::warn("\t{} is an empty array, no configs loaded", id);
                }
            } else {
                continue;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        logger::error("directory iteration aborted: {}", e.what());
    } catch (...) {
        logger::error("directory iteration aborted: unknown exception");
    }

    return !rawConfigs.empty();
}

std::optional<std::uint32_t> SmokeManager::ResolveFormId(const std::string& a_formId)
{
    if (const auto it = formIdCache.find(a_formId); it != formIdCache.end()) {
        return it->second;
    }

    auto fail = [&](std::string_view a_reason) {
        logger::error("[DynamicSmoke] formId '{}' resolve failed: {}", a_formId, a_reason);
        // 将不存在的formId缓存为空值，防止错误一直打印到日志中
        formIdCache.emplace(a_formId, std::nullopt);
        return std::nullopt;
    };

    if (a_formId.empty()) {
        return fail("formId is required (expected 'Plugin.esp|0x800')");
    }
    const auto bar = a_formId.find('|');
    if (bar == std::string::npos) {
        return fail("missing plugin part (expected 'Plugin.esp|0x800')");
    }

    const auto plugin = std::string_view{ a_formId }.substr(0, bar);
    const auto pFirst = plugin.find_first_not_of(" \t");
    const auto pLast = plugin.find_last_not_of(" \t");
    if (pFirst == std::string_view::npos) {
        return fail("empty plugin name");
    }
    const std::string pluginName{ plugin.substr(pFirst, pLast - pFirst + 1) };

    auto       idStr = std::string_view{ a_formId }.substr(bar + 1);
    const auto idFirst = idStr.find_first_not_of(" \t");
    const auto idLast = idStr.find_last_not_of(" \t");
    if (idFirst == std::string_view::npos) {
        return fail("empty formID part");
    }
    idStr = idStr.substr(idFirst, idLast - idFirst + 1);
    if (idStr.starts_with("0x"sv) || idStr.starts_with("0X"sv)) {
        idStr = idStr.substr(2);
    }

    RE::FormID formID{};
    const auto [ptr, ec] = std::from_chars(idStr.data(), idStr.data() + idStr.size(), formID, 16);
    if (ec != std::errc{} || ptr != idStr.data() + idStr.size()) {
        return fail("formID is not valid hex");
    }

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* form = dh ? dh->LookupForm<RE::BGSAddonNode>(formID, pluginName) : nullptr;
    if (!form) {
        return fail(fmt::format("ADDN form {} not found in '{}'", formID, pluginName));
    }

    formIdCache.emplace(a_formId, form->index);
    logger::info("[DynamicSmoke] formId '{}' -> ADDN formID={} index={}",
        a_formId,
        form->GetFormID(),
        form->index);
    return form->index;
}

void SmokeManager::BuildIndex()
{
    runtimeIndex.clear();
    runtimeIndexByName.clear();
    formIdCache.clear();

    std::size_t fileIdx = 0;
    std::size_t droppedRules = 0;

    const auto indexSet = [&](RuleIndex& a_index, const std::string& a_model, const Config::TargetSet& a_set) {
        auto& entries = a_index[a_model];
        if (!entries.empty()) {
            logger::info("model '{}' appears in multiple configs, rules merged ({} + {})",
                a_model,
                entries.size(),
                a_set.nodes.size());
        }
        for (std::size_t nodeIdx = 0; nodeIdx < a_set.nodes.size(); ++nodeIdx) {
            const auto& rule = a_set.nodes[nodeIdx];
            const auto  value = ResolveFormId(rule.formId);
            if (!value) {
                ++droppedRules;
                continue;
            }
            entries.push_back(IndexEntry{ .fileIdx = fileIdx, .nodeIdx = nodeIdx, .value = *value, .rule = rule });
        }
    };

    for (auto& [id, set] : rawConfigs) {
        set.NormalizePath();

        for (const auto& model : set.models) {
            // 规范化后含 '\' 视为相对 meshes 的全路径，否则按文件名匹配
            if (model.contains('\\')) {
                indexSet(runtimeIndex, model, set);
            } else {
                indexSet(runtimeIndexByName, model, set);
            }
        }
        ++fileIdx;
    }

    std::size_t totalRules = 0;
    for (const auto& [model, entries] : runtimeIndex) {
        totalRules += entries.size();
    }
    for (const auto& [model, entries] : runtimeIndexByName) {
        totalRules += entries.size();
    }
    logger::info(
        "index built: {} configs, {} path models, {} filename models, {} node rules, {} dropped "
        "(unresolvable formId)",
        rawConfigs.size(),
        runtimeIndex.size(),
        runtimeIndexByName.size(),
        totalRules,
        droppedRules);

    for (const auto& [model, entries] : runtimeIndex) {
        logger::debug("  index entry: model='{}' rules={}", model, entries.size());
    }
    for (const auto& [model, entries] : runtimeIndexByName) {
        logger::debug("  index entry (by filename): model='{}' rules={}", model, entries.size());
    }

    rawConfigs.clear();
    formIdCache.clear();
}

void SmokeManager::OnRefLoad3D(RE::TESObjectREFR* a_refr, RE::TESBoundObject* a_base, RE::NiAVObject* a_root)
{
    if (!a_refr || !a_base || !a_root) {
        return;
    }
    const char* modelPath = GetBaseModelPath(a_base);
    if (!modelPath) {
        return;
    }
    const auto normalized = DSC::NormalizePath(modelPath);

    // 这里存在竞态，为了保证 ReadConfigs -> InitFromData -> OnRefLoad3D 按预期执行
    std::lock_guard<std::mutex> guard(lock);

    // 确保 InitFromData 执行完毕，AddNode 数据已被加载
    if (!indexReady) {
        return;
    }

    const auto it = runtimeIndex.find(normalized);
    if (it != runtimeIndex.end()) {
        // 全路径命中即作为特例生效，不再叠加文件名规则
        AttachRules(a_refr, a_root, it->second);
        return;
    }

    const auto fileName = DSC::GetFileName(normalized);
    if (fileName.empty()) {
        return;
    }
    const auto itByName = runtimeIndexByName.find(fileName);
    if (itByName == runtimeIndexByName.end()) {
        return;
    }

    AttachRules(a_refr, a_root, itByName->second);
}

void SmokeManager::AttachRules(RE::TESObjectREFR* a_refr,
    RE::NiAVObject*                               a_root,
    const std::vector<IndexEntry>&                a_entries)
{
    const auto pos = a_refr->GetPosition();
    const auto tag = fmt::format("refr={} pos=({}, {}, {})", a_refr->GetFormID(), pos.x, pos.y, pos.z);

    auto* rootNode = a_root->AsNode();
    if (!rootNode) {
        logger::error("[DynamicSmoking] root is not a NiNode: {}", tag);
        return;
    }

    if (spdlog::should_log(spdlog::level::debug)) {
        logger::debug("[DynamicSmoking] attach {} rules, tree BEFORE: {}", a_entries.size(), tag);
        DumpTree(rootNode, 0);
    }

    std::size_t attachedCount = 0;
    for (const auto& entry : a_entries) {
        const auto anchorName = fmt::format("DSC_Anchor_{}_{}", entry.fileIdx, entry.nodeIdx);
        if (rootNode->GetObjectByName(RE::BSFixedString(anchorName))) {
            logger::info("[DynamicSmoking] anchor '{}' already attached, skip: {}", anchorName, tag);
            continue;
        }

        // 构造 BSValueNode。先创建NiNode，挂在场景图的根上，再把AddNode挂载到NiNode上
        RE::NiPointer<RE::BSValueNode> valueNode(DSC::CreateValueNode());
        if (!valueNode) {
            logger::error("[DynamicSmoking] anchor '{}': CreateValueNode failed: {}", anchorName, tag);
            continue;
        }

        valueNode->name = anchorName;
        valueNode->value = entry.value;

        valueNode->local = RE::NiTransform{};
        valueNode->local.translate = RE::NiPoint3{ entry.rule.offset[0],
            entry.rule.offset[1],
            entry.rule.offset[2] };

        rootNode->AttachChild(valueNode.get());
        RegisterTracked(a_refr, anchorName);
        ++attachedCount;
        logger::info("[DynamicSmoke] anchor '{}' formId='{}' value={} attached: {}",
            anchorName,
            entry.rule.formId,
            entry.value,
            tag);
    }

    // 由引擎更新场景图
    if (attachedCount > 0) {
        RE::NiUpdateData updateData{};
        updateData.time = GameRuntimeSeconds();
        rootNode->UpdateDownwardPass(updateData, 0);

        const auto attachAddonNodes = EngineAPI::GetAttachAddonNodes();
        if (!attachAddonNodes) {
            logger::error("[DynamicSmoke] EngineAPI::AttachAddonNodes unavailable: {}", tag);
        } else {
            const bool engineResult = attachAddonNodes(rootNode);
            logger::info("[DynamicSmoke] AttachAddonNodes({} new anchors) -> {} ({})",
                attachedCount,
                engineResult,
                tag);
        }
    }

    if (spdlog::should_log(spdlog::level::debug)) {
        logger::debug("[DynamicSmoke] tree AFTER: {}", tag);
        DumpTree(rootNode, 0);
    }
}

void SmokeManager::RegisterTracked(RE::TESObjectREFR* a_refr, const std::string& a_anchorName)
{
    const RE::ObjectRefHandle handle{ a_refr };
    for (auto& entry : tracked) {
        if (entry.refHandle == handle && entry.anchorName == a_anchorName) {
            entry.visible = true;
            entry.hiding = false;
            entry.hiddenSinceMS = 0;
            return;
        }
    }
    tracked.push_back(TrackedSmoke{ .refHandle = handle, .anchorName = a_anchorName, .visible = true });
}

void SmokeManager::UpdateVisibility()
{
    const std::uint32_t nowMS = RE::BSTimer::GetSingleton()->runTimeMS;
    if (nowMS - lastUpdateMS < kVisibilityUpdateIntervalMS) {
        return;
    }
    lastUpdateMS = nowMS;

    std::lock_guard<std::mutex> guard(lock);

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }
    const RE::NiPoint3 playerPos = player->GetPosition();
    const float        maxDistSq = Settings::GetMaxDistance() * Settings::GetMaxDistance();
    const float        minDistSq = Settings::GetMinDistance() * Settings::GetMinDistance();

    for (auto it = tracked.begin(); it != tracked.end();) {
        auto                          refr = it->refHandle.get();
        RE::NiPointer<RE::NiAVObject> root{ refr ? refr->Get3D() : nullptr };
        if (!refr || !root) {
            it = tracked.erase(it);
            continue;
        }

        auto* node = root->GetObjectByName(RE::BSFixedString(it->anchorName.c_str()));
        if (!node) {
            it = tracked.erase(it);
            continue;
        }

        auto& entry = *it;

        bool shouldShow = true;
        if (refr.get() != player) {
            const float distSq = (refr->GetPosition() - playerPos).SqrLength();
            if (distSq > maxDistSq) {
                shouldShow = false;
            } else if (distSq > minDistSq && Settings::IsLineOfSightCullingEnabled()) {
                bool arg2 = false;
                if (!player->HasLineOfSight(refr.get(), arg2)) {
                    shouldShow = false;
                }
            }
        }

        if (shouldShow) {
            if (!entry.visible) {
                node->SetAppCulled(false);
                ReregisterEmitter(node);
                entry.visible = true;
            }
            entry.hiding = false;
        } else if (entry.visible) {
            if (entry.hiding) {
                // 第二次依然不可见，才会真正隐藏，防止马上隐藏造成的突兀感
                if (nowMS - entry.hiddenSinceMS >= kVisibilityHideDelayMS) {
                    node->SetAppCulled(true);
                    entry.visible = false;
                    entry.hiding = false;
                }
            } else {
                // 首次不可见：开始计时，暂不隐藏
                entry.hiding = true;
                entry.hiddenSinceMS = nowMS;
            }
        }
        ++it;
    }
}

void SmokeManager::DumpTree(RE::NiAVObject* a_node, std::uint32_t a_depth)
{
    if (!a_node) {
        return;
    }

    const std::string pad(a_depth * 2, ' ');
    const auto*       rtti = a_node->GetRTTI();
    const auto&       name = a_node->name;
    const auto&       t = a_node->local.translate;
    logger::debug("  {}[d{}] name='{}' rtti={} t=({:.2f}, {:.2f}, {:.2f})",
        pad,
        a_depth,
        std::string_view(name.c_str() ? name.c_str() : ""),
        std::string_view(rtti && rtti->GetName() ? rtti->GetName() : "?"),
        t.x,
        t.y,
        t.z);

    if (a_depth >= kMaxDumpDepth) {
        return;
    }
    if (auto* asNode = a_node->AsNode()) {
        for (const auto& child : asNode->GetChildren()) {
            if (child) {
                DumpTree(child.get(), a_depth + 1);
            }
        }
    }
}

void SmokeManager::ReregisterEmitter(RE::NiAVObject* a_node)
{
    if (!a_node) {
        return;
    }

    const auto* rtti = a_node->GetRTTI();
    if (!rtti || !rtti->GetName() || std::strcmp(rtti->GetName(), "BSValueNode") != 0) {
        return;
    }

    auto* valueNode = static_cast<RE::BSValueNode*>(a_node);
    auto* ps = valueNode->associatedObject.get();
    if (!ps) {
        return;
    }

    auto&      emitters = ps->emitterObjs;
    const bool exists = std::any_of(emitters.begin(), emitters.end(),
        [a_node](const RE::NiPointer<RE::NiAVObject>& e) { return e.get() == a_node; });
    if (exists) {
        return;
    }

    if (emitters.size() >= ps->maxEmitterObj) {
        logger::warn("[DynamicSmoking] emitter capacity full: anchor='{}' ps={:#x} max={}",
            std::string_view(a_node->name.c_str() ? a_node->name.c_str() : ""),
            reinterpret_cast<std::uintptr_t>(ps),
            ps->maxEmitterObj);
        return;
    }

    emitters.push_back(RE::NiPointer<RE::NiAVObject>(a_node));
    logger::info("[DynamicSmoking] re-registered emitter: anchor='{}' ps={:#x} emitters={}",
        std::string_view(a_node->name.c_str() ? a_node->name.c_str() : ""),
        reinterpret_cast<std::uintptr_t>(ps),
        emitters.size());
}
