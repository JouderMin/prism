// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Stargazer
//
// This file is part of PRISM.
//
// PRISM is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// PRISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
// more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with PRISM. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <memory>
#include <vector>

#if defined(PRISM_USE_PFFFT)
#include <pffft.h>
#else
#include <juce_dsp/juce_dsp.h>
#endif

namespace prism
{

/** 单精度实数 FFT 的小型后端接口。

    默认后端是 juce::dsp::FFT（macOS 走 vDSP，其他平台走 JUCE fallback）；
    配置 -DPRISM_FFT_BACKEND=pffft 时改用 vendored pffft。两条路径对
    Filterbank 暴露完全相同的频谱布局，即 JUCE 的完整复数谱：

      data[2k]、data[2k+1] = bin k 的实部、虚部（k = 0 … size−1）
      bin 0 为 DC（虚部为 0）
      bin size/2 为 Nyquist（虚部为 0）
      bin size−k = conj(bin k)

    data 须指向至少 2*size 个 float 的缓冲区，前 size 个 float 放时域
    输入；forwardReal 填满 2*size 个 float。inverseReal 读取完整谱，把
    归一化后的时域结果写回前 size 个 float。pffft 的 ordered 格式只含
    DC/Nyquist/bin 1..size/2−1，后端在内部展开成上述完整谱，因此调用方
    不需要区分后端。pffft 要求缓冲区 16 字节对齐；std::vector<float> 在
    本项目支持的 64 位平台上满足该要求，调试构建会断言。
*/
class RealFft
{
public:
    explicit RealFft (int size);
    ~RealFft();

    RealFft (const RealFft&) = delete;
    RealFft& operator= (const RealFft&) = delete;

    int size() const noexcept { return size_; }

    void forwardReal (float* data) noexcept;
    void inverseReal (float* data) noexcept;

private:
    int size_;

#if defined(PRISM_USE_PFFFT)
    struct SetupDeleter
    {
        void operator() (PFFFT_Setup* setup) const noexcept;
    };

    std::unique_ptr<PFFFT_Setup, SetupDeleter> setup_;
    std::vector<float> packed_;
    std::vector<float> work_;
#else
    juce::dsp::FFT fft_;
#endif
};

} // namespace prism
