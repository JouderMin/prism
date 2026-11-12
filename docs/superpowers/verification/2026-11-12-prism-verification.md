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
