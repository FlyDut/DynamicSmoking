set_xmakever("3.0.0")

set_project("DynamicSmoking")
set_version("0.1.0")
set_plat("windows")
set_arch("x64")
set_languages("c++23")
set_encodings("utf-8")
set_warnings("allextra")

add_rules("mode.debug", "mode.release")

set_toolchains("clang-cl")

if is_mode("debug") then
    set_runtimes("MTd")
else
    set_runtimes("MT")
end

target("spdlog")
    set_kind("static")
    add_files(
        "lib/spdlog/src/async.cpp",
        "lib/spdlog/src/bundled_fmtlib_format.cpp",
        "lib/spdlog/src/cfg.cpp",
        "lib/spdlog/src/color_sinks.cpp",
        "lib/spdlog/src/file_sinks.cpp",
        "lib/spdlog/src/spdlog.cpp",
        "lib/spdlog/src/stdout_sinks.cpp"
    )
    add_includedirs("lib/spdlog/include", { public = true })
    add_defines("SPDLOG_COMPILED_LIB", { public = true })
    add_cxxflags("/bigobj")

target("commonlibsse")
    set_kind("static")
    add_deps("spdlog")

    add_files("lib/CommonLibSSE-NG/src/**.cpp")
    set_pcxxheader("lib/CommonLibSSE-NG/include/SKSE/Impl/PCH.h")
    add_includedirs("lib/CommonLibSSE-NG/include", { public = true })
    -- DirectXTK
    add_includedirs("lib/DirectXTK/Inc", { public = true })
    add_includedirs("lib/DirectXMath/Inc", { public = true })

    add_defines(
        "WINVER=0x0601",
        "_WIN32_WINNT=0x0601",
        "ENABLE_SKYRIM_SE=1",
        "ENABLE_SKYRIM_AE=1",
        "HAS_SKYRIM_MULTI_TARGETING=1",
        { public = true }
    )

    add_syslinks(
        "advapi32", "bcrypt", "d3d11", "d3dcompiler", "dbghelp", "dxgi",
        "ole32", "shell32", "user32", "version",
        { public = true }
    )

    add_cxxflags("/EHsc", "/permissive-", { public = true })
    add_cxxflags("cl::/Zc:preprocessor", { public = true })
    add_cxxflags("clang_cl::-fms-compatibility", "clang_cl::-fms-extensions", { public = true })
    add_cxxflags(
        "clang_cl::-Wno-delete-non-abstract-non-virtual-dtor",
        "clang_cl::-Wno-deprecated-volatile",
        "clang_cl::-Wno-ignored-qualifiers",
        "clang_cl::-Wno-inconsistent-missing-override",
        "clang_cl::-Wno-invalid-offsetof",
        "clang_cl::-Wno-microsoft-include",
        "clang_cl::-Wno-overloaded-virtual",
        "clang_cl::-Wno-pragma-system-header-outside-header",
        "clang_cl::-Wno-reinterpret-base-class",
        "clang_cl::-Wno-switch",
        "clang_cl::-Wno-unused-private-field",
        { public = true }
    )
    add_headerfiles(
        "lib/CommonLibSSE-NG/include/(RE/**.h)",
        "lib/CommonLibSSE-NG/include/(REL/**.h)",
        "lib/CommonLibSSE-NG/include/(REX/**.h)",
        "lib/CommonLibSSE-NG/include/(SKSE/**.h)"
    )

    on_load(function(target)
        local scriptdir = "lib/CommonLibSSE-NG"
        local version = try { function()
            return os.iorunv("git", { "-C", scriptdir, "describe", "--tags", "--always", "--dirty" })
        end }
        target:add("defines", "COMMONLIB_VERSION=\"" .. (version and version:trim() or "unknown") .. "\"")
    end)

target("DynamicSmoking")
    set_kind("shared")
    add_files("src/**.cpp")
    set_pcxxheader("src/pch.h")
    add_deps("commonlibsse")
    add_defines("NOMINMAX")
    add_includedirs("lib/glaze/include")
    add_includedirs("lib/SimpleIni")
    add_cxxflags("/utf-8", "/permissive-", "cl::/Zc:preprocessor")
