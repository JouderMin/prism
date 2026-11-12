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

#include "PrismDsp.h"

#include <algorithm>

namespace prism
{

namespace
{
// AMOUNT=100% 时相对标称驱动（t3 斜率测试点，0 dB）多推 18 dB。
// auto-gain 最多补 12 dB，而 OptoBand 在 1 kHz、−6 dBFS 下约压 16 dB，
// 因此净压缩约 4 dB；用 15 dB 时只剩 2.9 dB，达不到“>3 dB”的验收线。
constexpr float kMaxAmountDriveDb = 18.0f;

// SPECTRUM=±1 时，最低段与最高段的驱动差 ±6 dB。
constexpr float kSpectrumTiltDb = 6.0f;

// 参数平滑：一阶低通，时间常数 4 ms，5τ 正好 20 ms（约 99.3%）。
constexpr double kParamSmoothingSeconds = 0.004;

// auto-gain：500 ms 一阶低通跟踪每段压缩量。
constexpr double kAutoGainSeconds = 0.5;
constexpr float kAutoGainLimitDb = 12.0f;
constexpr float kAutoGainDeadbandDb = 0.1f;

constexpr float kMinLogGain = 1.0e-6f;
constexpr float kDriveClampDb = 60.0f;
} // namespace

PrismDsp::PrismDsp()
{
    const EngineParams defaults;

    amount_.snap (defaults.amount01);
    spectrum_.snap (defaults.spectrum);
    mix_.snap (defaults.mix01);
    inputDb_.snap (defaults.inputDb);
    outputDb_.snap (defaults.outputDb);

    for (auto& meter : meters_)
        meter.store (0.0f, std::memory_order_relaxed);
}

void PrismDsp::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    jassert (sampleRate > 0.0);
    jassert (maxBlockSize > 0);
    jassert (numChannels >= 1 && numChannels <= 2);

    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    numChannels_ = juce::jlimit (1, 2, numChannels);

    filterbank_.prepare (sampleRate_);
    jassert (filterbank_.latencySamples() == kLatencySamples);

    for (auto& opto : optos_)
        opto.prepare (sampleRate_);

    for (auto& band : bands_)
    {
        band.setSize (numChannels_, maxBlockSize_, false, true, true);
        band.clear();
    }

    inputGainBuffer_.setSize (numChannels_, maxBlockSize_, false, true, true);
    inputGainBuffer_.clear();

    for (auto& delay : dryDelay_)
        delay.assign (static_cast<size_t> (kDryDelaySamples), 0.0f);

    dryWrite_ = 0;

    for (auto& value : autoGainDb_)
        value = 0.0f;

    const auto autoGainSamples = kAutoGainSeconds * sampleRate_;
    autoGainCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / autoGainSamples));

    amount_.prepare (sampleRate_, kParamSmoothingSeconds);
    spectrum_.prepare (sampleRate_, kParamSmoothingSeconds);
    mix_.prepare (sampleRate_, kParamSmoothingSeconds);
    inputDb_.prepare (sampleRate_, kParamSmoothingSeconds);
    outputDb_.prepare (sampleRate_, kParamSmoothingSeconds);

    reset();
}

void PrismDsp::reset()
{
    filterbank_.reset();

    for (auto& opto : optos_)
        opto.reset();

    for (auto& band : bands_)
        band.clear();

    inputGainBuffer_.clear();

    for (auto& delay : dryDelay_)
        std::fill (delay.begin(), delay.end(), 0.0f);

    dryWrite_ = 0;

    for (auto& value : autoGainDb_)
        value = 0.0f;

    for (auto& meter : meters_)
        meter.store (0.0f, std::memory_order_relaxed);

    amount_.snapToTarget();
    spectrum_.snapToTarget();
    mix_.snapToTarget();
    inputDb_.snapToTarget();
    outputDb_.snapToTarget();
}

void PrismDsp::setParams (const EngineParams& params) noexcept
{
    targets_.amount01 = juce::jlimit (0.0f, 1.0f, params.amount01);
    targets_.spectrum = juce::jlimit (-1.0f, 1.0f, params.spectrum);
    targets_.mix01 = juce::jlimit (0.0f, 1.0f, params.mix01);
    targets_.inputDb = juce::jlimit (-12.0f, 24.0f, params.inputDb);
    targets_.outputDb = juce::jlimit (-24.0f, 12.0f, params.outputDb);

    amount_.setTarget (targets_.amount01);
    spectrum_.setTarget (targets_.spectrum);
    mix_.setTarget (targets_.mix01);
    inputDb_.setTarget (targets_.inputDb);
    outputDb_.setTarget (targets_.outputDb);
}

void PrismDsp::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);

    if (numSamples <= 0 || numChannels <= 0 || numSamples > maxBlockSize_)
    {
        jassertfalse; // 块长在 prepare 之后不应变化。
        return;
    }

    // 第一遍：输入增益逐样本平滑，写入预分配缓冲，同时作为干声源。
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = decibelsToGain (inputDb_.next());

        for (int ch = 0; ch < numChannels; ++ch)
            inputGainBuffer_.getWritePointer (ch)[i] = buffer.getReadPointer (ch)[i] * gain;
    }

    // 分频器要求一块完整输入；用外部指针视图避免拷贝与分配。
    juce::AudioBuffer<float> inputView (inputGainBuffer_.getArrayOfWritePointers(),
                                        numChannels, numSamples);
    filterbank_.processBlock (inputView, bands_);

    // 第二遍：检测、光电曲线、auto-gain、求和、干湿、输出增益。
    for (int i = 0; i < numSamples; ++i)
    {
        const auto amount = amount_.next();
        const auto spectrum = spectrum_.next();
        const auto mix = mix_.next();
        const auto outputGain = decibelsToGain (outputDb_.next());

        // AMOUNT 直接缩放光强；0 时旁路光电段，保证恒等。
        const auto baseDriveDb = amount > 0.0f
                                     ? 20.0f * std::log10 (amount) + kMaxAmountDriveDb
                                     : -kDriveClampDb;

        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        for (int b = 0; b < kNumBands; ++b)
        {
            const auto tilt = spectrum * kSpectrumTiltDb
                              * (static_cast<float> (b) - 3.0f) / 3.0f;

            optos_[static_cast<size_t> (b)].setDrive (
                juce::jlimit (-kDriveClampDb, kDriveClampDb, baseDriveDb + tilt));

            const auto left = bands_[static_cast<size_t> (b)].getReadPointer (0)[i];
            const auto right = numChannels > 1
                                   ? bands_[static_cast<size_t> (b)].getReadPointer (1)[i]
                                   : left;

            // 检测取左右较大值，两侧共用同一条增益曲线。
            const auto detector = juce::jmax (std::abs (left), std::abs (right));

            optos_[static_cast<size_t> (b)].processSample (detector);

            auto optoGain = optos_[static_cast<size_t> (b)].currentGain();

            if (amount <= 0.0f)
                optoGain = 1.0f;

            const auto gainDb = 20.0f * std::log10 (juce::jmax (optoGain, kMinLogGain));

            auto& averageDb = autoGainDb_[static_cast<size_t> (b)];
            averageDb += autoGainCoefficient_ * (gainDb - averageDb);

            // 反向补偿：不超过当前压缩量，也不超过 ±12 dB；均值 <0.1 dB 不补偿。
            auto compensationDb = juce::jmin (-averageDb, -gainDb);
            compensationDb = juce::jlimit (0.0f, kAutoGainLimitDb, compensationDb);

            if (compensationDb < kAutoGainDeadbandDb)
                compensationDb = 0.0f;

            const auto totalGain = optoGain * decibelsToGain (compensationDb);

            wetLeft += left * totalGain;
            wetRight += right * totalGain;
        }

        // 干声延时线：与分频器同样的 1152 样本延迟，保证 MIX 相位对齐。
        const auto dryLeft = dryDelay_[0][static_cast<size_t> (dryWrite_)];
        dryDelay_[0][static_cast<size_t> (dryWrite_)] = inputGainBuffer_.getReadPointer (0)[i];

        auto dryRight = dryLeft;

        if (numChannels > 1)
        {
            dryRight = dryDelay_[1][static_cast<size_t> (dryWrite_)];
            dryDelay_[1][static_cast<size_t> (dryWrite_)]
                = inputGainBuffer_.getReadPointer (1)[i];
        }

        dryWrite_ = (dryWrite_ + 1) % kDryDelaySamples;

        buffer.getWritePointer (0)[i] = (mix * wetLeft + (1.0f - mix) * dryLeft) * outputGain;

        if (numChannels > 1)
            buffer.getWritePointer (1)[i]
                = (mix * wetRight + (1.0f - mix) * dryRight) * outputGain;
    }

    for (int b = 0; b < kNumBands; ++b)
        meters_[static_cast<size_t> (b)].store (
            optos_[static_cast<size_t> (b)].currentLight(), std::memory_order_relaxed);
}

} // namespace prism
