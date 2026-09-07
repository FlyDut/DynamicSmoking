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

## 配置规则

模组在 `Data\DynamicSmoking\` 目录下递归扫描所有 `.json` 文件作为配置文件，每个文件内容为一个规则数组（`TargetSet` 列表）。

### 配置文件格式

```json
[
  {
    "models": [
      "meshes\\clutter\\candlehorn01.nif",
      "candlehorn02.nif"
    ],
    "nodes": [
      {
        "formId": "SomePlugin.esp|0x800",
        "offset": [0.0, 0.0, 0.0]
      }
    ]
  }
]
```

### 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `models` | `string[]` | 需要附加挂点的模型集合，大小写不敏感 |
| `nodes` | `object[]` | 该组模型要附加的挂点规则列表 |
| `nodes[].formId` | `string` | Addon Node 的表单引用，格式为 `插件名\|formID`（十六进制，可带 `0x` 前缀），须指向一个 `BGSAddonNode` 记录 |
| `nodes[].offset` | `number[3]` | 挂点相对模型根节点的三维偏移 `[x, y, z]`（游戏单位） |

### 模型匹配规则

- 若模型路径包含 `\`，视为相对 `meshes\` 的**全路径**匹配（`/` 会自动归一化为 `\`）；
- 否则仅按**文件名**匹配。

全路径命中时作为特例生效，不再叠加文件名规则；同一模型出现在多个配置中时，其规则会被合并。

### formId 解析

`formId` 由 `|` 分隔为两部分：

- 左半部分：插件名（含 `.esp`/`.esm`/`.esl` 后缀）；
- 右半部分：十六进制 formID（`0x` 前缀可选，如 `0x800` 或 `800`）。

解析后通过 `TESDataHandler::LookupForm<BGSAddonNode>` 定位 Addon Node 记录，其 `index` 作为 `BSValueNode` 的 `value`，交由引擎的 `AttachAddonNodes` 完成烟雾/粒子发射器的实际附加。

### 设置文件（INI）

SKSE 设置文件位于 `Data\SKSE\Plugins\DynamicSmoking.ini`，`[Settings]` 段包含：

| 键 | 默认值 | 说明 |
| --- | --- | --- |
| `fMaxDistance` | `900.0` | 烟雾可见的最大距离 |
| `fMinDistance` | `400.0` | 视线裁剪起效的最小距离 |
| `bEnableLineOfSightCulling` | `false` | 是否启用视线裁剪 |
