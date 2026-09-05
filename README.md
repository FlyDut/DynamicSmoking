# DynamicSmoking

Dynamic Smoking Candles SKSE Plugin

《上古卷轴 5：天际》（Skyrim SE/AE）的 SKSE 插件模组，目标是将 **Smoking Torches and Candles** 模组的功能**配置化**：通过 JSON 配置，将 Addon Node 动态附加到指定模型上，从而用数据驱动的方式替代硬编码的烟雾效果。

## 环境构建

### 前置条件

- **xmake** ≥ 3.0.0（构建系统）
- **clang-cl**（编译器，需安装 LLVM；配合 MSVC 兼容模式 `-fms-compatibility`）
- **Visual Studio**（提供 Windows SDK 与库文件）
- **Git**（用于拉取子模块）

项目使用 **C++23** 标准，仅支持 `windows`/`x64` 平台。

### 依赖

所有第三方依赖通过 Git 子模块管理（位于 `lib/` 目录）：

| 依赖 | 用途 |
| --- | --- |
| [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) | SKSE 插件框架与反向工程 API（`RE`/`REL`/`SKSE` 命名空间） |
| [spdlog](https://github.com/gabime/spdlog) | 日志库 |
| [glaze](https://github.com/stephenberry/glaze) | JSON 配置解析 |
| [SimpleIni](https://github.com/brofield/simpleini) | INI 设置文件解析 |
| [DirectXTK](https://github.com/microsoft/DirectXTK) / [DirectXMath](https://github.com/microsoft/DirectXMath) | CommonLibSSE-NG 依赖 |

### 构建步骤

```bash
# 1. 初始化并拉取所有子模块
git submodule update --init --recursive

# 2. 构建（Debug 或 Release）
xmake f -m release
xmake
```

生成的 DLL 位于 `build/windows/x64/release/`，将其安装至游戏目录 `Data\SKSE\Plugins\` 即可。

## Hook 实现

本模组的 Hook 实现源自 [LightPlacer](https://github.com/powerof3/LightPlacer) 模组，并在此基础上实现了烟雾发射器的配置化附加。

- 配置文件（JSON）放置于 `Data\DynamicSmoking\`
- SKSE设置文件（INI）位于 `Data\SKSE\Plugins\DynamicSmoking.ini`
