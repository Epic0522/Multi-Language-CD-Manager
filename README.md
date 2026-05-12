# MultiLanguageCDManager

一个面向 Audio CD / CD-TEXT 研究与实验刻录的**跨平台**工具集。当前重点是**日文 CD-TEXT** 的导入、重建、对照分析，以及尽量复现已验证成功样本的刻录结构。

项目目前包含两部分：

- `MultiLanguageCDManager`：图形界面程序，用于导入音频、编辑专辑/轨道文本、准备刻录任务、执行实验刻录。
- `cdtext-diff`：命令行分析工具，用于解析样本、导出当前项目的 CD-TEXT packs、比较结构差异。

## Current Status

- 项目目标是跨平台；当前仓库里的 GUI、设备接入和刻录实验主要先在 **macOS** 上实现与验证。
- 已验证当前这套日文文本链路可以生成 **W789J 可识别** 的 CD-TEXT。
- 仍在继续收敛与已知成功样本之间的低层差异，尤其是 `W770J` 的兼容性。
- 仓库内保留了多张参考盘 / 对照盘抓取包，便于继续做 pack、`SIZE_INFO`、`block` 结构分析。

如果你只想快速开始：

1. 先按下面的编译步骤构建项目。
2. 优先阅读 [`reference_samples/kureha_success_sana_collection_06/README.txt`](reference_samples/kureha_success_sana_collection_06/README.txt)。
3. 用 `cdtext-diff` 跑一遍参考样本导出和对照命令。

## Features

- Cross-platform project structure with current runtime focus on macOS
- Audio CD / blank disc detection on macOS
- Audio import and burn-source preparation
- Japanese fullwidth normalization and MS-JIS encoding pipeline
- CD-TEXT pack assembly and export
- Reference-sample parsing and comparison
- Experimental `cdrdao` / `cdrecord` burn backends
- On-disk sample capture for regression and reverse-engineering work

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

Export the current project shape from a reference sample:

```bash
./build/cdtext-diff export-current \
  --sample-dir reference_samples/kureha_success_sana_collection_06 \
  --json /tmp/kureha-export.json
```

Compare a reference sample with the exported result:

```bash
./build/cdtext-diff compare \
  reference_samples/kureha_success_sana_collection_06 \
  /tmp/kureha-export.json \
  --left-format reference-sample \
  --right-format packs-json
```

Export a Sony-style lead-in blob (`18 * N + 1` bytes):

```bash
./build/cdtext-diff export-current \
  --sample-dir reference_samples/kureha_success_sana_collection_06 \
  --sony-bin-out /tmp/kureha-sony.bin
```

## Tests

Enable and run the current regression suite:

```bash
ctest --test-dir build --output-on-failure
```

The repository currently includes regression coverage for:

- exact reference-sample comparison
- exported artifact presence
- Sony-style lead-in blob shape
- project JSON preserving Track 1 title
- exported pack roundtrip
- DiscRecording acceptance checks for the captured success sample

## Recommended Workflow

For Japanese CD-TEXT work, the current safest order is:

1. Prepare / inspect the project in `MultiLanguageCDManager`
2. Export and compare with `cdtext-diff`
3. Verify against the success sample in `reference_samples/kureha_success_sana_collection_06`
4. Run a simulated burn first
5. Only then run a real burn and immediately capture the disc back into `reference_samples/`

## Repository Layout

- [`app/`](app): application source
- [`cmake/`](cmake): helper CMake scripts and regression drivers
- [`reference_samples/`](reference_samples): captured discs and comparison baselines
- [`ARCHITECTURE.md`](ARCHITECTURE.md): layer overview
- [`REVERSE_ENGINEERING_KUREHA_CD_TEXT.md`](REVERSE_ENGINEERING_KUREHA_CD_TEXT.md): reverse-engineering notes for the KUREHA toolchain

## Reference Samples

The most important baseline is:

- [`reference_samples/kureha_success_sana_collection_06`](reference_samples/kureha_success_sana_collection_06)

It is the current known-good captured sample used to evaluate pack layout and structure.

Additional captured discs document intermediate and failing states, including:

- `test_disc_03`
- `test_disc_04`
- `test_disc_05_failed_cdrecord_burn`
- `test_disc_06_half_burn_w789_only`
- `test_disc_07_kureha_burn_elma`

These directories are intentionally kept in the repo because they are part of the reverse-engineering evidence chain.

## Notes

- The project is still experimental.
- Some burn paths are intentionally easier to analyze than to use as final production workflows.
- Compatibility between old head units is sensitive to low-level CD-TEXT structure, not only visible text content.

## License

No license file is included yet. If you plan to publish or redistribute the project, add an explicit license before release.
