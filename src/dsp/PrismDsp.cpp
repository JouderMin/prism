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

// 参数平滑：一阶低通，时间常数 4 ms，5τ 正好 20 ms（约 99.3%）。PUSH 与输入/输出共用。
constexpr double kParamSmoothingSeconds = 0.004;

// auto-gain：500 ms 一阶低通跟踪每段压缩量；dB 均值每 32 样本更新，
// 样本间对补偿增益线性插值，避免逐样本 log10/pow。
constexpr double kAutoGainSeconds = 0.5;
constexpr int kAutoGainUpdateSamples = 32;
constexpr float kAutoGainLimitDb = 12.0f;
constexpr float kAutoGainDeadbandDb = 0.1f;

constexpr float kMinLogGain = 1.0e-6f;
constexpr float kDriveClampDb = 60.0f;
constexpr float kDbPerLog2 = 6.020599913279624f; // 20·log10(2)
} // namespace

PrismDsp::PrismDsp()
{
    const EngineParams defaults;

    amount_.snap (defaults.amount01);
    spectrum_.snap (defaults.spectrum);
    mix_.snap (defaults.mix01);
    inputGain_.snap (1.0f);
    pushGain_.snap (1.0f);
    outputGain_.snap (1.0f);
    clipMix_.snap (0.0f);
    agcTargetDb_.snap (0.0f);

    for (auto& value : autoGainStart_)
        value = 1.0f;

    for (auto& value : autoGainTarget_)
        value = 1.0f;

    for (auto& value : autoGainCurrent_)
        value = 1.0f;

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

    for (auto& value : autoGainStart_)
        value = 1.0f;

    for (auto& value : autoGainTarget_)
        value = 1.0f;

    for (auto& value : autoGainCurrent_)
        value = 1.0f;

    autoGainPhase_ = 0;

    const auto autoGainSamples = kAutoGainSeconds * sampleRate_;
    autoGainCoefficient_ = static_cast<float> (
        1.0 - std::exp (-static_cast<double> (kAutoGainUpdateSamples) / autoGainSamples));

    amount_.prepare (sampleRate_, kParamSmoothingSeconds);
    spectrum_.prepare (sampleRate_, kParamSmoothingSeconds);
    mix_.prepare (sampleRate_, kParamSmoothingSeconds);
    inputGain_.prepare (sampleRate_, kParamSmoothingSeconds);
    pushGain_.prepare (sampleRate_, kParamSmoothingSeconds);
    outputGain_.prepare (sampleRate_, kParamSmoothingSeconds);
    clipMix_.prepare (sampleRate_, kParamSmoothingSeconds);
    agcTargetDb_.prepare (sampleRate_, kParamSmoothingSeconds);

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

    for (auto& value : autoGainStart_)
        value = 1.0f;

    for (auto& value : autoGainTarget_)
        value = 1.0f;

    for (auto& value : autoGainCurrent_)
        value = 1.0f;

    autoGainPhase_ = 0;

    for (auto& meter : meters_)
        meter.store (0.0f, std::memory_order_relaxed);

    amount_.snapToTarget();
    spectrum_.snapToTarget();
    mix_.snapToTarget();
    inputGain_.snapToTarget();
    pushGain_.snapToTarget();
    outputGain_.snapToTarget();
    clipMix_.snapToTarget();
    agcTargetDb_.snap (-20.0f * std::log10 (juce::jmax (pushGain_.current(), kMinLogGain)));
}

float PrismDsp::autoGainCompensationDb (float averageDb, float gainDb, float targetDb) noexcept
{
    auto compensationDb = juce::jmin (targetDb - averageDb, targetDb - gainDb);
    compensationDb = juce::jlimit (0.0f, kAutoGainLimitDb, compensationDb);

    if (compensationDb < kAutoGainDeadbandDb)
        compensationDb = 0.0f;

    return compensationDb;
}

void PrismDsp::setParams (const EngineParams& params) noexcept
{
    targets_.amount01 = juce::jlimit (0.0f, 1.0f, params.amount01);
    targets_.spectrum = juce::jlimit (-1.0f, 1.0f, params.spectrum);
    targets_.mix01 = juce::jlimit (0.0f, 1.0f, params.mix01);
    targets_.inputDb = juce::jlimit (-12.0f, 24.0f, params.inputDb);
    targets_.outputDb = juce::jlimit (-24.0f, 12.0f, params.outputDb);
    targets_.pushDb = juce::jlimit (0.0f, 24.0f, params.pushDb);
    targets_.clip = params.clip;
    targets_.delta = params.delta;

    amount_.setTarget (targets_.amount01);
    spectrum_.setTarget (targets_.spectrum);
    mix_.setTarget (targets_.mix01);
    inputGain_.setTarget (decibelsToGain (targets_.inputDb));
    pushGain_.setTarget (decibelsToGain (targets_.pushDb));
    clipMix_.setTarget (targets_.clip ? 1.0f : 0.0f);

    // PUSH 与 OUTPUT 共用 4 ms 平滑：输出级 = outputDb − pushDb。
    // PUSH 前置增益会被输出级等量削减，静态净响度不变。
    outputGain_.setTarget (decibelsToGain (targets_.outputDb - targets_.pushDb));
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

    // 第一遍：输入增益 → PUSH 增益 → 可选 tanh 软削波，逐样本平滑，
    // 写入预分配缓冲，同时作为干声源与分频器输入。
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = inputGain_.next() * pushGain_.next();
        const auto clipMix = clipMix_.next();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto pushed = buffer.getReadPointer (ch)[i] * gain;
            auto shaped = pushed;

            // 关闭时完全不进 tanh，保证与旧版本逐位一致；开启/关闭用 4 ms 线性交叉淡化。
            if (clipMix > 0.0f)
            {
                const auto clipped = std::tanh (pushed);
                shaped = clipMix >= 1.0f ? clipped
                                         : pushed + clipMix * (clipped - pushed);
            }

            inputGainBuffer_.getWritePointer (ch)[i] = shaped;
        }
    }

    // 分频器要求一块完整输入；用外部指针视图避免拷贝与分配。
    juce::AudioBuffer<float> inputView (inputGainBuffer_.getArrayOfWritePointers(),
                                        numChannels, numSamples);
    filterbank_.processBlock (inputView, bands_);

    // PUSH 的 AGC 目标随平滑后的 PUSH 增益下降：PUSH 造成的衰减是“应有”的，
    // auto-gain 只补偿超出该基线的长期偏低，不再把 PUSH 的压缩补回来。
    agcTargetDb_.setTarget (-20.0f * std::log10 (juce::jmax (pushGain_.current(), kMinLogGain)));

    // 第二遍：检测、光电曲线、auto-gain、求和、干湿、输出增益。
    for (int i = 0; i < numSamples; ++i)
    {
        const auto amount = amount_.next();
        const auto agcTargetDb = agcTargetDb_.next();
        const auto spectrum = spectrum_.next();
        const auto mix = mix_.next();
        const auto outputGain = outputGain_.next();

        // AMOUNT 直接缩放光强；0 时旁路光电段，保证恒等。
        const auto baseDriveDb = amount > 0.0f
                                     ? kDbPerLog2 * fastLog2 (amount) + kMaxAmountDriveDb
                                     : -kDriveClampDb;

        // 每段检测、光电、auto-gain；返回该段总增益。
        const auto bandGain = [&] (int b, float left, float right) noexcept
        {
            const auto tilt = spectrum * kSpectrumTiltDb
                              * (static_cast<float> (b) - 3.0f) / 3.0f;

            optos_[static_cast<size_t> (b)].setDrive (
                juce::jlimit (-kDriveClampDb, kDriveClampDb, baseDriveDb + tilt));

            // 检测取左右较大值，两侧共用同一条增益曲线。
            const auto detector = juce::jmax (std::abs (left), std::abs (right));

            optos_[static_cast<size_t> (b)].processSample (detector);

            auto optoGain = optos_[static_cast<size_t> (b)].currentGain();

            if (amount <= 0.0f)
                optoGain = 1.0f;

            const auto bandIndex = static_cast<size_t> (b);

            if (autoGainPhase_ == 0)
            {
                const auto gainDb = 20.0f * std::log10 (juce::jmax (optoGain, kMinLogGain));

                auto& averageDb = autoGainDb_[bandIndex];
                averageDb += autoGainCoefficient_ * (gainDb - averageDb);

                // 只在低于目标时补偿，且不超过目标、不超过 +12 dB；<0.1 dB 不补偿。
                const auto compensationDb = autoGainCompensationDb (averageDb, gainDb, agcTargetDb);

                autoGainStart_[bandIndex] = autoGainCurrent_[bandIndex];
                autoGainTarget_[bandIndex] = decibelsToGain (compensationDb);
            }

            // 32 样本内线性推进；下一次更新时 start 正好落在上一目标上。
            const auto ramp = static_cast<float> (autoGainPhase_ + 1)
                            * (1.0f / static_cast<float> (kAutoGainUpdateSamples));
            autoGainCurrent_[bandIndex] = autoGainStart_[bandIndex]
                                        + (autoGainTarget_[bandIndex] - autoGainStart_[bandIndex]) * ramp;

            return optoGain * autoGainCurrent_[bandIndex];
        };

        // 干声延时线：与分频器同样的 1152 样本延迟，保证 MIX 相位对齐。
        // 先读后写，读到的就是 1152 样本前的输入增益信号。
        const auto dryLeft = dryDelay_[0][static_cast<size_t> (dryWrite_)];
        auto dryRight = dryLeft;

        if (numChannels > 1)
            dryRight = dryDelay_[1][static_cast<size_t> (dryWrite_)];

        float bandSumLeft = 0.0f;
        float bandSumRight = 0.0f;
        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        for (int b = 0; b < Filterbank::kNumOutputBands; ++b)
        {
            const auto left = bands_[static_cast<size_t> (b)].getReadPointer (0)[i];
            const auto right = numChannels > 1
                                   ? bands_[static_cast<size_t> (b)].getReadPointer (1)[i]
                                   : left;

            const auto gain = bandGain (b, left, right);

            bandSumLeft += left;
            bandSumRight += right;
            wetLeft += left * gain;
            wetRight += right * gain;
        }

        // 段 7 = x_delayed − Σ_{0..5} band。恒等式：
        // Σ g_b·band_b = g_6·x_delayed + Σ_{0..5} (g_b − g_6)·band_b。
        const auto band7Left = dryLeft - bandSumLeft;
        const auto band7Right = numChannels > 1 ? dryRight - bandSumRight : band7Left;

        const auto band7Gain = bandGain (Filterbank::kNumOutputBands, band7Left, band7Right);

        wetLeft += band7Left * band7Gain;
        wetRight += band7Right * band7Gain;

        dryDelay_[0][static_cast<size_t> (dryWrite_)] = inputGainBuffer_.getReadPointer (0)[i];

        if (numChannels > 1)
            dryDelay_[1][static_cast<size_t> (dryWrite_)]
                = inputGainBuffer_.getReadPointer (1)[i];

        dryWrite_ = (dryWrite_ + 1) % kDryDelaySamples;

        auto leftOut = (mix * wetLeft + (1.0f - mix) * dryLeft) * outputGain;
        auto rightOut = (mix * wetRight + (1.0f - mix) * dryRight) * outputGain;

        // DELTA：从正常输出里减掉对齐、且同样经过 input/output 增益的干声。
        // MIX=0 时正常输出就是 dryRef，相减严格为 0（不变量）。
        if (targets_.delta)
        {
            leftOut -= dryLeft * outputGain;
            rightOut -= dryRight * outputGain;
        }

        buffer.getWritePointer (0)[i] = leftOut;

        if (numChannels > 1)
            buffer.getWritePointer (1)[i] = rightOut;

        autoGainPhase_ = (autoGainPhase_ + 1) & (kAutoGainUpdateSamples - 1);
    }

    for (int b = 0; b < kNumBands; ++b)
        meters_[static_cast<size_t> (b)].store (
            optos_[static_cast<size_t> (b)].currentLight(), std::memory_order_relaxed);
}

} // namespace prism
