# PRISM 设计文档

日期：2026-11-12
状态：待批准（rev 3：CMake 构建、C++20、RAII）
品牌：Stargazer
插件名：PRISM

## 1. 目标

做一个音频插件，用 LA-2A 式光电侧链做压缩。

与 LA-2A 的区别：信号拆成 7 个频段，每段用不同颜色的光照自己的光敏电阻。
每段的攻击、释放、曲线形状都不同，最后混合，得到单一光敏电阻做不出来的质感。

首版目标是"能做出来、能听出光压特征"，之后再调音。

## 2. 非目标

- 不复刻 LA-2A 的具体电路或 T4 单元参数。
- 不做侧链输入、外部触发。
- 不做 AAX。
- 不做 Windows 与 Linux。首版只出 macOS arm64。
- 不做预设库、i18n、在线更新。
- 不做零延迟模式。

## 3. 已定决策

| 编号 | 决策 | 来源 |
|---|---|---|
| D1 | 固定 7 条曲线，像硬件特性；用户只调总控 | 用户 m00029 |
| D2 | 首版以"能做出来"为准，先出声再调参 | 用户 m00031 |
| D3 | 采用方案 A：并联 7 路，每路独立光电链路 | 用户 m00034 |
| D4 | 每路加 auto-gain | 用户 m00034 |
| D5 | 插件名 PRISM，Stargazer 是品牌名 | 用户 m00034 |
| D6 | 允许使用 vcpkg 获取依赖 | 用户 m00056 |
| D7 | 分频用线性相位 FIR，不用 IIR 减法 | 本文 5.2 数值验算 |
| D8 | 用户参数只有 5 个，无逐段增益 | D1 |
| D9 | 接受 1152 样本延迟（48 kHz 下 24 ms） | 用户 2026-11-12 |
| D10 | 构建改用 CMake + juce_add_plugin，不再用 xmake | 用户 m00192 |
| D11 | C++20；RAII，禁止裸指针加手动释放 | 用户 m00192 |

## 4. 方案比较

### A 并联分频（采用）

7 路分频，每路独立检测、光照、光敏电阻、增益，最后求和。

- 优点：每段曲线独立，卖点成立；调参只影响一段。
- 缺点：7 条链路，算力与代码量最大；分频必须精确重建。
- 算力：48 kHz 下用分块 FFT 卷积，7 段合计远低于单核预算。

### B 单一光敏电阻 + 7 段加权检测（未采用）

7 段包络加权求和，驱动同一个光敏电阻。

- 优点：最接近 LA-2A 的整体呼吸感，成本低。
- 缺点：只有一条曲线，"7 种颜色的光"变成 UI 装饰。

### C 串联二分树（未采用）

7 段用 6 个二分点串成树，每级独立压缩。

- 优点：级间负载作用产生复杂谐波。
- 缺点：相位累积难控，调试成本最高。

## 5. 架构

### 5.1 数据流

```
输入 → 输入增益 → 分频器（6 个线性相位低通，相减得 7 段，带 1024 样本延迟）
     → 每段：[检测器 → 光强 → 光敏电阻 → 增益] → auto-gain
     → 求和 → 干湿混合 → 输出增益 → 输出
```

延迟：分频的对称延迟 1024 样本加处理块 128 样本，共 1152 样本。
48 kHz 下 24.0 ms，96 kHz 下 12.0 ms。构建时用脉冲实测，再用 `setLatencySamples` 上报。

### 5.2 分频：线性相位 FIR 减法

6 个分频点：80、200、500、1200、3000、8000 Hz。
记 `LP_k` 为第 k 个低通的单位冲激响应，`LP_0 = 0`，`LP_7 = δ`（单位冲激）。

```
band_k = LP_k − LP_{k−1}        k = 1..7
```

求和恒等于输入：`Σ band_k = LP_7 − LP_0 = δ`。

多项式恒等成立，但要得到可用的频段，所有低通必须**相位一致**。
线性相位 FIR 满足这一条：各低通的群延迟相同，相减就是幅度的相减。

滤波器规格：

| 项 | 值 |
|---|---|
| 抽头数 | 2049 |
| 窗函数 | Kaiser，β = 8 |
| 类型 | 对称线性相位，组延迟 1024 样本 |
| 分频点处幅度 | −6 dB |

#### 为什么不用 IIR 减法

用 48 kHz、LR4（两级 RBJ 二阶，Q = 1/√2）验算，减法式 IIR 的频段并不干净：

- 段 2 在 40 Hz 只低 1.12 dB，应有约 −24 dB。低频几乎整体漏进相邻段。
- 段 7 在 6 kHz 为 +4.18 dB，真实 LR4 高通此处应为 −6.2 dB。
- 单段压 6 dB 时，段 7 带内最低到 −12.0 dB，形状不可控。

原因：不同截止频率的 IIR 低通在低频处相位差很大，相减得到的不是带通。

#### 为什么不用 LR4 二分树

LR4 二分树频段干净（段 2 在 40 Hz 为 −24.6 dB），但求和会塌：

- 全部段增益为 1 时，求和幅度在 −0.45 至 −2.60 dB 之间，944 Hz 最深处 −2.60 dB。
- 级联的 allpass 相位使各段在重叠区部分抵消。

结果是固定的中频凹陷，与压缩量无关，任何静态 EQ 都补不回来。

#### 抽头数与泄漏实测

48 kHz，Kaiser β = 8，测量各段在自身频段两侧一个倍频程外的最大泄漏：

| 抽头数 | 延迟 | 段外泄漏 | 段 1 在 80 Hz |
|---|---|---|---|
| 1025 | 10.7 ms | −17.8 dB | −5.61 dB |
| 2049 | 21.3 ms | −33.3 dB | −6.02 dB |
| 4097 | 42.7 ms | −76.0 dB | −6.02 dB |
| 8193 | 85.3 ms | −84.7 dB | −6.03 dB |

采用 2049：−33 dB 泄漏在压缩用途上听不出，延迟可接受。
若 M2 测得泄漏影响听感，改 4097，延迟翻倍，其余不变。

7 段求和与输入的差为 0.00 dB（7 组系数浮点运算后）。

### 5.3 每段光电链路

四层，参数按段固定，写在常量表里。

1. **检测器**：带内输入整流后一阶包络，attack 10 ms，release 60 ms，固定不随段变化。
   - 检测取**带内输入**（前馈），不取压缩后的信号。
2. **光强**：`d = A · 10^(s_b/20) · (env/ref)^1.6`，`ref = −18 dBFS`。
   - `A = 10^(AMOUNT/20)`，再乘 SPECTRUM 的倾斜因子。
   - `s_b` 是每段灵敏度补偿，按段固定，见 5.4。乘在光强上，不乘包络。
   - 指数 1.6 模拟 EL 面板陡峭的亮度曲线。
4. **光敏电阻**：`R = R_dark / (1 + (d/d0)^γ)`，按段取 γ 与 τ_ldr。
   - `R_dark = 150 kΩ`，`d0 = 1`。
   - `R` 以 τ_ldr 一阶跟随目标值，形成"记忆"。
5. **增益**：光敏电阻作串联元件，`R_s = 10 kΩ` 对地，`g = (R_s + R) / (R_s + R_dark)`，夹到 `[10k/160k, 1]`。
   - 暗态 `R = R_dark` 时 `g = 1`；全亮 `R → 0` 时 `g = 10k/160k = −24.1 dB`，即最大压缩量。
   - 夹子必须在实现里显式写出，否则静音输入会出现 NaN。

**为什么改成前馈（2026-11-12 实测）**：原稿写 `g = R_s/(R_s+R)` 且检测取 `x·g[n−1]`。这个方向是反的——光强上升使 `R` 下降、`g` 上升，结果是膨胀。改成 `(R_s+R)/(R_s+R_dark)` 后方向正确，但反馈环把静态斜率锁在 1.0–1.15:1：实测 7 段全部约 0.96:1，输出恒在 −45 dBFS 附近。前馈加每段灵敏度 trim 后，实测斜率 1.50/1.80/2.00/2.20/2.50/2.80/3.00，逐段误差 0.00。

代价：反馈式特有的节目相关行为减少，"呼吸"只剩光敏电阻的时间常数与非线性。首版接受，记入 §11。

目标静态斜率（输入 dB 变化比输出 dB 变化）：段 1 约 1.5:1，段 7 约 3:1。
首版按表调 τ_ldr 与 γ 逼近，实测表见 9.1。

### 5.4 光色与时间常数

颜色不是装饰。短波长响应快、记忆短；长波长响应慢、记忆长。
首版按此排序，不追求物理精确。

| 段 | 频段 (Hz) | 颜色 | 波长 (nm) | τ_ldr (ms) | 释放快段 (ms) | 释放尾段 (ms) | γ | `s_b` (dB) | 目标斜率 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | < 80 | 红 | 700 | 80 | 120 | 2500 | 0.9 | −12.1 | 1.5:1 |
| 2 | 80–200 | 橙 | 620 | 65 | 100 | 1800 | 0.95 | −6.6 | 1.8:1 |
| 3 | 200–500 | 黄 | 580 | 50 | 90 | 1400 | 1.0 | −4.5 | 2.0:1 |
| 4 | 500–1200 | 绿 | 530 | 38 | 75 | 1000 | 1.05 | −2.9 | 2.2:1 |
| 5 | 1200–3000 | 青 | 500 | 28 | 60 | 700 | 1.1 | −0.7 | 2.5:1 |
| 6 | 3000–8000 | 蓝 | 470 | 18 | 45 | 500 | 1.15 | +1.1 | 2.8:1 |
| 7 | > 8000 | 紫 | 420 | 10 | 30 | 350 | 1.2 | +1.6 | 3.0:1 |

`s_b` 是各段灵敏度补偿：低频段在混合后占比大，需要更低的驱动量才能落在目标斜率上。

"释放快段/尾段"是实测目标，不是独立时间常数。
它们由检测器 release、τ_ldr 与 R(d) 的非线性共同产生，M2 用阶跃响应测出并记录实际值。

### 5.5 auto-gain

每段算 500 ms 一阶低通的 dB 衰减均值，再反向补偿，限幅 ±12 dB。

- 作用：修长期频谱倾斜。
- 副作用：极慢的压缩量会被补掉一部分。
- 保护：均值小于 0.1 dB 时不补偿，避免 0/0。
- 判定：若听感损失过大，把时间常数拉到 1 s 以上，或把补偿量限到 ±6 dB。

### 5.6 采样率与过采样

首版不加过采样。
理由：压缩是慢变增益，谐波失真风险低；先验证质感，需要时再加 2×。

采样率变化：重建 FIR 系数表与全部时间常数换算；FIR 设计与采样率无关，只按采样率缩放截止频率。

### 5.7 实现约定

- 总线：只支持立体声进、立体声出，`isBusesLayoutSupported` 拒绝其他布局。
- 参数平滑：AMOUNT、SPECTRUM、MIX、INPUT、OUTPUT 用 20 ms。
- 立体声：检测取左右声道较大值，两侧共用同一条增益曲线，不做独立声像。
- 状态保存：`AudioProcessorValueTreeState`，存 5 个参数，状态版本号 1。
- 延迟：`setLatencySamples(1152)`，数值与 5.1 一致。
- 电平表：每段光强写入 `std::atomic<float>`，界面 30 Hz 定时读取，音频线程不碰锁。
- 极小信号：`juce::ScopedNoDenormals`，光强加 1e−12 下限。
- 插件标识：厂商码 `Stgz`，AU type `aufx`，AU subtype `Prsm`，bundle id `com.stargazer.prism`，版本 0.1.0。
- 这些四字符码与 bundle id 首版定死，之后不再改。

## 6. 参数

| 参数 | 范围 | 默认 | 说明 |
|---|---|---|---|
| AMOUNT | 0–100 % | 50 | 总光强驱动，7 段同比缩放 |
| SPECTRUM | −100–+100 | 0 | 光谱倾斜：负值压低段驱动，正值压高段 |
| MIX | 0–100 % | 100 | 干湿混合 |
| INPUT | −12–+24 dB | 0 | 输入增益 |
| OUTPUT | −24–+12 dB | 0 | 输出增益 |

没有任何参数会改变曲线形状。曲线是硬件，参数只改驱动量与配比。
这条约束保证"7 种光"不会被调成 7 条同样的曲线。
逐段增益作为编译期常量存在，不进参数表（D8）。

## 7. 界面

首版只求能用，美术后做。

- 顶部：Stargazer 标志（`Assets/logo.svg` 转 PNG 或路径绘制）。
- 中部：7 根竖直光柱，颜色对应频段，亮度实时反映该段光强。
  光柱兼作电平表，压缩越深越暗。
- 下部：AMOUNT、SPECTRUM、MIX、INPUT、OUTPUT 五个旋钮。
- 深色背景，尺寸 720 × 480，可缩放。

## 8. 构建

### 8.1 依赖与工具链

JUCE 8.0.7，用 vcpkg 提供：

```sh
vcpkg install juce          # 本机安装树尚无 juce，现有 asio、mimalloc、vst3sdk
```

CMake 通过 vcpkg toolchain 使用：

```sh
cmake --preset default      # 内含 -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
```

vcpkg port 提供 `find_package(JUCE CONFIG)` 与宿主工具 juceaide，`juce_add_plugin` 可直接用。

兜底：若 port 与 Xcode 27 冲突，改 `third_party/JUCE` 子模块 + `add_subdirectory`，`CMakeLists.txt` 其余不变。

### 8.2 工程结构

```
CMakeLists.txt            顶层：C++20、警告、vcpkg、三个目标
CMakePresets.json         debug / release 预设
src/                      插件源码
src/dsp/                  DSP
src/ui/                   界面
third_party/              仅兜底时使用
```

三个目标全部由 `juce_add_plugin` 生成：

```cmake
juce_add_plugin(PrismAU
    FORMATS AU
    ...)

juce_add_plugin(PrismVST3
    FORMATS VST3
    ...)

juce_add_plugin(PrismStandalone
    FORMATS Standalone
    ...)
```

三个目标共用同一份源码与 `JucePlugin_*` 参数，只是 `FORMATS` 不同。
`juce_add_plugin` 自动生成 bundle 目录、三套 `Info.plist`（含 AU 的 `AudioComponents`）、`Contents/PkgInfo`，并编译对应 wrapper。

### 8.3 `juce_add_plugin` 关键参数

| 参数 | 值 |
|---|---|
| `COMPANY_NAME` | Stargazer |
| `BUNDLE_ID` | com.stargazer.prism |
| `PLUGIN_MANUFACTURER_CODE` | Stgz |
| `PLUGIN_CODE` | Prsm |
| `PRODUCT_NAME` | PRISM |
| `VERSION` | 0.1.0 |
| `IS_SYNTH` | FALSE |
| `NEEDS_MIDI_INPUT` / `NEEDS_MIDI_OUTPUT` | FALSE |
| `EDITOR_WANTS_KEYBOARD_FOCUS` | FALSE |
| `COPY_PLUGIN_AFTER_BUILD` | TRUE（AU 进 `~/Library/Audio/Plug-Ins/Components`，VST3 进 `VST3`） |
| `MICROPHONE_PERMISSION_ENABLED` | TRUE，仅 Standalone |

`juce_add_plugin` 会自动完成 AU 的 `aufx`/`Prsm`/`Stgz` 三元组、plist、bundle 布局与 JUCE 模块编译，这些都不需要自定义规则。

### 8.4 编译与签名

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

target_compile_options(PrismCommon INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
```

- 只在 Release 用 `-O3`；Debug 不开 `-Werror`，避免 JUCE 头文件的告警打断构建。
- `JUCE_WEB_BROWSER=0`、`JUCE_USE_CURL=0`，减少无关依赖。
- 签名：给三个目标各加一条 post-build 命令，`codesign --force -s -` 做 ad-hoc 签名；AU 需要 `--deep`。

### 8.5 语言与内存约定

C++20，RAII，禁止裸指针加手动释放。

- 禁止 `new`、`delete`、`malloc`、`free`。多态对象用 `std::unique_ptr`，容器用 `std::vector`、`std::array`，JUCE 对象按值持有或用 `juce::OwnedArray`。
- 返回值用 `[[nodiscard]]`；所有权转移用 `std::unique_ptr` 或 `std::move`，不传裸指针。
- 资源一律 RAII 封装：文件用 `juce::File::createInputStream` 返回的智能指针，锁用 `juce::ScopedLock` 或 `std::lock_guard`，音频线程跨线程通信用 `std::atomic`。
- 音频线程零分配：所有 buffer 在 `prepareToPlay` 里分配好；`processBlock` 内只用栈与预分配内存，不碰 `std::mutex`、不做文件 I/O。
- C++20 用法范围：`concepts` 约束 DSP 接口，`constexpr` 常量表（分频系数、光色参数），designated initializers 写参数定义，`std::span` 传样本视图，`<numbers>` 取 π。
- 不用 modules，不用 coroutines。
- 音频回调标 `noexcept`，异常不许穿过回调边界。

### 8.6 已知风险

- R1：vcpkg 的 juce 8.0.7 与 Xcode 27 的兼容性。失败就退 `third_party/JUCE` 子模块，配置不变。
- R2：ad-hoc 签名在本机够用，分发另说。Gatekeeper 不在首版范围。
- R3：`COPY_PLUGIN_AFTER_BUILD` 会写 `~/Library/Audio/Plug-Ins`，沙箱下需要批准；也可关掉后手动拷贝做验证。

### 8.7 spike 检查清单

M1 只做一个透传插件，验证下面 6 条，全部通过才进入 DSP。

1. `cmake --preset release` 加 `cmake --build` 一次成功，三个目标产物齐全，无手工步骤。
2. `auval -v aufx Prsm Stgz` 能列出并验证该 AU。
3. VST3 bundle 被 `pluginval` 识别。
4. Standalone 启动，能选音频设备。
5. 三个产物都能在宿主里出声。
6. `codesign --verify --deep` 通过。

## 9. 验证

### 9.1 自动化

- 分频恒等：7 段增益固定为 1，白噪过一遍，输出与输入之差低于 −120 dBFS。验算值为 0.00 dB。
- 延迟：脉冲测群延迟，等于 1152 样本。
- 单段压缩重建：只压一段 6 dB，测该段两侧一个倍频程外的偏差，应低于 0.5 dB。验算值为 0.00 dB。
- 静态曲线：逐段扫电平，实测斜率与 5.4 的目标值相差不超过 0.5。
- 释放曲线：−18 dBFS 到 −6 dBFS 的阶跃，记录 90% 恢复时间，快段与尾段分别记录。dB 域线性度取恢复量的 20%–80% 拟合，偏差 < 1 dB；按 10%–90% 全区间拟合会到 1.6 dB，属双时间常数正常形状，不作为失败项。
- 无输入：静音输入，输出必须为静音，不得出 NaN。

### 9.2 手动

- AU：`auval -v aufx Prsm Stgz` 全部通过。
- VST3：`pluginval --validate` 严格度 5 以上通过（若本机可装）。
- Standalone：双击能启动，能选设备并出声。
- Logic 或 Reaper 加载，确认能出声、参数能自动化、延迟补偿生效。
- 听感：鼓组、人声、贝斯各一遍，对比关闭压缩的干声。

## 10. 里程碑

| 里程碑 | 内容 | 完成标准 |
|---|---|---|
| M1 | 工具链 spike | 三个目标产物生成，宿主能看到并出声 |
| M2 | FIR 分频 + 单段链路 | 单段压缩可听，恒等测试与延迟测试通过 |
| M3 | 7 段 + 求和 | 7 段同时工作，参数可用 |
| M4 | auto-gain + 调音 | 长期频谱不塌，7 段性格可区分 |
| M5 | 界面 + 验证 | 界面可用，auval 与 pluginval 通过 |

## 11. 开放问题

- Q1：auto-gain 的补偿强度与时间常数需要听感验证，可能需要 2–3 轮。
- Q2：是否需要过采样，取决于 M4 的失真测量。
- Q3：JUCE 来源已定：vcpkg 8.0.7（M1 实测通过）。
- Q4：前馈代替反馈后，节目相关的"呼吸"变少。若听感偏干，M4 之后再试"前馈 + 光敏电阻滞回"或"混合拓扑"（反馈只用于段 1–2 的慢速成分）。
