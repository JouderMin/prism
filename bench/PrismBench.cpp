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

// PrismBench：DSP 基线计时。
//
// 测 48 kHz、立体声、块 128 的等效 10 s 音频，丢弃前 0.5 s。三段计时：
//   - filterbank：单独构造 Filterbank 并只调用 processBlock；
//   - opto+mix：PrismDsp::processBlock 与 Filterbank::processBlock 的差。
//     PrismDsp 的第二遍循环与第一遍、分频器写在同一个函数里，不改变 DSP
//     行为就无法单独切出，因此按计划采用差值口径；该差值同时包含第一遍
//     输入增益与调用开销。
//   - total：PrismDsp::processBlock 整块。
//
// 输入为固定种子白噪，参数取默认值；所有缓冲在计时前分配，热循环内不分配。
// 每段跑 3 轮，取每轮平均值中的最小值，以压低调度噪声。

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/Filterbank.h"
#include "dsp/PrismDsp.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;
constexpr int kNumChannels = 2;
constexpr double kMeasuredSeconds = 10.0;
constexpr double kDiscardSeconds = 0.5;
constexpr int kRounds = 3;

constexpr int kSamples = static_cast<int> (kMeasuredSeconds * kSampleRate);
constexpr int kTotalBlocks = kSamples / kBlockSize;
constexpr int kWarmupBlocks = static_cast<int> (kDiscardSeconds * kSampleRate / kBlockSize + 0.5);

static_assert (kTotalBlocks * kBlockSize == kSamples, "10 s 必须能被块长整除");
static_assert (kWarmupBlocks < kTotalBlocks, "丢弃段必须短于测量段");

using Clock = std::chrono::steady_clock;

double microsecondsPerBlock (double nanoseconds, int blocks)
{
    return blocks > 0 ? nanoseconds / static_cast<double> (blocks) / 1000.0 : 0.0;
}

void fillWhiteNoise (juce::AudioBuffer<float>& buffer)
{
    std::mt19937 rng (0x50524953u); // 固定种子，跨次运行可复现。
    std::uniform_real_distribution<float> distribution (-0.5f, 0.5f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, distribution (rng));
}

void copyBlock (const juce::AudioBuffer<float>& source, int blockIndex,
                juce::AudioBuffer<float>& destination)
{
    const auto start = blockIndex * kBlockSize;

    for (int ch = 0; ch < kNumChannels; ++ch)
        destination.copyFrom (ch, 0, source, ch, start, kBlockSize);
}

/** 只做分频，测 Filterbank::processBlock。 */
double measureFilterbank (const juce::AudioBuffer<float>& source)
{
    prism::Filterbank filterbank;
    filterbank.prepare (kSampleRate);

    std::array<juce::AudioBuffer<float>, prism::kNumBands> bands;

    for (auto& band : bands)
    {
        band.setSize (kNumChannels, kBlockSize, false, true, true);
        band.clear();
    }

    juce::AudioBuffer<float> work (kNumChannels, kBlockSize);
    auto best = std::numeric_limits<double>::max();

    for (int round = 0; round < kRounds; ++round)
    {
        filterbank.reset();

        double elapsedNanoseconds = 0.0;
        int measuredBlocks = 0;

        for (int block = 0; block < kTotalBlocks; ++block)
        {
            copyBlock (source, block, work);

            const auto start = Clock::now();
            filterbank.processBlock (work, bands);
            const auto end = Clock::now();

            if (block >= kWarmupBlocks)
            {
                elapsedNanoseconds += std::chrono::duration<double, std::nano> (end - start).count();
                ++measuredBlocks;
            }
        }

        best = std::min (best, microsecondsPerBlock (elapsedNanoseconds, measuredBlocks));
    }

    return best;
}

/** 整机，测 PrismDsp::processBlock（默认参数）。 */
double measureTotal (const juce::AudioBuffer<float>& source)
{
    prism::PrismDsp dsp;
    dsp.prepare (kSampleRate, kBlockSize, kNumChannels);
    dsp.setParams (prism::EngineParams {});

    juce::AudioBuffer<float> work (kNumChannels, kBlockSize);
    auto best = std::numeric_limits<double>::max();

    for (int round = 0; round < kRounds; ++round)
    {
        dsp.reset();

        double elapsedNanoseconds = 0.0;
        int measuredBlocks = 0;

        for (int block = 0; block < kTotalBlocks; ++block)
        {
            copyBlock (source, block, work);

            const auto start = Clock::now();
            dsp.processBlock (work);
            const auto end = Clock::now();

            if (block >= kWarmupBlocks)
            {
                elapsedNanoseconds += std::chrono::duration<double, std::nano> (end - start).count();
                ++measuredBlocks;
            }
        }

        best = std::min (best, microsecondsPerBlock (elapsedNanoseconds, measuredBlocks));
    }

    return best;
}
} // namespace

int main()
{
    juce::AudioBuffer<float> source (kNumChannels, kSamples);
    fillWhiteNoise (source);

    const auto filterbankUs = measureFilterbank (source);
    const auto totalUs = measureTotal (source);

    if (! (totalUs > 0.0))
    {
        std::fprintf (stderr, "PrismBench: 计时失败\n");
        return 1;
    }

    // 差值口径见文件头；单独计时不应超过整机，夹到 0 只防噪声导致的负值。
    const auto optoUs = std::max (0.0, totalUs - filterbankUs);
    const auto filterbankPercent = 100.0 * filterbankUs / totalUs;
    const auto optoPercent = 100.0 - filterbankPercent;

    std::printf ("%-12s%5.1f us/block (%.1f%%)\n", "filterbank:", filterbankUs, filterbankPercent);
    std::printf ("%-12s%5.1f us/block (%.1f%%)\n", "opto+mix:", optoUs, optoPercent);
    std::printf ("%-12s%5.1f us/block (100.0%%)\n", "total:", totalUs);

    return 0;
}
