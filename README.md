# DynamicSmoking

Dynamic Smoking Candles SKSE Plugin

> **中文**：[README.zh.md](README.zh.md)

An SKSE plugin mod for *The Elder Scrolls V: Skyrim* (SE/AE), aiming to make the features of the **Smoking Torches and Candles** mod **configurable**: attach Addon Nodes to specified models dynamically via JSON configuration, replacing hardcoded smoke effects with a data-driven approach.

## Build Environment

### Prerequisites

- **xmake** ≥ 3.0.0 (build system)
- **clang-cl** (compiler; requires LLVM, used with MSVC compatibility mode `-fms-compatibility`)
- **Visual Studio** (provides Windows SDK and library files)
- **Git** (used to pull submodules)

The project uses the **C++23** standard and only supports the `windows`/`x64` platform.

### Dependencies

All third-party dependencies are managed via Git submodules (located in the `lib/` directory):

| Dependency | Purpose |
| --- | --- |
| [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) | SKSE plugin framework and reverse-engineering API (`RE`/`REL`/`SKSE` namespaces) |
| [spdlog](https://github.com/gabime/spdlog) | Logging library |
| [glaze](https://github.com/stephenberry/glaze) | JSON configuration parsing |
| [SimpleIni](https://github.com/brofield/simpleini) | INI settings file parsing |
| [DirectXTK](https://github.com/microsoft/DirectXTK) / [DirectXMath](https://github.com/microsoft/DirectXMath) | Dependencies of CommonLibSSE-NG |

### Build Steps

```bash
# 1. Initialize and pull all submodules
git submodule update --init --recursive

# 2. Build (Debug or Release)
xmake f -m release
xmake
```

The generated DLL is located in `build/windows/x64/release/`; install it to the game directory `Data\SKSE\Plugins\`.

## Hook Implementation

The hook implementation of this mod is derived from the [LightPlacer](https://github.com/powerof3/LightPlacer) mod, extended with configurable attachment of smoke emitters.

## Configuration Rules

The mod recursively scans all `.json` files under the `Data\DynamicSmoking\` directory as configuration files. Each file contains an array of rules (a list of `TargetSet`).

### Configuration File Format

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

### Field Reference

| Field | Type | Description |
| --- | --- | --- |
| `models` | `string[]` | The set of models to attach nodes to; case-insensitive |
| `nodes` | `object[]` | The list of node rules to attach to this group of models |
| `nodes[].formId` | `string` | The form reference of the Addon Node, in the format `plugin name\|formID` (hexadecimal, optional `0x` prefix); must point to a `BGSAddonNode` record |
| `nodes[].offset` | `number[3]` | The 3D offset `[x, y, z]` of the node relative to the model root node (in game units) |

### Model Matching Rules

- If the model path contains `\`, it is treated as a **full-path** match relative to `meshes\` (`/` is automatically normalized to `\`);
- Otherwise it matches by **file name** only.

A full-path match takes precedence as a special case and no longer applies the file-name rule; when the same model appears in multiple configurations, its rules are merged.

### formId Parsing

`formId` is split by `|` into two parts:

- Left part: the plugin name (including the `.esp`/`.esm`/`.esl` suffix);
- Right part: the hexadecimal formID (`0x` prefix optional, e.g. `0x800` or `800`).

After parsing, the Addon Node record is located via `TESDataHandler::LookupForm<BGSAddonNode>`, and its `index` is used as the `value` of a `BSValueNode`, which is passed to the engine's `AttachAddonNodes` to actually attach the smoke/particle emitters.

### Settings File (INI)

The SKSE settings file is located at `Data\SKSE\Plugins\DynamicSmoking.ini`; the `[Settings]` section contains:

| Key | Default | Description |
| --- | --- | --- |
| `fMaxDistance` | `900.0` | Maximum distance at which the smoke is visible |
| `fMinDistance` | `400.0` | Minimum distance at which line-of-sight culling takes effect |
| `bEnableLineOfSightCulling` | `false` | Whether line-of-sight culling is enabled |