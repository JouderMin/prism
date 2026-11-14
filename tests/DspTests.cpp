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

#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "dsp/PrismDsp.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kProcessBlock = 512;

void fillSine (juce::AudioBuffer<float>& buffer, double frequency, float amplitude)
{
    const auto twoPi = juce::MathConstants<double>::twoPi;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.getWritePointer (ch)[i] = amplitude
                * static_cast<float> (std::sin (twoPi * frequency
                                                * static_cast<double> (i) / kSampleRate));
}

void processInBlocks (prism::PrismDsp& dsp, juce::AudioBuffer<float>& buffer, int blockSize)
{
    for (int offset = 0; offset < buffer.getNumSamples(); offset += blockSize)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - offset);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                       buffer.getNumChannels(), offset, count);
        dsp.processBlock (view);
    }
}

juce::AudioBuffer<float> renderEngine (const prism::EngineParams& params,
                                       const juce::AudioBuffer<float>& input,
                                       int blockSize)
{
    prism::PrismDsp dsp;
    dsp.setParams (params);
    dsp.prepare (kSampleRate, blockSize, input.getNumChannels());

    juce::AudioBuffer<float> output (input);
    processInBlocks (dsp, output, blockSize);
    return output;
}

double rmsOf (const juce::AudioBuffer<float>& buffer, int start, int length, int channel)
{
    double sum = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const auto value = static_cast<double> (buffer.getReadPointer (channel)[start + i]);
        sum += value * value;
    }

    return std::sqrt (sum / static_cast<double> (juce::jmax (1, length)));
}

class DspTests final : public juce::UnitTest
{
public:
    DspTests() : juce::UnitTest ("PRISM Dsp", "prism") {}

    void runTest() override
    {
        testIdentity();
        testCompression();
        testSilence();
        testLatency();
        testDelta();
        testPush();
        testAgcPushTarget();
        testSmoothing();
    }

private:
    void testIdentity()
    {
        beginTest ("identity: AMOUNT 0, MIX 1, unity gain");

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 0.0f;
        params.mix01 = 1.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> buffer (2, 8192);
        juce::Random random (1234);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.getWritePointer (ch)[i] = (random.nextFloat() * 2.0f - 1.0f) * 0.25f;

        const juce::AudioBuffer<float> reference (buffer);

        processInBlocks (dsp, buffer, kProcessBlock);

        double maxError = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples() - prism::PrismDsp::kLatencySamples; ++i)
            {
                const auto output = static_cast<double> (
                    buffer.getReadPointer (ch)[i + prism::PrismDsp::kLatencySamples]);
                const auto input = static_cast<double> (reference.getReadPointer (ch)[i]);
                maxError = juce::jmax (maxError, std::abs (output - input));
            }

        logMessage ("max identity error after latency: " + juce::String (maxError, 12));
        expectLessThan (maxError, 1.0e-4);
    }

    void testCompression()
    {
        beginTest ("compression: AMOUNT 100 reduces a 1 kHz tone");

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 1.0f;
        params.mix01 = 1.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        const auto amplitude = static_cast<float> (std::pow (10.0, -6.0 / 20.0));
        constexpr int kLength = 72000; // 1.5 s

        juce::AudioBuffer<float> buffer (2, kLength);
        fillSine (buffer, 1000.0, amplitude);

        processInBlocks (dsp, buffer, kProcessBlock);

        const auto inputRms = static_cast<double> (amplitude) / std::sqrt (2.0);
        const auto outputRms = rmsOf (buffer, kLength - 24000, 24000, 0);
        const auto reductionDb = 20.0 * std::log10 (outputRms / inputRms);

        logMessage ("1 kHz -6 dBFS, output vs input: " + juce::String (reductionDb, 2) + " dB");
        expectLessThan (reductionDb, -3.0);
    }

    void testSilence()
    {
        beginTest ("silence produces no NaN/Inf");

        prism::PrismDsp dsp;
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> buffer (2, 48000);
        buffer.clear();

        processInBlocks (dsp, buffer, kProcessBlock);

        bool finite = true;
        float peak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (buffer.getReadPointer (ch)[i]));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                finite = finite && std::isfinite (buffer.getReadPointer (ch)[i]);

        for (auto& meter : dsp.meters())
        {
            const auto value = meter.load (std::memory_order_relaxed);
            finite = finite && std::isfinite (value) && value >= 0.0f && value < 1.0f;
        }

        logMessage ("silence peak: " + juce::String (peak, 12));
        expect (finite);
        expectLessThan (peak, 1.0e-6f);
    }

    void testLatency()
    {
        beginTest ("latency: impulse peak lands at 1152");

        expectEquals (prism::PrismDsp::kLatencySamples, 1152);

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 0.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> buffer (2, 4096);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);

        processInBlocks (dsp, buffer, kProcessBlock);

        int peakIndex = 0;
        float peakValue = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = std::abs (buffer.getReadPointer (ch)[i]);

                if (value > peakValue)
                {
                    peakValue = value;
                    peakIndex = i;
                }
            }

        logMessage ("impulse peak at sample " + juce::String (peakIndex)
                    + ", value " + juce::String (peakValue, 6));
        expectEquals (peakIndex, 1152);
        expectWithinAbsoluteError (peakValue, 1.0f, 1.0e-2f);
    }

    void testDelta()
    {
        beginTest ("DELTA outputs the aligned wet-minus-dry difference");

        constexpr int kLength = 16384;

        juce::AudioBuffer<float> input (2, kLength);
        juce::Random random (20261113);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLength; ++i)
                input.getWritePointer (ch)[i] = (random.nextFloat() * 2.0f - 1.0f) * 0.25f;

        prism::EngineParams normalParams;
        normalParams.amount01 = 0.8f;
        normalParams.mix01 = 1.0f;

        auto dryParams = normalParams;
        dryParams.amount01 = 0.0f;

        auto deltaParams = normalParams;
        deltaParams.delta = true;

        const auto normal = renderEngine (normalParams, input, kProcessBlock);
        const auto dry = renderEngine (dryParams, input, kProcessBlock);
        const auto delta = renderEngine (deltaParams, input, kProcessBlock);

        double maxDifference = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLength; ++i)
            {
                const auto expected = static_cast<double> (normal.getReadPointer (ch)[i])
                                    - static_cast<double> (dry.getReadPointer (ch)[i]);
                const auto actual = static_cast<double> (delta.getReadPointer (ch)[i]);
                maxDifference = juce::jmax (maxDifference, std::abs (actual - expected));
            }

        logMessage ("max DELTA vs (normal - dry) error: " + juce::String (maxDifference, 12));
        expectLessThan (maxDifference, 1.0e-6);

        // AMOUNT=0 时 wet 与 dry 相同，DELTA 必须静音。
        auto silentDeltaParams = dryParams;
        silentDeltaParams.delta = true;
        const auto silentDelta = renderEngine (silentDeltaParams, input, kProcessBlock);

        auto silentPeak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLength; ++i)
                silentPeak = juce::jmax (silentPeak,
                                         std::abs (silentDelta.getReadPointer (ch)[i]));

        logMessage ("AMOUNT=0 DELTA peak: " + juce::String (silentPeak, 12));
        expectLessThan (silentPeak, 1.0e-5f); // < -100 dBFS

        // MIX=0 时正常输出就是 dryRef，DELTA 相减严格为 0。
        auto mixZeroParams = normalParams;
        mixZeroParams.mix01 = 0.0f;
        mixZeroParams.delta = true;
        const auto mixZero = renderEngine (mixZeroParams, input, kProcessBlock);

        auto mixZeroPeak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLength; ++i)
                mixZeroPeak = juce::jmax (mixZeroPeak,
                                          std::abs (mixZero.getReadPointer (ch)[i]));

        logMessage ("MIX=0 DELTA peak: " + juce::String (mixZeroPeak, 12));
        expectLessThan (mixZeroPeak, 1.0e-12f);
    }

    void testPush()
    {
        beginTest ("PUSH is an extra gain stage cancelled at the output");

        constexpr int kLength = 48000;
        constexpr int kMeasureStart = kLength - 24000;

        const auto renderLevelDb = [] (const prism::EngineParams& params, float amplitude)
        {
            juce::AudioBuffer<float> input (2, kLength);
            fillSine (input, 1000.0, amplitude);
            const auto output = renderEngine (params, input, kProcessBlock);
            const auto level = rmsOf (output, kMeasureStart, 24000, 0);
            return 20.0 * std::log10 (juce::jmax (level, 1.0e-12));
        };

        // 低电平、无压缩：PUSH 只是被输出级抵消的增益级。
        prism::EngineParams lowParams;
        lowParams.amount01 = 0.0f;
        lowParams.mix01 = 1.0f;
        lowParams.pushDb = 0.0f;
        const auto lowPush0 = renderLevelDb (lowParams, 0.01f);
        lowParams.pushDb = 12.0f;
        const auto lowPush12 = renderLevelDb (lowParams, 0.01f);
        const auto lowDifference = std::abs (lowPush12 - lowPush0);
        logMessage ("low level (-40 dBFS): PUSH 0 = " + juce::String (lowPush0, 3)
                    + " dB, PUSH 12 = " + juce::String (lowPush12, 3)
                    + " dB, difference " + juce::String (lowDifference, 3) + " dB");
        expectLessOrEqual (lowDifference, 0.05);

        // 压缩加深：PUSH 抬高进入压缩器的电平，输出级抵消后剩下的就是额外 GR。
        constexpr float twelveDbfs = 0.25118864f;
        prism::EngineParams compressionParams;
        compressionParams.amount01 = 1.0f;
        compressionParams.mix01 = 1.0f;

        compressionParams.pushDb = 0.0f;
        const auto compressedPush0 = renderLevelDb (compressionParams, twelveDbfs);
        compressionParams.pushDb = 12.0f;
        const auto compressedPush12 = renderLevelDb (compressionParams, twelveDbfs);
        const auto extraReduction = compressedPush0 - compressedPush12;
        logMessage ("-12 dBFS compression: PUSH 0 = " + juce::String (compressedPush0, 3)
                    + " dB, PUSH 12 = " + juce::String (compressedPush12, 3)
                    + " dB, extra reduction " + juce::String (extraReduction, 3) + " dB");
        expectGreaterOrEqual (extraReduction, 4.0);

        // 0–24 dB 全范围：有限且输出不越界。
        prism::EngineParams sweepParams;
        sweepParams.amount01 = 1.0f;
        sweepParams.mix01 = 1.0f;

        juce::AudioBuffer<float> sweepInput (2, 24000);
        fillSine (sweepInput, 1000.0, 0.25f);

        bool finite = true;
        float peak = 0.0f;

        for (float pushDb = 0.0f; pushDb <= 24.0f; pushDb += 4.0f)
        {
            sweepParams.pushDb = pushDb;
            const auto output = renderEngine (sweepParams, sweepInput, kProcessBlock);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < output.getNumSamples(); ++i)
                {
                    const auto value = output.getReadPointer (ch)[i];
                    finite = finite && std::isfinite (value);
                    peak = juce::jmax (peak, std::abs (value));
                }
        }

        logMessage ("PUSH 0..24 dB sweep: peak " + juce::String (peak, 6)
                    + ", finite " + (finite ? "yes" : "no"));
        expect (finite);
        expectLessOrEqual (peak, 1.0f);
    }

    void testAgcPushTarget()
    {
        beginTest ("auto-gain target follows PUSH and stops fighting its attenuation");

        // push=0 时目标为 0 dB，公式必须与旧实现逐位一致。
        float maxLegacyDifference = 0.0f;

        for (float averageDb = -24.0f; averageDb <= 0.0f; averageDb += 1.5f)
            for (float gainDb = -24.0f; gainDb <= 0.0f; gainDb += 1.5f)
            {
                auto legacy = juce::jmin (-averageDb, -gainDb);
                legacy = juce::jlimit (0.0f, 12.0f, legacy);

                if (legacy < 0.1f)
                    legacy = 0.0f;

                const auto current = prism::PrismDsp::autoGainCompensationDb (
                    averageDb, gainDb, 0.0f);
                maxLegacyDifference = juce::jmax (maxLegacyDifference,
                                                  std::abs (current - legacy));
            }

        logMessage ("push=0 AGC compensation max difference from old formula: "
                    + juce::String (maxLegacyDifference, 12) + " dB");
        expectLessOrEqual (maxLegacyDifference, 1.0e-6f);

        // 目标 -12 dB 时，补偿后不得越过目标，也不得把高于目标的增益往下压。
        for (float averageDb = -24.0f; averageDb <= -13.0f; averageDb += 1.0f)
            for (float gainDb = -24.0f; gainDb <= -13.0f; gainDb += 1.0f)
            {
                const auto compensation = prism::PrismDsp::autoGainCompensationDb (
                    averageDb, gainDb, -12.0f);
                expectLessOrEqual (averageDb + compensation, -12.0f + 1.0e-4f);
                expectLessOrEqual (gainDb + compensation, -12.0f + 1.0e-4f);
            }

        expectEquals (prism::PrismDsp::autoGainCompensationDb (-5.0f, -5.0f, -12.0f), 0.0f);

        constexpr int kLength = 144000;  // 3 s
        constexpr int kMeasureLength = 48000;
        constexpr int kMeasureStart = kLength - kMeasureLength;

        const auto renderLevelDb = [] (const prism::EngineParams& params, float amplitude)
        {
            juce::AudioBuffer<float> input (2, kLength);
            fillSine (input, 1000.0, amplitude);
            const auto output = renderEngine (params, input, kProcessBlock);

            const auto inputLevel = rmsOf (input, kMeasureStart, kMeasureLength, 0);
            const auto outputLevel = rmsOf (output, kMeasureStart, kMeasureLength, 0);
            return 20.0 * std::log10 (juce::jmax (outputLevel, 1.0e-12)
                                      / juce::jmax (inputLevel, 1.0e-12));
        };

        prism::EngineParams loudParams;
        loudParams.amount01 = 1.0f;
        loudParams.mix01 = 1.0f;
        loudParams.pushDb = 12.0f;

        const auto loudLevel = renderLevelDb (loudParams, 0.25118864f);  // -12 dBFS
        logMessage ("push=12 dB, -12 dBFS steady level change: "
                    + juce::String (loudLevel, 3) + " dB (AGC target -12 dB)");
        expectLessOrEqual (loudLevel, -11.8);
        expectGreaterThan (loudLevel, -14.5);

        // -40 dBFS 时压缩器只轻微作用，AGC 必须保持 1.0（不提升）。
        prism::PrismDsp quietDsp;
        quietDsp.setParams (loudParams);
        quietDsp.prepare (kSampleRate, kProcessBlock, 2);
        juce::AudioBuffer<float> quietInput (2, kLength);
        fillSine (quietInput, 1000.0, 0.01f);
        processInBlocks (quietDsp, quietInput, kProcessBlock);

        const auto quietGains = quietDsp.currentAutoGainForTesting();
        float maxQuietGain = 0.0f;
        float minQuietGain = 10.0f;

        for (const auto gain : quietGains)
        {
            maxQuietGain = juce::jmax (maxQuietGain, gain);
            minQuietGain = juce::jmin (minQuietGain, gain);
        }

        const auto quietLevel = renderLevelDb (loudParams, 0.01f);
        logMessage ("push=12 dB, -40 dBFS steady level change: "
                    + juce::String (quietLevel, 3) + " dB, AGC gain "
                    + juce::String (minQuietGain, 6) + ".." + juce::String (maxQuietGain, 6));
        expectWithinAbsoluteError (maxQuietGain, 1.0f, 1.0e-3f);
        expectWithinAbsoluteError (minQuietGain, 1.0f, 1.0e-3f);

        // PUSH 从 0 平滑扫到 24 dB：不产生 NaN/Inf，输出有限。
        prism::PrismDsp dsp;
        prism::EngineParams sweepParams;
        sweepParams.amount01 = 1.0f;
        sweepParams.mix01 = 1.0f;
        dsp.setParams (sweepParams);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> sweepInput (2, kLength);
        fillSine (sweepInput, 1000.0, 0.25f);
        juce::AudioBuffer<float> sweepOutput (sweepInput);
        bool finite = true;
        float peak = 0.0f;

        for (int offset = 0; offset < kLength; offset += kProcessBlock)
        {
            const auto count = juce::jmin (kProcessBlock, kLength - offset);
            sweepParams.pushDb = 24.0f * static_cast<float> (offset)
                               / static_cast<float> (kLength);
            dsp.setParams (sweepParams);
            juce::AudioBuffer<float> view (sweepOutput.getArrayOfWritePointers(),
                                           sweepOutput.getNumChannels(), offset, count);
            dsp.processBlock (view);
        }

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLength; ++i)
            {
                const auto value = sweepOutput.getReadPointer (ch)[i];
                finite = finite && std::isfinite (value);
                peak = juce::jmax (peak, std::abs (value));
            }

        logMessage ("PUSH 0..24 dB ramp: peak " + juce::String (peak, 6)
                    + ", finite " + (finite ? "yes" : "no"));
        expect (finite);
        expectLessOrEqual (peak, 4.0f);

        // 冲激延迟：PUSH 0 与 12 dB 都仍在 1152。
        for (const auto pushDb : { 0.0f, 12.0f })
        {
            prism::EngineParams params;
            params.amount01 = 0.0f;
            params.pushDb = pushDb;
            params.mix01 = 1.0f;

            juce::AudioBuffer<float> impulse (2, 4096);
            impulse.clear();
            impulse.setSample (0, 0, 1.0f);
            const auto output = renderEngine (params, impulse, kProcessBlock);

            int peakIndex = 0;
            float peakValue = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < output.getNumSamples(); ++i)
                {
                    const auto value = std::abs (output.getReadPointer (ch)[i]);

                    if (value > peakValue)
                    {
                        peakValue = value;
                        peakIndex = i;
                    }
                }

            logMessage ("impulse latency at PUSH " + juce::String (pushDb, 1)
                        + " dB: sample " + juce::String (peakIndex)
                        + ", value " + juce::String (peakValue, 6));
            expectEquals (peakIndex, 1152);
            expectWithinAbsoluteError (peakValue, 1.0f, 1.0e-2f);
        }
    }

    void testSmoothing()
    {
        beginTest ("smoothing: AMOUNT jump settles within 20 ms, no step");

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 0.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, 1024, 2);

        constexpr int kSettleSamples = 960; // 20 ms at 48 kHz
        constexpr int kLength = 4800 + kSettleSamples;

        juce::AudioBuffer<float> buffer (2, kLength);
        fillSine (buffer, 1000.0, 0.5f);

        // 先跑 100 ms 稳定状态，再突跳 AMOUNT。
        {
            juce::AudioBuffer<float> head (buffer.getArrayOfWritePointers(), 2, 0, 4800);
            processInBlocks (dsp, head, 480);
        }

        dsp.setParams ([&] {
            auto jumped = params;
            jumped.amount01 = 1.0f;
            return jumped;
        }());

        {
            juce::AudioBuffer<float> tail (buffer.getArrayOfWritePointers(), 2, 4800, kSettleSamples);
            dsp.processBlock (tail);
        }

        const auto smoothed = dsp.currentAmount01();
        logMessage ("smoothed AMOUNT after 20 ms: " + juce::String (smoothed, 6));

        float maxDelta = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxDelta = juce::jmax (maxDelta,
                                       std::abs (buffer.getReadPointer (ch)[i]
                                                 - buffer.getReadPointer (ch)[i - 1]));

        logMessage ("max adjacent sample delta: " + juce::String (maxDelta, 6));
        expectGreaterThan (smoothed, 0.99f);
        expectLessThan (maxDelta, 0.2f);
    }
};

DspTests dspTests;

} // namespace
