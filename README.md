# PRISM

七段光电压缩器，Stargazer 出品。

信号拆成 7 个频段，每段用自己的颜色照一个光敏电阻：低频红、高频紫。7 条压缩曲线形状不同，求和后得到一种会随频谱变化的呼吸感。

JUCE 8.0.7 + CMake + C++20。当前提供 AU、VST3、Standalone 三种目标，macOS arm64，仅立体声。

以 GNU Affero 通用公共许可证第 3 版（AGPL-3.0）发布，全文见 `LICENSE`。

## 工作原理

```
输入 → 增益 → 线性相位分频（7 段）→ 每段光电增益 → 求和 → 干湿混合 → 增益 → 输出
```

- 分频用 2049 抽头线性相位 FIR（Kaiser β=8）。第 k 段取 `LP_k − LP_{k−1}`，7 段求和恒为 1，重建误差约 1e-7。
- 每段检测器 attack 10 ms、release 60 ms，前馈。光敏电阻按 `R = R_dark / (1 + (d/d0)^γ)` 变化，光强 `d` 由 AMOUNT 与 SPECTRUM 决定。
- 每段的 γ、τ、目标斜率固定，像硬件。用户只调总控。
- 每段有 auto-gain 补偿长期漂移：500 ms，上限 ±12 dB。
- 全链路延迟 1152 样本，48 kHz 下 24.0 ms。插件向宿主报告，宿主自动补偿。干声走同样延迟的缓冲，MIX 不产生梳状滤波。

## 参数

| 名称 | 范围 | 默认 | 作用 |
|---|---|---|---|
| AMOUNT | 0–100 % | 50 | 驱动量，映射到光强，最大 18 dB |
| SPECTRUM | −100–+100 % | 0 | 光谱倾斜，负值压低频段、正值压高频段，±6 dB |
| MIX | 0–100 % | 100 | 干湿比 |
| INPUT | −12–+24 dB | 0 | 输入增益 |
| OUTPUT | −24–+12 dB | 0 | 输出增益 |

旋钮读数：dB 一位小数，百分比整数，SPECTRUM 正值带 `+`。

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

## 当前状态

AU、VST3、Standalone 均可构建并通过验证：`auval -v aufx Prsm Stgz` 为 AU VALIDATION SUCCEEDED，VST3 在 pluginval 1.0.4 严格度 10 下为 SUCCESS，28 条自动化测试通过。

界面是 MVP：7 根光柱加 5 个旋钮。

未做：双时间常数释放、预设、A/B、侧链、电平刻度。

## 贡献方式

### 环境

macOS arm64，Xcode 命令行工具，CMake 3.28 以上，Ninja，vcpkg。

```sh
cd ~/vcpkg && ./vcpkg install juce --triplet arm64-osx
```

### 构建

```sh
cmake --preset debug
cmake --build --preset debug
```

release 把 preset 换成 `release`。产物在 `build/<preset>/Prism*_artefacts/Release/`。

post-build 拷贝默认关闭（`PRISM_COPY_PLUGINS=OFF`），避免写 `~/Library`。要装到本机就单独构建目标：

```sh
cmake --preset release -DPRISM_COPY_PLUGINS=ON
cmake --build --preset release --target PrismAU_AU
cmake --build --preset release --target PrismVST3_VST3
```

### 测试

```sh
cmake --build --preset debug --target PrismTests
./build/debug/PrismTests
```

测试会刷新 `build/debug/editor-snapshot.png`，可开图核对界面。提交前要求 debug 与 release 各构建一次、测试全过、`src/` 与 `tests/` 无新警告。

GitHub Actions 在 macOS arm64 与 Windows x64 上构建并跑测试，同时把编译好的 AU、VST3、Standalone 作为运行产物上传，可从对应运行的 Artifacts 下载。

### 安装验证

```sh
cp -R build/release/PrismAU_artefacts/Release/AU/PRISM.component ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar
auval -v aufx Prsm Stgz
```

不装插件可以直接试听：`build/release/PrismStandalone_artefacts/Release/Standalone/PRISM.app`。
