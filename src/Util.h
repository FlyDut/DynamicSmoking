#pragma once

#include "pch.h"

#include <cctype>
#include <string>
#include <string_view>

namespace stl
{
    template <class F, std::size_t vtbl_idx, class T>
    void write_vfunc()
    {
        REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[vtbl_idx] };
        T::func = vtbl.write_vfunc(T::idx, T::thunk);
    }

    template <class F, class T>
    void write_vfunc()
    {
        write_vfunc<F, 0, T>();
    }
}

namespace DSC
{
    [[nodiscard]] inline std::string NormalizePath(std::string_view a_path)
    {
        constexpr auto is_space = [](char a_c) {
            return a_c == ' ' || a_c == '\t' || a_c == '\r' || a_c == '\n';
        };

        auto begin = a_path.begin();
        auto end = a_path.end();
        while (begin != end && is_space(*begin)) {
            ++begin;
        }
        while (end != begin && is_space(*(end - 1))) {
            --end;
        }

        std::string out;
        out.reserve(static_cast<std::size_t>(end - begin));
        for (auto it = begin; it != end; ++it) {
            char c = *it;
            if (c == '/') {
                c = '\\';
            }
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return out;
    }

    [[nodiscard]] inline std::string_view GetFileName(std::string_view a_path)
    {
        const auto pos = a_path.find_last_of("\\/");
        return pos == std::string_view::npos ? a_path : a_path.substr(pos + 1);
    }
}
