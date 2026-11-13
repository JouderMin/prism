# pffft（vendored）

PRISM 的可选 FFT 后端。默认构建仍使用 JUCE；配置 `-DPRISM_FFT_BACKEND=pffft`
时编译本目录源码。

## 来源

- 上游：<https://github.com/marton78/pffft>
- Commit：`efc1dd2de6c3562fc464f905d37d9281f08a20dd`
- 许可：FFTPACK / BSD 风格，见同目录 `LICENSE.txt`（保留原始版权与许可声明）。

## 收录范围

只收录单精度实数 FFT 所需的文件：

- `include/pffft.h`
- `src/pffft.c`、`src/pffft_common.c`、`src/pffft_priv_impl.h`
- `src/simd/` 下的 float SIMD 分派与各架构后端头（x86 SSE1、arm64 NEON、
  scalar、wasm、Altivec、RVV、Helium）。

未收录 double、complex FFT、DSP 辅助模块、测试与构建脚本。

## 修改

未修改上游源码；文件内容与上述 commit 一致。PRISM 只在 `CMakeLists.txt`
中编译它们，并在 `src/dsp/RealFft.*` 外面包一层后端切换。
