#pragma once

#include "pch.h"

#include "Util.h"

#include "glaze/glaze.hpp"

#include <array>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Config
{
    struct StringHash
    {
        using is_transparent = void;  // 启用异构查找

        std::size_t operator()(std::string_view a_str) const
        {
            std::size_t hash = 14695981039346656037ull;
            for (const char c : a_str) {
                hash ^= static_cast<std::size_t>(std::tolower(static_cast<unsigned char>(c)));
                hash *= 1099511628211ull;
            }
            return hash;
        }
    };

    struct StringIequal
    {
        using is_transparent = void;

        bool operator()(std::string_view a_lhs, std::string_view a_rhs) const
        {
            if (a_lhs.size() != a_rhs.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a_lhs.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a_lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(a_rhs[i]))) {
                    return false;
                }
            }
            return true;
        }
    };

    using ModelSet = std::unordered_set<std::string, StringHash, StringIequal>;

    struct NodeRule
    {
        std::string          formId;
        std::array<float, 3> offset{ 0.f, 0.f, 0.f };
    };

    struct TargetSet
    {
        ModelSet              models;
        std::vector<NodeRule> nodes;

        void NormalizePath();
    };
}

template <>
struct glz::meta<Config::NodeRule>
{
    using T = Config::NodeRule;
    static constexpr auto value = object(
        "formId", &T::formId,
        "offset", &T::offset);
};

template <>
struct glz::meta<Config::TargetSet>
{
    using T = Config::TargetSet;
    static constexpr auto value = object(
        "models", &T::models,
        "nodes", &T::nodes);
};
