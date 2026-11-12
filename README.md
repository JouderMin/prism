# PRISM

七段光电压缩器。信号拆成 7 个频段，每段用自己的颜色照一个光敏电阻，得到 7 条压缩曲线，再求和。

JUCE 8.0.7 + CMake + C++20。品牌 Stargazer。

## 当前状态

- 三个目标可构建：AU、VST3、Standalone。
- 28 条自动化测试通过。
- 当前版本通过 `auval -v aufx Prsm Stgz`（AU VALIDATION SUCCEEDED），VST3 通过 pluginval 1.0.4 严格度 10。
- 界面为 MVP：7 根光柱加 5 个旋钮。
- 未做：双时间常数释放、预设、A/B、侧链。

## 信号路径

```
输入 → 增益 → 线性相位分频（7 段）→ 每段前馈光电增益 → 求和 → 干湿混合 → 增益 → 输出
```

- 分频用 2049 抽头线性相位 FIR，Kaiser β=8。段 = `LP_k − LP_{k−1}`，求和恒为 1。
- 每段检测器 attack 10 ms、release 60 ms，前馈。光敏电阻 `R = R_dark / (1 + (d/d0)^γ)`。
- 每段 auto-gain 补偿长期漂移：500 ms，上限 ±12 dB，小于 0.1 dB 不补。
- 干声走同样延迟的环形缓冲，MIX 不产生梳状滤波。

## 延迟

1152 样本。48 kHz 下 24.0 ms，96 kHz 下 12.0 ms。插件向宿主报告延迟，宿主自动补偿。

## 构建

前置：Xcode 命令行工具、CMake 3.28 以上、Ninja、vcpkg，并装 juce 8.0.7。

```sh
cd ~/vcpkg && ./vcpkg install juce --triplet arm64-osx
cd <repo> && cmake --preset release
cmake --build --preset release
```

产物在 `build/release/Prism*_artefacts/Release/`。

插件的 post-build 拷贝默认关闭（`PRISM_COPY_PLUGINS=OFF`），因为沙箱不能写 `~/Library`。要拷贝就构建具体目标：

```sh
cmake --preset release -DPRISM_COPY_PLUGINS=ON
cmake --build --preset release --target PrismAU_AU
cmake --build --preset release --target PrismVST3_VST3
```

## 安装与验证

```sh
cp -R build/release/PrismAU_artefacts/Release/AU/PRISM.component ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar
auval -v aufx Prsm Stgz
cp -R build/release/PrismVST3_artefacts/Release/VST3/PRISM.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

不装插件直接试听：`build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app`。

## 测试

```sh
cmake --build --preset debug --target PrismTests
./build/debug/PrismTests
```

测试会顺带刷出 `build/debug/editor-snapshot.png`，可开图核对界面。

## 参数

| 名称 | 范围 | 默认 | 说明 |
|---|---|---|---|
| AMOUNT | 0–100 % | 50 | 驱动量，映射到光强，最大 18 dB |
| SPECTRUM | −100–+100 % | 0 | 光谱倾斜，负压低频段、正压高频段，±6 dB |
| MIX | 0–100 % | 100 | 干湿比 |
| INPUT | −12–+24 dB | 0 | 输入增益 |
| OUTPUT | −24–+12 dB | 0 | 输出增益 |

旋钮读数：dB 固定 1 位小数（0.1 精度），百分比固定 0 位小数；SPECTRUM 正值带 `+`。

## 频段

| 段 | 范围 | 颜色 | 波长 | γ | 目标斜率 |
|---|---|---|---|---|---|
| 1 | 20–150 Hz | 红 | 700 nm | 0.90 | 1.5:1 |
| 2 | 150–500 Hz | 橙 | 620 nm | 0.95 | 1.8:1 |
| 3 | 500–1.2 kHz | 黄 | 580 nm | 1.00 | 2.0:1 |
| 4 | 1.2–3 kHz | 绿 | 530 nm | 1.05 | 2.2:1 |
| 5 | 3–8 kHz | 青 | 500 nm | 1.10 | 2.5:1 |
| 6 | 8–14 kHz | 蓝 | 470 nm | 1.15 | 2.8:1 |
| 7 | 14 kHz 以上 | 紫 | 420 nm | 1.20 | 3.0:1 |

## 目录

```
src/dsp/FirDesign.*     窗函数法低通设计
src/dsp/Filterbank.*    overlap-save 分频与重建
src/dsp/BandTable.h     每段固定参数
src/dsp/OptoBand.*      单段光电增益
src/dsp/PrismDsp.*      整机引擎
src/Parameters.h        APVTS 参数与状态
src/PluginProcessor.*   处理器接线
src/PluginEditor.*      界面
tests/                  28 条单元测试与快照
```

## 文档

`docs/` 不入 git，只在本地保留：

- 设计：`docs/superpowers/specs/2026-11-12-prism-design.md`
- 计划：`docs/superpowers/plans/2026-11-12-prism.md`
- 验证记录：`docs/superpowers/verification/2026-11-12-prism-verification.md`

## 许可

PRISM 以 GNU Affero 通用公共许可证第 3 版（AGPL-3.0）发布，全文见 `LICENSE`。每个源码文件带 SPDX 头 `AGPL-3.0-or-later`。

选 AGPL-3.0 而不选 GPL-3.0，是因为 JUCE 8 的免费授权是 AGPLv3；没有商业授权时，分发基于 JUCE 8 的二进制必须按 AGPLv3。

若今后购买 JUCE 商业授权，可改为 GPL-3.0 或其他许可。第三方组件与各自许可见 JUCE 与 VST3 SDK 的 LICENSE。

## 已知限制

- 只支持 arm64 macOS，只支持立体声输入输出。
- pluginval 1.0.4 的 AU 模式在本机返回 0 个插件（第三方 AU 同样如此），AU 只能用 auval 验。
- 释放由检测器 60 ms 与光敏电阻一阶跟随叠加（段 1 为 80 ms，段 7 为 10 ms）。−6 → −18 dBFS 的 90% 恢复为 114.0–232.5 ms。双时间常数释放留到 M4。
- 界面没有电平刻度与增益衰减读数。
