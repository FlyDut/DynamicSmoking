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
                    if (!a_backgroundLoading) {
                        Attach(a_this, baseObject, node);
                    } else {
                        // 后台加载线程不能直接改场景图；挂到主线程任务队列后再挂锚点
                        const auto handle = a_this->CreateRefHandle();
                        RE::NiPointer<RE::NiAVObject> nodePtr(node);
                        auto* task = SKSE::GetTaskInterface();
                        if (!task) {
                            logger::error("SKSE task interface unavailable, background attach skipped");
                        } else {
                            task->AddTask([handle, baseObject, nodePtr]() {
                                const auto ref = handle.get();
                                if (!ref) {
                                    return;
                                }
                                RE::NiAVObject* root = nodePtr.get();
                                if (auto* currentRoot = ref->Get3D(); currentRoot && currentRoot != root) {
                                    root = currentRoot;
                                }
                                if (root) {
                                    Attach(ref.get(), baseObject, root);
                                }
                            });
                        }
                    }
                }
            }
            return node;
        }

        static void Attach(T* a_this, RE::TESBoundObject* a_baseObject, RE::NiAVObject* a_root)
        {
            try {
                SmokeManager::GetSingleton()->OnRefLoad3D(a_this, a_baseObject, a_root);
            } catch (...) {
                logger::error("OnRefLoad3D threw, attach skipped for this ref");
            }
        }

        static inline REL::Relocation<decltype(thunk)> func;
        static constexpr std::size_t                   idx{ 0x6A };
    };

    void Install();
}
