# MultiLanguageCDManager

一个面向 Audio CD / CD-TEXT 研究与实验刻录的**跨平台**工具集。当前重点是**日文 CD-TEXT** 的导入、重建、对照分析，以及持续改进刻录兼容性。

项目目前包含两部分：

- `MultiLanguageCDManager`：图形界面程序，用于导入音频、编辑专辑/轨道文本、准备刻录任务、执行实验刻录。
- `cdtext-diff`：命令行分析工具，用于解析样本、导出当前项目的 CD-TEXT packs、比较结构差异。

## Current Status

- 项目目标是跨平台；当前仓库里的 GUI、设备接入和刻录实验主要先在 **macOS** 上实现与验证。
- 已验证当前这套日文文本链路可以生成 **W789J 可识别** 的 CD-TEXT。
- 仍在继续收敛与已知成功样本之间的低层差异，尤其是 `W770J` 的兼容性。

如果你只想快速开始：

1. 先按下面的编译步骤构建项目。
2. 先运行 `MultiLanguageCDManager`，确认 GUI 可以正常启动。
3. 再运行 `ctest --test-dir build --output-on-failure`，确认当前构建通过回归测试。

## Features

- Cross-platform project structure with current runtime focus on macOS
- Audio CD / blank disc detection on macOS
- Audio import and burn-source preparation
- Japanese fullwidth normalization and MS-JIS encoding pipeline
- CD-TEXT pack assembly and export
- Experimental `cdrdao` / `cdrecord` burn backends

## Requirements

### Build dependencies

- CMake 3.24+
- C++20 compiler
- Qt 6.5+ with components:
  - `Core`
  - `Gui`
  - `Widgets`
  - `Multimedia`
  - `Concurrent`

### Platform status

- **macOS:** current primary development and validation target
- **Windows / Linux:** planned, but not implemented to the same level yet

### Optional libraries

- `libcdio`
  - If present, audio CD playback / track location support is enabled.
  - If absent, the project still builds, but related playback functionality is unavailable.

### External tools used at runtime

Depending on the workflow you test, the app may call some of the following tools:

- `cdrdao`
- `cdrecord`
- `drutil`
- `ffmpeg` / `ffprobe`
- `afconvert` / `afinfo`

For the current Japanese CD-TEXT experiments on macOS, **`cdrdao` is the recommended backend**.

`cdrdao` project links:

- Source repository: [cdrdao/cdrdao](https://github.com/cdrdao/cdrdao)
- Project homepage: [cdrdao.sourceforge.net](https://cdrdao.sourceforge.net/)

### Example dependency install on Homebrew

```bash
brew install cmake qt cdrdao cdrtools libcdio ffmpeg
```

If CMake cannot find Qt automatically, pass `CMAKE_PREFIX_PATH` explicitly:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
```

## Build

```bash
cmake -S . -B build
cmake --build build -j4
```

Generated binaries:

- `build/MultiLanguageCDManager`
- `build/cdtext-diff`
- `build/cdtext-disc-recording-check` (Apple-only helper)

## Run

### GUI app

```bash
./build/MultiLanguageCDManager
```

To explicitly force the current recommended backend:

```bash
CDMANAGER_BURN_BACKEND=cdrdao ./build/MultiLanguageCDManager
```

If you need to force a specific `cdrdao` device target:

```bash
CDMANAGER_BURN_BACKEND=cdrdao CDMANAGER_CDRDAO_DEVICE='0,0,0' ./build/MultiLanguageCDManager
```

### Command-line CD-TEXT tooling

Show available commands:

```bash
./build/cdtext-diff --help
```

Export the current project shape into a normalized JSON report:

```bash
./build/cdtext-diff export-current --json /tmp/cdtext-export.json
```

Export a Sony-style lead-in blob (`18 * N + 1` bytes):

```bash
./build/cdtext-diff export-current --sony-bin-out /tmp/cdtext-leadin.bin
```

## Tests

Enable and run the current regression suite:

```bash
ctest --test-dir build --output-on-failure
```

The repository currently includes regression coverage for:

- exported artifact presence
- Sony-style lead-in blob shape
- project JSON preserving Track 1 title
- exported pack roundtrip
- DiscRecording acceptance checks on the current Apple-side validation path

## Recommended Workflow

For Japanese CD-TEXT work, the current safest order is:

1. Prepare / inspect the project in `MultiLanguageCDManager`
2. Export and compare with `cdtext-diff`
3. Run a simulated burn first
4. Only then run a real burn on disposable media

## Repository Layout

- [`app/`](app): application source
- [`cmake/`](cmake): helper CMake scripts and regression drivers
- [`ARCHITECTURE.md`](ARCHITECTURE.md): layer overview

## Notes

- The project is still experimental.
- Some burn paths are intentionally easier to analyze than to use as final production workflows.
- Compatibility between old head units is sensitive to low-level CD-TEXT structure, not only visible text content.

## License

No license file is included yet. If you plan to publish or redistribute the project, add an explicit license before release.
