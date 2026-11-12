# PRISM 验证记录（t8）

- 日期：2026-11-12
- 验证人：verifier
- 任务：t8 整机验证，attempt `f1ba2962-451f-4242-8fb1-6eb549cd74df`
- HEAD：`c564395198ad9deca955ce69117bec1c538889c8`
- 工作区：`/Volumes/外置SSD/Codes.localized/prism`；验证开始时 `git status --short` 为空
- 环境：macOS 27.0.1（26A434），arm64；CMake 4.4.5；Ninja 1.13.2；Apple clang 21.0.0；JUCE 8.0.7（vcpkg `arm64-osx`）

## 结论

t8 六条验收全部 **PASS**。release 三格式产物齐全，AU/VST3/Standalone 的 `codesign --verify --deep --strict` 均退出 0；`PrismTests` 22 条用例全过（debug 与 release 各跑一遍，退出 0）；标识码、bundle id、版本、5 个参数与状态 version 均与 spec 一致；延迟 1152 由代码与冲激测试双重确认。

未执行：`auval`、`pluginval`、Standalone/宿主加载、听感。原因见「未执行项」。这些不是本次失败项，但 PRISM 0.1.0 还不能算“全绿”。

另有 4 条 spec 9.1 覆盖观察，见文末。它们不推翻上述六条验收，但会影响“spec 9.1 自动化全部覆盖”的说法。

## 验收逐项

| # | 条款 | 结果 | 证据 |
|---|---|---|---|
| 1 | debug 与 release 全量构建成功，退出码 0 | PASS | debug `115` 个目标、release clean-first `146` 个目标，均退出 0；项目源码 0 warning |
| 2 | `PrismTests` 全部通过，输出贴出 | PASS | `./build/debug/PrismTests` 退出 0；22 条 `Starting tests`，0 条失败；release 同款 22 条、退出 0 |
| 3 | release 三格式产物存在，AU 与 VST3 `codesign` 通过 | PASS | AU/VST3/Standalone 均存在；三条 `codesign --verify --deep --strict` 均退出 0 |
| 4 | 源码无裸 `new`/`delete`/`malloc`/`free`（列例外） | PASS | `grep -rnE '\b(new\|delete\|malloc\|free\|calloc\|realloc)\b' src tests` 无调用；仅 3 处注释 |
| 5 | 标识码、bundle id、版本、5 个参数 ID 与 spec 一致 | PASS | `CMakeLists.txt:55-59`；AU plist；`src/Parameters.h:17-42`；状态 `version=1` |
| 6 | 延迟 1152（代码与产物确认） | PASS | `PrismDsp.h:40`；`Filterbank.h:40`；`PluginProcessor.cpp:25,47`；冲激峰在样本 1152、幅度 1.000000 |

### 1. 构建

命令与退出码：

```sh
cmake --preset debug && cmake --build --preset debug          # exit 0（115 个目标）
cmake --preset release && cmake --build --preset release --clean-first  # exit 0（146 个目标）
```

- debug 为全量构建（build 目录由本次 `cmake --preset debug` 重新生成）。
- release 先跑增量命令（`ninja: no work to do`，exit 0），再跑 `--clean-first` 全量重编，确认不是复用旧产物。
- 项目源码（`src/`、`tests/`）0 warning。debug 日志 11 条、release clean 日志 14 条 `warning:` 全部来自 vcpkg 的 JUCE 8.0.7 头文件（Metal 弃用、VST3 SDK `memset` 等），与本项目代码无关。

### 2. 测试

```sh
./build/debug/PrismTests    # exit 0
./build/release/PrismTests  # exit 0（额外回归）
```

两次都是 22 条用例、0 失败。关键实测值（debug 日志）：

| 用例 | 实测 |
|---|---|
| Dsp 恒等（AMOUNT 0） | 延迟对齐后最大误差 `1.19209e-7` |
| Dsp 压缩（1 kHz、−6 dBFS、AMOUNT 100） | 输出比输入低 `6.38 dB` |
| Dsp 延迟 | 冲激峰样本 `1152`，值 `1.000000` |
| Dsp 平滑 | 20 ms 后 `0.993262`，最大相邻差 `0.065263` |
| Filterbank 重建 | 最大误差 `2.38e-7`（约 −132.5 dBFS） |
| Filterbank 隔离 | 段 2 在 40 Hz 泄漏 `−32.79 dB` |
| Filterbank 边缘 | 段 1 在 80 Hz `−5.99 dB` |
| Filterbank 96 kHz | 重建误差 `2.98e-7`，80 Hz `−5.61 dB` |
| OptoBand 静态斜率 | `1.502 / 1.802 / 2.000 / 2.200 / 2.501 / 2.799 / 2.999` |
| OptoBand 阶跃 | 7 段 attack `10.00 ms`、release `60.00 ms` |
| OptoBand 释放拟合 | 20%–80% 最大偏差 `0.189–0.378 dB` |
| 参数状态往返 | `amount=77.000 spectrum=-42.000 mix=33.000 input=9.500 output=-13.250` |
| Processor 延迟 | `impulse peak at sample 1152, magnitude 1.000000` |

`Starting tests` 共 22 条：Dsp 5、Editor 1、Filterbank 5、OptoBand 5、Parameters 4、Processor 2。

### 3. 产物与签名

| 产物 | 路径 | 架构 | `codesign --verify --deep --strict` |
|---|---|---|---|
| AU | `build/release/PrismAU_artefacts/Release/AU/PRISM.component` | Mach-O arm64 bundle | exit 0，adhoc |
| VST3 | `build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3` | Mach-O arm64 bundle | exit 0，adhoc |
| Standalone | `build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app` | Mach-O arm64 executable | exit 0，adhoc |

三条都用 `--verbose=2` 确认输出 `valid on disk` 与 `satisfies its Designated Requirement`。

### 4. 裸指针扫描

```sh
grep -rnE '\b(new|delete|malloc|free|calloc|realloc)\b' src tests --include='*.h' --include='*.cpp'
```

无任何调用。命中 3 条注释：

- `src/PluginEditor.h:14`
- `src/Parameters.h:46`
- `src/PluginProcessor.cpp:96`

RAII 用法：参数与编辑器控件用 `std::make_unique` 创建；JUCE 强制返回裸所有权的 `createEditor()` 与 `createPluginFilter()` 用 `std::make_unique<...>().release()` 转移所有权（`src/PluginProcessor.cpp:97,120`），不是裸 `new`。`getStateInformation` 用 `std::unique_ptr` 接管 `getXmlFromBinary` 的返回值（`src/PluginProcessor.cpp:110`）。

### 5. 标识码、版本、参数

CMake：

- `CMakeLists.txt:7`：`project(PRISM VERSION 0.1.0 ...)`
- `CMakeLists.txt:55`：`BUNDLE_ID "com.stargazer.prism"`
- `CMakeLists.txt:56`：`PLUGIN_MANUFACTURER_CODE Stgz`
- `CMakeLists.txt:57`：`PLUGIN_CODE Prsm`
- `CMakeLists.txt:59`：`VERSION "0.1.0"`

AU `Contents/Info.plist` 实测：

| 键 | 值 |
|---|---|
| `CFBundleIdentifier` | `com.stargazer.prism` |
| `CFBundleShortVersionString` | `0.1.0` |
| `AudioComponents[0].type` | `aufx` |
| `AudioComponents[0].subtype` | `Prsm` |
| `AudioComponents[0].manufacturer` | `Stgz` |
| `AudioComponents[0].version` | `256`（AU 编码） |
| `AudioComponents[0].name` | `Stargazer: PRISM` |

VST3 与 Standalone 的 `CFBundleIdentifier` 同为 `com.stargazer.prism`，版本同为 `0.1.0`。

5 个参数（`src/Parameters.h:17-21,36-42`）：

| ID | 名称 | 范围 | 默认 |
|---|---|---|---|
| `amount` | AMOUNT | 0–100 % | 50 |
| `spectrum` | SPECTRUM | −100–+100 | 0 |
| `mix` | MIX | 0–100 % | 100 |
| `input` | INPUT | −12–+24 dB | 0 |
| `output` | OUTPUT | −24–+12 dB | 0 |

状态 version：`src/PluginProcessor.cpp:20` 写入 `version=1`；`ParamTests` 的状态往返用例断言 `destination.apvts.state.getProperty("version") == 1`，通过。

### 6. 延迟

代码三重一致：

- `src/dsp/PrismDsp.h:40`：`kLatencySamples = 1152`
- `src/dsp/Filterbank.h:40`：`latencySamples() = taps/2 + kBlockSize = 1024 + 128 = 1152`
- `src/PluginProcessor.cpp:25,47`：构造与 `prepareToPlay` 都 `setLatencySamples(prism::PrismDsp::kLatencySamples)`

实测：

- `ProcessorTests` 在空构造、48 kHz/512、96 kHz/128、`releaseResources()` 后四次断言 `getLatencySamples() == 1152`，全部通过。
- 处理器级冲激：`impulse peak at sample 1152, magnitude 1.000000`。
- Dsp 级与 Filterbank 级冲激也都在样本 1152。

## 静态检查细节

### 音频线程分配

结论：`processBlock` 路径无分配；分配只发生在构造与 `prepare`。

- 分配点（非音频线程）：`Filterbank.cpp:46-58`、`Filterbank.cpp:98-119`（`rebuild` 的局部 vector）、`PrismDsp.cpp:62-70`（`band.setSize`、`delay.assign`）。
- 音频路径：`PluginProcessor.cpp:64-92` 只创建栈上 `EngineParams`、做 5 次 atomic load、调用 `dsp.setParams/processBlock`。
- `PrismDsp.cpp:134-241`：逐样本循环，只访问预分配成员；`PrismDsp.cpp:155-156` 的 `juce::AudioBuffer inputView` 用外部指针构造，不拥有、不分配。
- `Filterbank.cpp:186-233`：`std::copy`/`std::fill` 写入预分配的 `history_`、`pending_`、`rings_`、`work_`、`product_`；FFT 原地运算；无 `resize`/`push_back`。
- `OptoBand.cpp:54-75`：纯标量运算，无容器。

限制：以上是源码路径审查，未用分配器 hook 做运行时插桩。

### 锁与 I/O

```sh
grep -rnE 'mutex|lock_guard|ScopedLock|CriticalSection|spin|wait\(' src --include='*.h' --include='*.cpp'   # 无命中
grep -rnE 'File|FileInputStream|FileOutputStream|URL|http|socket|printf|std::cout|fopen' src --include='*.h' --include='*.cpp'   # 无命中
```

音频线程只做 atomic load/store（参数指针与 meters，`memory_order_relaxed`）。参数写入走 APVTS/消息线程，音频线程不查表、不加锁。

### jassert 依赖

`jassert` 共 14 处，全部是调试期不变量检查；release 行为不依赖断言：

- `PrismDsp.cpp:46-48,55`：`prepare` 的前置条件；调用方 `PluginProcessor::prepareToPlay` 已在 `PluginProcessor.cpp:39-40` 对非法采样率/块大小提前返回。
- `PrismDsp.cpp:141`：`jassertfalse` 后紧跟 `return`，release 下也是安全的“不处理”。
- `Filterbank.cpp:199-203`：段缓冲不足时 `jassertfalse` 后 `return`；缓冲只在 `PrismDsp::prepare` 中按块大小分配。
- `Filterbank.cpp:42-43,87,96,195`、`FirDesign.cpp:14-16`：构造/准备阶段的参数与状态检查。
- `OptoBand::prepare` 对 `sampleRate <= 0` 直接返回（`OptoBand.cpp:25-26`），不靠断言。

## spec 9.1 覆盖观察

以下 4 条不影响 t8 六条验收，但按“不美化”原则列出。

1. **单段压缩重建没有自动化用例。** spec 9.1 第 3 条要求“只压一段 6 dB，测该段两侧一个倍频程外的偏差 < 0.5 dB”。现有 22 条用例中没有这一项；`FilterbankTests` 的 isolation 只测静态分频泄漏，`DspTests` 只测全段 1 kHz 压缩。该项无法判定 PASS/FAIL。
2. **恒等测试阈值宽于 spec。** `DspTests.cpp:98` 用 `expectLessThan(maxError, 1e-4)`（约 −80 dBFS），spec 9.1 要求低于 −120 dBFS。实测 `1.19209e-7`（约 −138.5 dBFS）满足 spec，但测试本身拦不住退化到 −80 dB 的回归。
3. **隔离实测比 spec 5.2 的表值差 0.51 dB，且只测了一个点。** spec 5.2 给 2049 抽头的段外泄漏 `−33.3 dB`；测试只测“段 2 在 40 Hz”得 `−32.79 dB`，阈值写 `−32 dB`。没有逐段、逐倍频程测最大泄漏，不能确认 spec 5.2 的表格值是否整体满足。
4. **释放曲线没有按 spec 9.1 记录 90% 恢复时间与快/尾段。** 现有测试记录检测器 attack/release `10/60 ms`，并对 20%–80% 区间做 dB 域线性拟合（偏差 `0.189–0.378 dB`）；没有记录 90% 恢复时间，也没有分别记录快段与尾段。`BandTable.h:25-27` 已注明 `releaseFastMs/releaseTailMs` 是 M4 设计参考、当前不是独立时间常数。

## 未执行项（待用户批准或在场）

1. **auval（需要写 `~/Library`，本次沙箱与用户指令都不允许）**
   - 目的：验证 AU 在 macOS 音频单元扫描器下完整通过。
   - 步骤：`cmake --preset release -DPRISM_COPY_PLUGINS=ON && cmake --build --preset release --target PrismAU_AU`，把 `PRISM.component` 安装到 `~/Library/Audio/Plug-Ins/Components/`。
   - 命令：`auval -v aufx Prsm Stgz`
   - 预期：输出 `AU VALIDATION SUCCEEDED`，退出 0。
2. **pluginval（本机未安装）**
   - 命令：`pluginval --strictness-level 5 --validate build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3`
   - 预期：全部测试通过；未安装，未执行。
3. **Standalone 启动与设备出声**：需要 GUI 与音频设备，用户离开期间未执行。
4. **Logic/Reaper 加载、参数自动化、延迟补偿、离线渲染对齐**：需要宿主，未执行。
5. **听感（鼓组、人声、贝斯对比干声）**：需要人耳，未执行。

## 复现命令

```sh
cd /Volumes/外置SSD/Codes.localized/prism

cmake --preset debug
cmake --build --preset debug
./build/debug/PrismTests

cmake --preset release
cmake --build --preset release --clean-first
./build/release/PrismTests

codesign --verify --deep --strict --verbose=2 build/release/PrismAU_artefacts/Release/AU/PRISM.component
codesign --verify --deep --strict --verbose=2 build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3
codesign --verify --deep --strict --verbose=2 build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app

grep -rnE '\b(new|delete|malloc|free|calloc|realloc)\b' src tests --include='*.h' --include='*.cpp'
grep -rnE 'mutex|lock_guard|ScopedLock|CriticalSection|spin|wait\(' src --include='*.h' --include='*.cpp'
```

---

## t12 复验（当前 HEAD）

- 日期：2026-11-12
- 验证人：verifier
- 任务：t12，attempt `0c024448-f09d-4547-8ff5-9e1846b029af`
- HEAD：`55ebde9395074349d1dbcf5dc94cf2c8438def16`（验证时；本段只改文档）
- 工作区：验证开始时 `git status --short` 为空
- 环境：同 t8；macOS 27.0.1 arm64、CMake 4.4.5、Ninja 1.13.2、Apple clang 21.0.0、JUCE 8.0.7（vcpkg `arm64-osx`）

### 结论

构建、测试、产物签名、代码约束均 PASS。文档一致性有 4 类问题（见 §5）：README:10 的 auval 声明是事实错误；spec §7 未覆盖 t11 界面细节且与“Logo / 可缩放”冲突；plan Task 3 仍是旧检测器与旧 `s_b`；spec 9.1 的单段压缩与快/尾段记录仍无用例。`auval`、`pluginval` 仍未执行。

### 1. 构建

| 构建 | 命令 | 结果 |
|---|---|---|
| debug | `cmake --preset debug` + `cmake --build --preset debug --clean-first` | exit 0；146 个目标；14 条 warning，`src/`、`tests/` 0 条 |
| release | `cmake --preset release -DPRISM_COPY_PLUGINS=OFF --fresh` + `cmake --build --preset release --clean-first` | exit 0；146 个目标；14 条 warning，`src/`、`tests/` 0 条 |

两套 warning 全部来自 vcpkg 的 JUCE 8.0.7 头文件（Metal 弃用、VST3 SDK `memset` 等），与本项目源码无关。日志：`/tmp/t12_build_debug.log`、`/tmp/t12_build_release.log`。

### 2. 测试

`./build/debug/PrismTests` 与 `./build/release/PrismTests` 均 exit 0，各 25 条 `Starting tests`、0 条失败。

| 用例 | 实测（两套构建一致） |
|---|---|
| Dsp 恒等（AMOUNT 0） | 最大误差 `1.19209e-7` |
| Dsp 压缩（1 kHz、−6 dBFS、AMOUNT 100） | 输出比输入低 `−6.38 dB` |
| Dsp 延迟 | 冲激峰样本 `1152`，值 `1.000000` |
| Dsp 平滑 | 20 ms 后 `0.993262`，最大相邻差 `0.065263` |
| Filterbank 重建 | 最大误差 `2.38e-7` |
| Filterbank 隔离 | 段 2 在 40 Hz 泄漏 `−32.79 dB` |
| Filterbank 边缘 | 段 1 在 80 Hz `−5.99 dB` |
| Filterbank 96 kHz | 重建误差 `2.98e-7` |
| OptoBand 静态斜率 | `1.502 / 1.802 / 2.000 / 2.200 / 2.501 / 2.799 / 2.999` |
| OptoBand 90% 恢复 | `232.5 / 202.8 / 175.5 / 155.2 / 139.7 / 125.2 / 114.0 ms` |
| OptoBand 释放拟合 | 20%–80% 最大偏差 `0.189–0.378 dB` |
| 参数状态往返 | `77 / −42 / 33 / 9.5 / −13.25`，version 仍为 1 |

用例分布：Dsp 5、Editor 2、Filterbank 6、OptoBand 6、Parameters 4、Processor 2，共 25。

### 3. 产物

| 产物 | 路径 | 架构 | `codesign --verify --deep --strict` |
|---|---|---|---|
| AU | `build/release/PrismAU_artefacts/Release/AU/PRISM.component` | Mach-O arm64 bundle（thin） | exit 0，valid on disk / satisfies Designated Requirement |
| VST3 | `build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3` | Mach-O arm64 bundle（thin） | exit 0，同上 |
| Standalone | `build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app` | Mach-O arm64 executable（thin） | exit 0，同上 |

AU `Info.plist` 实测：`AudioComponents[0].type=aufx`、`subtype=Prsm`、`manufacturer=Stgz`、`CFBundleIdentifier=com.stargazer.prism`、`CFBundleShortVersionString=0.1.0`、`AudioComponents[0].version=256`。与 `CMakeLists.txt:7,55-59` 一致。

### 4. 代码约束

- 裸分配：`grep -rnE '\b(new|delete|malloc|free)\b' src tests` 只命中 3 条注释（`src/PluginEditor.h:14`、`src/Parameters.h:46`、`src/PluginProcessor.cpp:96`），无调用。
- 音频线程分配：分配只在 `Filterbank.cpp:46-58,98-119`、`PrismDsp.cpp:62-70` 的构造/`prepare` 路径；`PrismDsp::processBlock`（`PrismDsp.cpp:134-241`）、`Filterbank::processBlock`（`Filterbank.cpp:186-233`）、`OptoBand::processSample`（`OptoBand.cpp:54-75`）无 `new/delete/malloc/resize/assign/push_back`。
- 锁与 I/O：`mutex|lock_guard|ScopedLock|CriticalSection|spin|wait\(` 在 `src/` 无命中；`File|fopen|printf|std::cout|http|socket` 无命中。
- `jassert` 共 14 处，均有 release 路径保护：`PrismDsp.cpp:46-48,55` 是 `prepare` 前置条件；`PrismDsp.cpp:141` 后紧跟 `return`；`Filterbank.cpp:42-43,87,96,195,201` 为准备/缓冲检查，`201` 后 `return`；`FirDesign.cpp:14-16` 为设计参数检查。
- 限制：以上为源码路径审查，未用分配器 hook 做运行时插桩。

### 5. 文档一致性

#### README

一致项：

- 构建命令与产物路径（`README.md:33-39`）与 CMakePresets/构建结果一致。
- 延迟 1152、48 kHz 24.0 ms、96 kHz 12.0 ms（`README.md:27`）与 `kLatencySamples` 及换算一致。
- 5 个参数范围/默认值（`README.md:71-77`）与 `src/Parameters.h:36-42` 一致。
- 频段颜色、波长、γ、目标斜率（`README.md:81-89`）与 `src/dsp/BandTable.h:46-54` 一致。
- “25 条自动化测试”（`README.md:10`）与实测一致。

不一致：

1. **`README.md:10` 写“AU 通过 `auval -v aufx Prsm Stgz`”，这是事实错误。** 本会话无法写 `~/Library/Audio/Plug-Ins/Components`，auval 从未执行；t8 与 t12 都把它列为待用户批准项。发布前必须删除该句或改成“待验证”。
2. **`README.md:114` 写“释放曲线只有单一时间常数 60 ms”，不准确。** 实际释放 = 检测器 60 ms 与 `τ_ldr`（10–80 ms）叠加；spec 5.4 实测 90% 恢复为 114.0–232.5 ms。建议改成“检测器 release 60 ms，无独立双时间常数”。

#### spec

已覆盖：

- 前馈增益律与 `1 − exp(...)` 权重：`spec:141-158`。
- 重标 `s_b`（−18.14…−10.67）：`spec:168-178`。
- 段外泄漏 −32.79 dB：`spec:128`。
- 90% 恢复时间 114.0–232.5 ms：`spec:184`。

遗漏/冲突：

1. **§7 界面（`spec:228-236`）未覆盖 t11 实现**：静音辉光、en dash 频段标签、SPECTRUM 的“%”与正值“+”均未写。且 `spec:232` 要求 Stargazer 标志，实现只画 “PRISM” 文字；`spec:236` 要求“可缩放”，实现 `setResizable(false,false)`（`src/PluginEditor.cpp:48`）。spec 需补界面细节或改实现。
2. **spec 5.3 列表编号缺 3**（`spec:141-152` 为 1、2、4、5），编辑问题。
3. **spec 9.1 单段压缩仍无自动化用例**（`spec:356`）。`tests/FilterbankTests.cpp:241` 的 “single-band reconstruction” 是独立 FIR 卷积重建，不是“只压一段 6 dB、测两侧一个倍频程外偏差 < 0.5 dB”。
4. **spec 9.1 快段/尾段未分别记录**（`spec:358`）。`tests/OptoBandTests.cpp:172` 只记录总 90% 恢复时间，未记录 fast/tail 两段。

#### plan

1. **Task 3 仍写旧检测器与旧 `s_b`**：`plan:208` 的 sensitivityDb 为 −12.1/−6.6/−4.5/−2.9/−0.7/+1.1/+1.6，`plan:214` 写系数 `exp(-1/(tau·fs))`。二者都与 spec 5.3/5.4、`src/dsp/OptoBand.cpp:31-32` 和 `BandTable.h:47-53` 相反。照 plan 实现会退回近瞬时跟踪。
2. **Task 3 释放验收（`plan:192-195`）已过期**：要求 90% 恢复落在 `fastMs/2` 与 `slowMs*2`，当前测试改为总 90% 恢复 + 20%–80% 拟合；需同步 plan。
3. Task 4 验收（`plan:254-259`）与实现相符：恒等 < 1e-4、压缩 > 3 dB、延迟 1152、平滑 20 ms。

### 6. 未执行项

1. **auval**：需把 `PRISM.component` 安装到 `~/Library/Audio/Plug-Ins/Components/`，本会话无写权限，等用户批准。命令 `auval -v aufx Prsm Stgz`，预期 `AU VALIDATION SUCCEEDED`。
2. **pluginval**：本机未安装，未执行。
3. Standalone 启动/设备出声、Logic/Reaper 加载与延迟补偿、听感：需 GUI/宿主/人耳，未执行。

### 7. t12 验收

| # | 条款 | 结果 | 证据 |
|---|---|---|---|
| 1 | debug/release 全量构建 exit 0，`src/`、`tests/` 0 warning | PASS | 各 146 目标；14 条 warning 全在 JUCE 头文件 |
| 2 | 两套 PrismTests 各 25 条、0 失败、exit 0 | PASS | debug/release 各 25 条；关键数字见 §2 |
| 3 | 三产物 arm64 thin，codesign 全过，AU plist 正确 | PASS | 三条 codesign exit 0；plist 值见 §3 |
| 4 | 无裸分配；音频线程无分配/锁/IO；jassert 有 release 保护 | PASS | §4；限制：无运行时 hook |
| 5 | README 构建命令/路径/延迟/参数/频段与代码一致，不一致处列出 | PASS | 枚举项一致；额外错误见 §5：README:10、README:114 |
| 6 | spec/plan 覆盖当前实现，遗漏处列出 | PASS | 前馈、`s_b`、泄漏、90% 已覆盖；界面、plan Task 3、spec 9.1 缺口见 §5 |
| 7 | 验证记录追加 t12 段并提交一次，工作区干净 | PASS | 本段与一次文档提交 |

## t14 终验（最终 HEAD）

- 任务：t14，attempt `cf51ab26-6a52-44f8-97c7-7f0f18640b8e`
- 验证开始 HEAD：`1e5b8b7`；验证期间队长提交 `c780285`（仅 `README.md` 测试条数 25→26），`src/`、`tests/`、`CMakeLists.txt` 未变。本段记录以 `c780285` 的代码字节为准。
- 工作区：开始时 `git status --short` 为空；结束时仅本文件改动，提交后干净。
- 环境同 t8：macOS 27.0.1（26A434），arm64；CMake 4.4.5；Ninja 1.13.2；Apple clang 21.0.0；JUCE 8.0.7（vcpkg `arm64-osx`）。

### 1. 构建与测试

全量构建（clean-first，证明不复用旧产物）：

```sh
cmake --preset debug && cmake --build --preset debug --clean-first
# exit 0；146 个目标；14 条 warning，全部来自 JUCE/vcpkg 头文件

cmake --preset release -DPRISM_COPY_PLUGINS=OFF --fresh && cmake --build --preset release --clean-first
# exit 0；146 个目标；14 条 warning，全部来自 JUCE/vcpkg 头文件
```

- `src/`、`tests/` 的 warning 数：两套均为 0。debug 的 14 条为 2 条 VST3 SDK `fvariant.h`、12 条 `juce_CGMetalLayerRenderer_mac.h`；release 同源。
- 合同 verify 命令与结果：

```sh
cmake --build --preset debug 2>&1 | tail -3 && ./build/debug/PrismTests 2>&1 | grep -c '^Starting tests in'
# ninja: no work to do；26
cmake --build --preset release 2>&1 | tail -3 && ./build/release/PrismTests 2>&1 | grep -c '^Starting tests in'
# ninja: no work to do；26
```

- 两套 `PrismTests` 均 exit 0，各 26 条 `Starting tests`，0 条 `FAILED`。
- 关键实测（debug 与 release 一致）：
  - 恒等：`1.19209e-7`。
  - 压缩：1 kHz、−6 dBFS，输出比输入低 `−6.38 dB`。
  - 冲激峰：样本 `1152`，值 `1.000000`；Dsp 与 Processor 两条路径均一致。
  - 平滑：20 ms 后 AMOUNT `0.993262`；最大相邻样本差 `0.065263`。
  - 单段 −6 dB 带外偏差：最大 `−0.101 dB`（段 2 @ 40 Hz），其余测点 `0.000` 或 `−0.000 dB`；阈值 0.5 dB。
  - 90% 恢复：`232.5 / 202.8 / 175.5 / 155.2 / 139.7 / 125.2 / 114.0 ms`。
  - 分频重建：48 kHz `2.38e-7`，96 kHz `2.98e-7`。

### 2. 产物

| 产物 | 架构 | codesign |
|---|---|---|
| `build/release/PrismAU_artefacts/Release/AU/PRISM.component` | Mach-O 64-bit bundle arm64 | exit 0 |
| `build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3` | Mach-O 64-bit bundle arm64 | exit 0 |
| `build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app` | Mach-O 64-bit executable arm64 | exit 0 |

合同 verify 命令：

```sh
codesign --verify --deep --strict build/release/PrismAU_artefacts/Release/AU/PRISM.component build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3 build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app
# exit 0，三产物均通过
```

AU `Info.plist`：`AudioComponents.0.type=aufx`、`subtype=Prsm`、`manufacturer=Stgz`、`version=256`（0.1.0 的十六进制）；`CFBundleIdentifier=com.stargazer.prism`、`CFBundleShortVersionString=0.1.0`。

### 3. 代码约束

```sh
grep -rnE '\b(new|delete|malloc|free)\b' src tests | grep -v 'ScopedNoDenormals' | head
```

只命中 3 条注释，无调用：

- `src/PluginEditor.h:14`
- `src/Parameters.h:46`
- `src/PluginProcessor.cpp:96`

- 分配只在构造/`prepare`/`rebuild`：`src/dsp/Filterbank.cpp:46-58,98-119`、`src/dsp/PrismDsp.cpp:62-70`。
- `PrismDsp::processBlock`（`src/dsp/PrismDsp.cpp:134-241`）、`Filterbank::process`（`src/dsp/Filterbank.cpp:186-233`）、`OptoBand::processSample`（`src/dsp/OptoBand.cpp:54-75`）无分配、无锁、无 I/O。`grep` 锁/IO 模式在 `src/` 无命中。
- `jassert` 共 14 处；两处 `jassertfalse` 均有 release 保护：`src/dsp/PrismDsp.cpp:141-143`（块长越界 return）、`src/dsp/Filterbank.cpp:201-203`（段缓冲不足 return）。其余 `jassert` 在 release 编译为空，仅诊断用。
- 限制：未做运行时分配器 hook，结论来自静态审查与实时代码路径阅读，不是运行时证明。

### 4. 文档回归

| 检查项 | 结果 | 证据 |
|---|---|---|
| README 测试条数 26 | PASS | `README.md:10`、`README.md:103`；`c780285` 修正 |
| README auval 表述 | PASS（按队长指定措辞） | `README.md:11` 写“t1 透传版本通过、当前版本待复验”；但 t1 记录称当时未安装/未运行 auval，历史“通过”无法由本会话独立证实，见文末残留项 |
| README 释放描述 | PASS | `README.md:115` 写检测器 60 ms + 光敏电阻一阶跟随、90% 恢复 114.0–232.5 ms |
| README 参数/频段表 | PASS | `README.md:74-78` 对 `src/Parameters.h:36-42`；`README.md:84-90` 对 `src/dsp/BandTable.h:46-54` 与 `src/dsp/FirDesign.h:13` |
| spec §5.4 释放字段 | PASS | `spec:182` 明确 `releaseFastMs`/`releaseTailMs` 未被读取，留 M4 |
| spec §7 界面 | PASS | `spec:233-239` 与实现一致：PRISM 文字、5% 静音辉光、en dash + Hz、SPECTRUM 百分号/正号、720×480 不可缩放 |
| spec §8.1 构建命令 | PASS | `spec:254` 用 `cmake --preset debug` / `--preset release` |
| spec §8.3 拷贝选项 | PASS | `spec:304` 改为 `PRISM_COPY_PLUGINS` 控制、默认 OFF |
| spec §9.1 释放口径 | PASS | `spec:361` 写 90% 恢复 114.0–232.5 ms，20%–80% 拟合 <1 dB，快/尾段留 M4 |
| plan Task 3 灵敏度 | PASS | `plan:209` 为 −18.14…−10.67 |
| plan Task 3 系数 | PASS | `plan:215` 为 `1 − exp(-1/(tau·fs))` |
| plan Task 3 释放验收 | PASS | `plan:195` 为 90–300 ms、20%–80% 拟合 <1 dB |

### 5. 未执行项

1. **auval**：需安装 AU 到 `~/Library/Audio/Plug-Ins/Components/`，本会话不能写该目录，等用户批准。命令 `auval -v aufx Prsm Stgz`，预期 `AU VALIDATION SUCCEEDED`。
2. **pluginval**：本机未安装，未执行。
3. Standalone 启动/设备出声、Logic/Reaper 加载与延迟补偿、听感：需 GUI/宿主/人耳，未执行。

### 6. t14 验收

| # | 条款 | 结果 | 证据 |
|---|---|---|---|
| 1 | debug/release 全量构建 exit 0，`src/`、`tests/` 0 warning | PASS | 各 146 目标；14 条 warning 全在 JUCE/vcpkg 头文件 |
| 2 | 两套 PrismTests 各 26 条、0 失败，关键数字齐全 | PASS | 各 26 条、exit 0；数字见 §1 |
| 3 | 三产物 arm64 thin、codesign 全过、AU plist 正确 | PASS | §2 |
| 4 | 无裸分配；processBlock 无分配/锁/IO；jassert 有 release 保护 | PASS | §3；限制：无运行时 hook |
| 5 | README/spec/plan 回归检查全部到位 | PASS | §4；auval 历史声明保留 1 条残留 |
| 6 | 记录追加并提交，工作区干净；列出未执行项 | PASS | 本段与一次文档提交；未执行项见 §5 |

**结论**：t14 六条验收全部 PASS。代码、测试、签名与文档回归均与当前 HEAD 一致。

残留项：

1. `README.md:11` 的“t1 透传版本通过 auval”与本记录中 t1 的“auval 未运行”陈述冲突。若 t1 当时确实未跑 auval，需改成“未验证”或由队长补一次 auval 后再保留。
2. 未做运行时分配器 hook；音频线程无分配仍是静态审查结论。
3. auval、pluginval、Standalone/宿主/听感未做。

---

## t15 外部验证（队长执行，2026-11-12 21:41–21:45）

用户批准后由队长在真实宿主环境执行，verifier 未参与。

| # | 项 | 命令 | 结果 |
|---|---|---|---|
| 1 | AU 安装 | `cp -R build/release/PrismAU_artefacts/Release/AU/PRISM.component ~/Library/Audio/Plug-Ins/Components/` + `killall -9 AudioComponentRegistrar` | 安装成功，二进制 3667056 字节 |
| 2 | AU 验证 | `auval -v aufx Prsm Stgz` | `AU VALIDATION SUCCEEDED`（含参数设置、MIDI、44100 渲染、Bad Max Frames 用例） |
| 3 | VST3 验证（严格度 8） | `pluginval --strictness-level 8 --validate-in-process --validate build/release/.../PRISM.vst3` | `SUCCESS` |
| 4 | VST3 验证（严格度 10） | 同上，`--strictness-level 10` | `SUCCESS` |

未通过但属工具限制的一项：

- `pluginval 1.0.4` 的 AU 模式在本机返回 `Num plugins found: 0`（`PRISM.component`，以及第三方 `TBTECH Kirchhoff-EQ.component` 均如此）。结论：macOS 27 上该版本 pluginval 的 AU 扫描不可用，不能作为 AU 失败证据；AU 结论以 auval 为准。

仍待人工确认：Standalone 启动并出声、Logic/Reaper 加载与延迟补偿、听感。

---

## t17 复验（HEAD 881d6d6，t15 分频点 + t16 读数格式）

- 任务：t17，attempt `19ca3ffc-eb3c-455b-b36e-b7c58103f263`
- HEAD：`881d6d6e02c8c603f5e943cfff4ae2d82119f7d7`（t15 `7923b76`、t16 `881d6d6`）；验证开始时 `git status --short` 为空。
- 环境同 t8/t14；验证结束后仅本文件改动。

### 1. 构建与测试

```sh
cmake --preset debug && cmake --build --preset debug --clean-first
# exit 0；146 个目标；14 条 warning，全部来自 JUCE/vcpkg 头文件；src/tests 0 条

cmake --preset release -DPRISM_COPY_PLUGINS=OFF --fresh && cmake --build --preset release --clean-first
# exit 0；146 个目标；14 条 warning，全部来自 JUCE/vcpkg 头文件；src/tests 0 条
```

- debug 的 14 条 warning：2 条 VST3 SDK `fvariant.h`、12 条 `juce_CGMetalLayerRenderer_mac.h`；release 同源。
- 两套 `PrismTests` 各 **28 条** `Starting tests`、0 条 `FAILED`、exit 0。
- 关键实测（debug 与 release 一致）：
  - 恒等：`1.19209e-7`（< 1e-6）。
  - 压缩：1 kHz、−6 dBFS，`−5.46 dB`。**与 t14 的 −6.38 dB 不同**：t15 把 1 kHz 从段 4 移到段 3（500–1200），灵敏度与目标斜率不同。仍大于 3 dB，验收线未变。
  - 冲激峰：样本 `1152`，值 `1.000000`（Dsp 与 Processor 两条路径）。
  - 平滑：20 ms 后 `0.993262`；最大相邻差 `0.065263`。
  - 段 1 @150 Hz：`−6.02 dB`（−6.02 ± 0.5 内）。
  - 段 2 @40 Hz 泄漏：`−101.35 dB`。
  - 单段 −6 dB 带外偏差：11 个新边界测点均 ≤`0.001 dB`（打印为 `0.000`/`−0.000`），最大泄漏 `−86.85 dB`（段 2 @75 Hz）。
  - 96 kHz：重建 `2.38e-7`，段 1 @150 Hz `−5.98 dB`。
  - 11.025 kHz：重建 `2.24e-7`，`latencySamples()==1152` 有断言，无 jassert 触发。
  - 90% 恢复：`232.5 / 202.8 / 175.5 / 155.2 / 139.7 / 125.2 / 114.0 ms`。

### 2. 产物

| 产物 | 架构 | codesign |
|---|---|---|
| `build/release/PrismAU_artefacts/Release/AU/PRISM.component` | Mach-O 64-bit bundle arm64 | exit 0 |
| `build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3` | Mach-O 64-bit bundle arm64 | exit 0 |
| `build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app` | Mach-O 64-bit executable arm64 | exit 0 |

```sh
codesign --verify --deep --strict build/release/PrismAU_artefacts/Release/AU/PRISM.component build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3 build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app
# exit 0
```

AU plist：`type=aufx`、`subtype=Prsm`、`manufacturer=Stgz`、`version=256`、`CFBundleIdentifier=com.stargazer.prism`、`CFBundleShortVersionString=0.1.0`。

### 3. 代码约束

```sh
grep -rnE '\b(new|delete|malloc|free)\b' src tests | grep -v 'ScopedNoDenormals' | head
```

只命中 3 条注释，无调用：`src/PluginEditor.h:14`、`src/Parameters.h:78`、`src/PluginProcessor.cpp:96`。

- 分配仅在构造/`prepare`/`rebuild`：`src/dsp/Filterbank.cpp:46-58,107-119`、`src/dsp/PrismDsp.cpp:62-70`。
- `PrismDsp::processBlock`（`src/dsp/PrismDsp.cpp:134-241`）、`Filterbank::processBlock`（`src/dsp/Filterbank.cpp:195-240`）、`OptoBand::processSample`（`src/dsp/OptoBand.cpp:54-75`）无分配；锁/IO grep 在 `src/` 无命中。
- `jassert` 共 14 处。两处 `jassertfalse` 有 release return：`src/dsp/PrismDsp.cpp:141-143`、`src/dsp/Filterbank.cpp:210-211`。其余 12 处是 prepare/设计期的参数前置条件，release 编译为空；音频线程关键路径不依赖它们。
- 限制：未做运行时分配器 hook。

### 4. 界面读数

`build/debug/editor-snapshot.png`：PNG 720×480、27924 字节。快照中 5 个读数：

- AMOUNT `37 %`
- SPECTRUM `+25 %`
- MIX `100 %`
- INPUT `3.7 dB`
- OUTPUT `0.0 dB`

dB 均 1 位小数，百分比均 0 位小数，SPECTRUM 正值带 `+`；频段标签为 `20–150 / 150–500 / 500–1.2k / 1.2–3k / 3–8k / 8–14k / 14k+`。

测试侧：`ParamTests.cpp:40-51` 断言 5 个参数 × 6 个值的精确字符串；`EditorTests.cpp:124-130` 断言真实编辑器读数为 `37 %`、`+25 %`、`3.7 dB`。

### 5. 文档一致性

| 检查项 | 结果 | 证据 |
|---|---|---|
| 分频点 | PASS | `src/dsp/FirDesign.h:13` = 150/500/1200/3000/8000/14000；`README.md:86-92`、`spec:81,170-176`、`plan:22,131` 一致 |
| 测试条数 | **FAIL** | 实际两套各 28 条；`README.md:10` 与 `README.md:105` 仍写 26 条 |
| 读数格式 | PASS | `README.md:80`、`spec:239`；快照与测试断言一致 |
| auval / pluginval 表述 | PASS（引用队长记录） | `README.md:11` 写 auval SUCCEEDED、pluginval 1.0.4 严格度 10；`README.md:117` 写 pluginval AU 模式限制；记录见本文件“t15 外部验证” |
| spec 分频/泄漏/150 Hz | PASS | `spec:125-132`；实测 −86.85 dB、−6.02 dB |
| plan 分频/150 Hz 验收 | PASS | `plan:22,131` |

### 6. 验收

| # | 条款 | 结果 | 证据 |
|---|---|---|---|
| 1 | debug/release 全量构建 exit 0，src/tests 0 warning | PASS | 各 146 目标；14 条 warning 全在 JUCE/vcpkg |
| 2 | 两套测试全过，关键数字齐全 | PASS | 各 28 条、0 失败；§1 |
| 3 | 三产物 arm64 thin、codesign、AU plist | PASS | §2 |
| 4 | 无裸分配；音频线程零分配；14 处 jassert | PASS | §3；限制：无运行时 hook |
| 5 | 界面读数 5 个文本格式 | PASS | §4 |
| 6 | 文档与当前行为一致 | **FAIL** | 唯一不一致：`README.md:10`、`README.md:105` 仍写 26 条，实际 28 条 |

**结论：FAIL。** 技术项（构建、28×2 测试、签名、代码约束、界面读数）全部通过；文档项有一处硬编码测试条数过期，不能给 pass。

残留/边界：

1. `README.md:10`、`README.md:105` 的“26 条”需改成“28 条”；改后重跑本条文档检查即可。
2. 压缩实测 `−5.46 dB` 而非 t14 的 `−6.38 dB`，原因是 1 kHz 落段变化；仍满足 >3 dB，但报告不应继续引用 −6.38。
3. auval/pluginval 结果来自队长 2026-11-12 21:41–21:45 的外部执行记录，verifier 未复跑；Standalone 启动、宿主加载与听感仍未做。
4. 未做运行时分配器 hook。

---

## t17b 复验（HEAD 89a504e，README 26→28 后）

- 任务：t17 第 2 次尝试，attempt `5c064923-144d-4093-98f1-a99eb9816613`
- HEAD：`89a504e70207dc5a21943e74b62af009e0a20d48`（`93bbf06` 之后仅 README 26→28）。
- 相对 `881d6d6` 的代码差异：只有 `README.md` 与上一段验证记录，`src/`、`tests/` 零差异；仍重新跑了 clean-first 构建与两套测试，裁决绑定本 HEAD。

### 1. 构建与测试

```sh
cmake --preset debug && cmake --build --preset debug --clean-first
# exit 0；146 目标；14 warning，全部在 JUCE/vcpkg

cmake --preset release -DPRISM_COPY_PLUGINS=OFF --fresh && cmake --build --preset release --clean-first
# exit 0；146 目标；14 warning，全部在 JUCE/vcpkg
```

- debug 的 14 条 warning：`fvariant.h` 2 条（VST3 SDK）、`juce_CGMetalLayerRenderer_mac.h` 12 条；release 同源。`src/`、`tests/` 0 warning。
- 两套 `PrismTests` 各 **28 条** `Starting tests`、0 `FAILED`、exit 0。
- 关键实测（debug 与 release 一致）：
  - 恒等：`1.19209e-7`（< 1e-6）。
  - 压缩：1 kHz、−6 dBFS，`−5.46 dB`；1 kHz 落段 3（500–1200），不是 t14 的段 4，故不等于 −6.38 dB；仍 >3 dB。
  - 冲激峰：`1152`，`1.000000`（Dsp / Filterbank / Processor 三条路径）。
  - 平滑：20 ms `0.993262`；最大相邻差 `0.065263`。
  - 段 1 @150 Hz：`−6.02 dB`。
  - 段 2 @40 Hz 泄漏：`−101.35 dB`。
  - 单段 −6 dB 带外：11 点最大泄漏 `−86.85 dB`（段 2 @75 Hz），偏差均 `±0.000 dB`。
  - 96 kHz：重建 `2.38e-7`，150 Hz `−5.98 dB`。
  - 11.025 kHz：重建 `2.24e-7`，延迟 1152，无 jassert。
  - 90% 恢复：`232.5 / 202.8 / 175.5 / 155.2 / 139.7 / 125.2 / 114.0 ms`。

### 2. 产物

| 产物 | 二进制 | codesign |
|---|---|---|
| `build/release/PrismAU_artefacts/Release/AU/PRISM.component` | `Contents/MacOS/PRISM`：Mach-O 64-bit bundle arm64 | exit 0 |
| `build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3` | `Contents/MacOS/PRISM`：Mach-O 64-bit bundle arm64 | exit 0 |
| `build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app` | `Contents/MacOS/PRISM`：Mach-O 64-bit executable arm64 | exit 0 |

```sh
codesign --verify --deep --strict build/release/PrismAU_artefacts/Release/AU/PRISM.component build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3 build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app
# exit 0
```

AU plist：`type=aufx`、`subtype=Prsm`、`manufacturer=Stgz`、`version=256`、`CFBundleIdentifier=com.stargazer.prism`、`CFBundleShortVersionString=0.1.0`。

### 3. 代码约束

```sh
grep -rnE '\b(new|delete|malloc|free)\b' src tests | grep -v 'ScopedNoDenormals' | head
```

只命中 3 条注释，无调用：`src/PluginEditor.h:14`、`src/Parameters.h:78`、`src/PluginProcessor.cpp:96`。分配只在构造/`prepare`/`rebuild`；`processBlock` 路径无分配、无锁、无 IO。`jassert` 共 14 处；`src/dsp/PrismDsp.cpp:141` 与 `src/dsp/Filterbank.cpp:210` 两处 `jassertfalse` 有 release return，其余为 prepare 期前置条件。限制：未做运行时分配器 hook。

### 4. 界面读数

`build/debug/editor-snapshot.png`：720×480、27924 字节。读数为 `37 %`、`+25 %`、`100 %`、`3.7 dB`、`0.0 dB`；dB 1 位小数、百分比整数、SPECTRUM 正值带 `+`。频段标签 `20–150 / 150–500 / 500–1.2k / 1.2–3k / 3–8k / 8–14k / 14k+`。

### 5. 文档一致性

| 检查项 | 结果 | 证据 |
|---|---|---|
| 测试条数 | PASS | `README.md:10`、`README.md:105` 已改为 28，与实际一致 |
| 分频点 | PASS | `README.md:86-92`、`spec:81,170-176`、`plan:22,131` 与 `src/dsp/FirDesign.h:13` 一致 |
| 读数格式 | PASS | `README.md:79`、`spec:239`；快照与测试一致 |
| auval/pluginval 表述 | PASS（引用队长记录） | `README.md:11` auval SUCCEEDED、pluginval 1.0.4 严格度 10；`README.md:117` AU 模式限制；本文件 t15 段 |
| spec/plan 分频与验收 | PASS | `spec:125-132,360`、`plan:22,131` |

### 6. 验收

| # | 条款 | 结果 | 证据 |
|---|---|---|---|
| 1 | debug/release 全量构建 exit 0，src/tests 0 warning | PASS | 各 146 目标；14 warning 全在 JUCE/vcpkg |
| 2 | 两套测试全过，关键数字齐全 | PASS | 各 28 条、0 失败；§1 |
| 3 | 三产物 arm64、codesign、AU plist | PASS | §2 |
| 4 | 无裸分配；音频线程零分配；14 处 jassert | PASS | §3；限制：无运行时 hook |
| 5 | 界面读数格式 | PASS | §4 |
| 6 | 文档与当前行为一致 | PASS | §5；README 26→28 已修 |

**结论：PASS。** 本裁决绑定 HEAD `89a504e`（代码与 `881d6d6` 相同，README 已修）。

残留/边界：

1. auval/pluginval 来自队长 2026-11-12 21:41–21:45 的外部记录，verifier 未复跑。
2. Standalone 启动、宿主加载与听感未做。
3. 未做运行时分配器 hook。
4. 压缩实测 `−5.46 dB`，不是 t14 的 `−6.38 dB`；原因是 1 kHz 落段 3。
