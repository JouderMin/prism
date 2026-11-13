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
        testLink();
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

    void testLink()
    {
        beginTest ("LINK makes OUTPUT cancel INPUT gain at the output stage");

        constexpr int kLength = 48000;

        juce::AudioBuffer<float> input (2, kLength);
        fillSine (input, 1000.0, 0.1f);

        const auto inputRms = rmsOf (input, kLength - 24000, 24000, 0);
        const auto measureGainDb = [&] (const prism::EngineParams& params)
        {
            const auto output = renderEngine (params, input, kProcessBlock);
            const auto outputRms = rmsOf (output, kLength - 24000, 24000, 0);
            return 20.0 * std::log10 (outputRms / inputRms);
        };

        prism::EngineParams params;
        params.amount01 = 0.0f; // 直通，仅测输入/输出增益级
        params.mix01 = 1.0f;
        params.inputDb = 6.0f;

        params.link = false;
        const auto unlinkedDb = measureGainDb (params);
        logMessage ("LINK off, INPUT +6 dB: " + juce::String (unlinkedDb, 3) + " dB");
        expectWithinAbsoluteError (unlinkedDb, 6.0, 0.01);

        params.link = true;
        const auto linkedDb = measureGainDb (params);
        const auto linkedOutputStageDb = linkedDb - params.inputDb;
        logMessage ("LINK on, INPUT +6 dB: net " + juce::String (linkedDb, 3)
                    + " dB, output stage " + juce::String (linkedOutputStageDb, 3) + " dB");
        expectWithinAbsoluteError (linkedDb, 0.0, 0.01);
        expectWithinAbsoluteError (linkedOutputStageDb, -6.0, 0.01);

        params.inputDb = 3.7f;
        const auto linked37Db = measureGainDb (params);
        const auto linked37OutputStageDb = linked37Db - params.inputDb;
        logMessage ("LINK on, INPUT +3.7 dB: net " + juce::String (linked37Db, 3)
                    + " dB, output stage " + juce::String (linked37OutputStageDb, 3) + " dB");
        expectWithinAbsoluteError (linked37Db, 0.0, 0.01);
        expectWithinAbsoluteError (linked37OutputStageDb, -3.7, 0.01);
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
