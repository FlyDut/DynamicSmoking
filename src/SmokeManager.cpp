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

    logger::debug("[DynamicSmoking] attach {} rules: {}", a_entries.size(), tag);

    std::size_t attachedCount = 0;
    std::vector<RE::NiPointer<RE::BSValueNode>> newAnchors;
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
        RegisterTracked(a_refr, anchorName, valueNode.get());
        newAnchors.push_back(valueNode);
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

        // AttachAddonNodes 只负责把 addon 挂到节点上，不保证节点已进入 MPS 的 emitter 列表。
        // 这里显式走一次 Toggle(true) 完成注册；失败时再用旧的手动注册兜底。
        for (auto& anchor : newAnchors) {
            if (!anchor) {
                continue;
            }
            if (!ToggleEmitter(anchor.get(), true)) {
                logger::warn("[DynamicSmoke] initial ToggleMasterParticleAddonNodes failed for '{}': {}",
                    std::string_view(anchor->name.c_str() ? anchor->name.c_str() : ""),
                    tag);
            }
            ReregisterEmitter(anchor.get());
        }
    }
}

void SmokeManager::RegisterTracked(RE::TESObjectREFR* a_refr, const std::string& a_anchorName, RE::BSValueNode* a_anchor)
{
    const RE::ObjectRefHandle handle{ a_refr };
    for (auto& entry : tracked) {
        if (entry.refHandle == handle && entry.anchorName == a_anchorName) {
            // 同一个 ref 重新加载 3D 时可能创建了新锚点；先把旧锚点从 MPS 摘掉，
            // 否则旧锚点会继续被全局 MPS 持有。
            if (entry.anchor && entry.anchor.get() != a_anchor) {
                DetachAnchor(entry.anchor.get(), nullptr);
            }
            entry.anchor.reset(a_anchor);
            entry.visible = true;
            entry.hiding = false;
            entry.hiddenSinceMS = 0;
            return;
        }
    }
    tracked.push_back(TrackedSmoke{ .refHandle = handle, .anchorName = a_anchorName, .visible = true });
    tracked.back().anchor.reset(a_anchor);
}

void SmokeManager::UpdateVisibility()
{
    const std::uint32_t nowMS = RE::BSTimer::GetSingleton()->runTimeMS;

    std::lock_guard<std::mutex> guard(lock);

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }
    auto* playerCell = player->GetParentCell();
    if (!playerCell) {
        return;
    }

    // 换 cell 时跳过一次间隔限制，立刻清理旧 cell 的锚点。
    const auto playerCellID = playerCell->GetFormID();
    if (playerCellID != lastPlayerCellID) {
        lastPlayerCellID = playerCellID;
        lastUpdateMS = nowMS - kVisibilityUpdateIntervalMS;
    }

    if (nowMS - lastUpdateMS < kVisibilityUpdateIntervalMS) {
        return;
    }
    lastUpdateMS = nowMS;

    const RE::NiPoint3 playerPos = player->GetPosition();
    const float        maxDistSq = Settings::GetMaxDistance() * Settings::GetMaxDistance();
    const float        minDistSq = Settings::GetMinDistance() * Settings::GetMinDistance();

    for (auto it = tracked.begin(); it != tracked.end();) {
        auto                          refr = it->refHandle.get();
        auto*                         refrCell = refr ? refr->GetParentCell() : nullptr;
        RE::NiPointer<RE::NiAVObject> root{ refr ? refr->Get3D() : nullptr };
        const bool otherCell = refrCell != playerCell;
        const bool shouldCleanCell = otherCell &&
            (!playerCell->IsExteriorCell() || (refrCell && !refrCell->IsExteriorCell()));
        if (refr && shouldCleanCell) {
            logger::debug("[DynamicSmoke] cell cleanup: anchor='{}' refr={} cell={:#x} playerCell={:#x}",
                it->anchorName,
                refr->GetFormID(),
                reinterpret_cast<std::uintptr_t>(refrCell),
                reinterpret_cast<std::uintptr_t>(playerCell));
        }
        if (!refr || !root || shouldCleanCell) {
            DetachTrackedAnchor(*it);
            it = tracked.erase(it);
            continue;
        }

        // 以 tracked 中保存的锚点指针为准，避免 3D 已被替换却误操作同名的新节点。
        auto* rootNode = root->AsNode();
        auto* node = it->anchor.get();
        if (!rootNode || !node || !IsAnchorUnderRoot(node, rootNode)) {
            DetachTrackedAnchor(*it);
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
                if (!ToggleEmitter(node, true)) {
                    node->SetAppCulled(false);
                }
                ReregisterEmitter(node);
                entry.visible = true;
            }
            entry.hiding = false;
        } else if (entry.visible) {
            if (entry.hiding) {
                // 第二次依然不可见，才会真正隐藏，防止马上隐藏造成的突兀感
                if (nowMS - entry.hiddenSinceMS >= kVisibilityHideDelayMS) {
                    if (!ToggleEmitter(node, false)) {
                        node->SetAppCulled(true);
                    }
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

void SmokeManager::DetachTrackedAnchor(TrackedSmoke& a_entry, RE::NiNode* a_root)
{
    if (!a_entry.anchor) {
        return;
    }

    DetachAnchor(a_entry.anchor.get(), a_root);
    a_entry.anchor.reset();
}

bool SmokeManager::IsAnchorUnderRoot(RE::NiAVObject* a_node, RE::NiNode* a_root)
{
    if (!a_node || !a_root) {
        return false;
    }

    for (auto* parent = a_node->parent; parent; parent = parent->parent) {
        if (parent == a_root) {
            return true;
        }
    }
    return false;
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

bool SmokeManager::ToggleEmitter(RE::NiAVObject* a_node, bool a_enable)
{
    if (!a_node) {
        return false;
    }

    auto* node = a_node->AsNode();
    if (!node) {
        return false;
    }

    const auto toggle = EngineAPI::GetToggleMasterParticleAddonNodes();
    if (!toggle) {
        logger::error("[DynamicSmoking] ToggleMasterParticleAddonNodes unavailable");
        return false;
    }

    const bool result = toggle(node, a_enable);
    logger::debug("[DynamicSmoking] ToggleMasterParticleAddonNodes(anchor='{}', {}) -> {}",
        std::string_view(a_node->name.c_str() ? a_node->name.c_str() : ""),
        a_enable,
        result);
    return result;
}

void SmokeManager::DetachAnchor(RE::BSValueNode* a_node, RE::NiNode* a_root)
{
    if (!a_node) {
        return;
    }

    // 保证 DetachAnchor 执行期间节点不会被析构；调用方通常已经通过 tracked 持有一份引用。
    RE::NiPointer<RE::BSValueNode> keepAlive(a_node);
    (void)keepAlive;

    const char* name = a_node->name.c_str();
    auto*       ps = a_node->associatedObject.get();
    if (ps) {
        auto& emitters = ps->emitterObjs;
        for (auto it = emitters.begin(); it != emitters.end(); ++it) {
            if (it->get() == a_node) {
                emitters.erase(it);
                logger::info("[DynamicSmoking] detached emitter: anchor='{}' ps={:#x} emitters={}",
                    name ? name : "",
                    reinterpret_cast<std::uintptr_t>(ps),
                    emitters.size());
                break;
            }
        }
    }

    a_node->SetAppCulled(true);
    a_node->associatedObject.reset();

    // 只在仍能确认父节点有效时从场景图摘除；root 已销毁时不能碰 node->parent
    if (a_root && a_node->parent == a_root) {
        a_root->DetachChild(a_node);
    }
}
