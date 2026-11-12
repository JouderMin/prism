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

#include "Filterbank.h"

#include <algorithm>

namespace prism
{

namespace
{
int nextPowerOfTwo (int value) noexcept
{
    auto result = 1;

    while (result < value)
        result <<= 1;

    return result;
}
} // namespace

int Filterbank::fftSizeFor (int taps) noexcept
{
    // 重叠保留：FFT 长度 >= taps + 块长 − 1，且要求 2 的幂。
    return nextPowerOfTwo (taps + kBlockSize - 1);
}

int Filterbank::fftOrderFor (int fftSize) noexcept
{
    auto order = 0;

    while ((1 << order) < fftSize)
        ++order;

    return order;
}

Filterbank::Filterbank (int taps)
    : taps_ (taps),
      fftSize_ (fftSizeFor (taps)),
      fft_ (fftOrderFor (fftSize_))
{
    jassert (taps_ >= 3 && (taps_ % 2) == 1);
    jassert (fftSize_ >= taps_ + kBlockSize - 1);

    for (auto& spectrum : bandSpectra_)
        spectrum.assign (static_cast<size_t> (2 * fftSize_), 0.0f);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        history_[static_cast<size_t> (ch)].assign (static_cast<size_t> (fftSize_), 0.0f);
        pending_[static_cast<size_t> (ch)].assign (static_cast<size_t> (kBlockSize), 0.0f);

        for (auto& ring : rings_[static_cast<size_t> (ch)])
            ring.assign (static_cast<size_t> (kRingCapacity), 0.0f);
    }

    work_.assign (static_cast<size_t> (2 * fftSize_), 0.0f);
    product_.assign (static_cast<size_t> (2 * fftSize_), 0.0f);

    reset();
}

void Filterbank::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        std::fill (history_[static_cast<size_t> (ch)].begin(),
                   history_[static_cast<size_t> (ch)].end(), 0.0f);
        std::fill (pending_[static_cast<size_t> (ch)].begin(),
                   pending_[static_cast<size_t> (ch)].end(), 0.0f);
        pendingCount_[static_cast<size_t> (ch)] = 0;

        for (int b = 0; b < kNumOutputBands; ++b)
        {
            std::fill (rings_[static_cast<size_t> (ch)][static_cast<size_t> (b)].begin(),
                       rings_[static_cast<size_t> (ch)][static_cast<size_t> (b)].end(), 0.0f);
            ringRead_[static_cast<size_t> (ch)][static_cast<size_t> (b)] = 0;

            // 队列预置一个块的静音：输出在此基础上再加一个块延迟。
            ringWrite_[static_cast<size_t> (ch)][static_cast<size_t> (b)] = kBlockSize;
        }
    }
}

void Filterbank::prepare (double sampleRate)
{
    jassert (sampleRate > 0.0);

    sampleRate_ = sampleRate;

    // 低采样率保护：分频点不得超过 0.45·fs（低于 Nyquist 留 10% 余量）。
    // 例：11.025 kHz 下 14 kHz 会被夹到 4961.25 Hz；min 保持升序，
    // 相邻点重合时对应段系数为 0，6 段输出 + 重建段 7 仍恒等于输入。
    const auto maxCutoff = 0.45 * sampleRate;

    for (int k = 0; k < kNumCrossovers; ++k)
        crossovers_[static_cast<size_t> (k)] = std::min (kCrossoverPointsHz[k], maxCutoff);

    rebuild();
    reset();
}

void Filterbank::rebuild()
{
    jassert (sampleRate_ > 0.0);

    std::array<std::vector<float>, kNumOutputBands> coefficients;

    // LP_0 = 0。
    std::vector<float> previous (static_cast<size_t> (taps_), 0.0f);

    for (int k = 0; k < kNumOutputBands; ++k)
    {
        const auto lowpass = designLowpass (static_cast<float> (crossovers_[static_cast<size_t> (k)]),
                                            sampleRate_, taps_, kKaiserBeta);
        auto& band = coefficients[static_cast<size_t> (k)];
        band.resize (static_cast<size_t> (taps_));

        for (int n = 0; n < taps_; ++n)
            band[static_cast<size_t> (n)] = lowpass[static_cast<size_t> (n)]
                                          - previous[static_cast<size_t> (n)];

        previous = lowpass;
    }

    // 前 6 段的掩码之和为 LP_6；第 7 段（δ − LP_6）不再单独做 IFFT，
    // 由调用方从延迟输入减去这 6 段重建。

    // 预计算每段频谱：零填充到 FFT 长度后做实数前向变换。
    for (int b = 0; b < kNumOutputBands; ++b)
    {
        std::fill (work_.begin(), work_.end(), 0.0f);
        std::copy (coefficients[static_cast<size_t> (b)].begin(),
                   coefficients[static_cast<size_t> (b)].end(),
                   work_.begin());

        fft_.performRealOnlyForwardTransform (work_.data(), false);

        bandSpectra_[static_cast<size_t> (b)] = work_;
    }
}

void Filterbank::processHop (int channel)
{
    auto& history = history_[static_cast<size_t> (channel)];

    // 左移一个块，把刚收满的样本接到末尾，保持最近 fftSize 个输入。
    std::copy (history.begin() + kBlockSize, history.end(), history.begin());
    std::copy (pending_[static_cast<size_t> (channel)].begin(),
               pending_[static_cast<size_t> (channel)].end(),
               history.end() - kBlockSize);

    std::fill (work_.begin(), work_.end(), 0.0f);
    std::copy (history.begin(), history.end(), work_.begin());

    fft_.performRealOnlyForwardTransform (work_.data(), false);

    for (int b = 0; b < kNumOutputBands; ++b)
    {
        const auto& spectrum = bandSpectra_[static_cast<size_t> (b)];

        for (int i = 0; i < fftSize_; ++i)
        {
            const auto ar = work_[static_cast<size_t> (2 * i)];
            const auto ai = work_[static_cast<size_t> (2 * i + 1)];
            const auto br = spectrum[static_cast<size_t> (2 * i)];
            const auto bi = spectrum[static_cast<size_t> (2 * i + 1)];

            product_[static_cast<size_t> (2 * i)]     = ar * br - ai * bi;
            product_[static_cast<size_t> (2 * i + 1)] = ar * bi + ai * br;
        }

        fft_.performRealOnlyInverseTransform (product_.data());

        // 重叠保留：FFT 尾部的块长样本为有效线性卷积结果。
        auto& ring = rings_[static_cast<size_t> (channel)][static_cast<size_t> (b)];
        auto write = ringWrite_[static_cast<size_t> (channel)][static_cast<size_t> (b)];

        for (int i = 0; i < kBlockSize; ++i)
        {
            ring[static_cast<size_t> (write)]
                = product_[static_cast<size_t> (fftSize_ - kBlockSize + i)];
            write = (write + 1) & (kRingCapacity - 1);
        }

        ringWrite_[static_cast<size_t> (channel)][static_cast<size_t> (b)] = write;
    }
}

void Filterbank::processBlock (const juce::AudioBuffer<float>& input,
                               std::array<juce::AudioBuffer<float>, kNumOutputBands>& bands)
{
    const auto numSamples = input.getNumSamples();
    const auto numChannels = juce::jmin (input.getNumChannels(), kMaxChannels);

    if (numSamples <= 0)
        return;

    jassert (sampleRate_ > 0.0);

    for (auto& band : bands)
    {
        if (band.getNumChannels() < numChannels || band.getNumSamples() < numSamples)
        {
            jassertfalse; // 段缓冲须由调用方在 prepare 阶段分配。
            return;
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& pending = pending_[static_cast<size_t> (ch)];
            pending[static_cast<size_t> (pendingCount_[static_cast<size_t> (ch)]++)]
                = input.getReadPointer (ch)[i];

            if (pendingCount_[static_cast<size_t> (ch)] == kBlockSize)
            {
                processHop (ch);
                pendingCount_[static_cast<size_t> (ch)] = 0;
            }

            for (int b = 0; b < kNumOutputBands; ++b)
            {
                auto& ring = rings_[static_cast<size_t> (ch)][static_cast<size_t> (b)];
                const auto read = ringRead_[static_cast<size_t> (ch)][static_cast<size_t> (b)];

                bands[static_cast<size_t> (b)].getWritePointer (ch)[i]
                    = ring[static_cast<size_t> (read)];

                ringRead_[static_cast<size_t> (ch)][static_cast<size_t> (b)]
                    = (read + 1) & (kRingCapacity - 1);
            }
        }
    }
}

} // namespace prism
