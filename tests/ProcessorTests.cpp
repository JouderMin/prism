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

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/PrismDsp.h"

namespace
{

constexpr int kBlockSize = 512;
constexpr int kNumBlocksToSettle = 8;

void setParameterValue (juce::AudioProcessorValueTreeState& apvts, const char* id, float plainValue)
{
    if (auto* parameter = apvts.getParameter (id))
    {
        const auto normalised = parameter->convertTo0to1 (plainValue);
        parameter->setValueNotifyingHost (normalised);
    }
}

/** 跑若干块静音让参数平滑到位，再在块首打一个冲激，返回输出峰值位置与幅度。 */
int runImpulse (PrismAudioProcessor& processor, float& peakMagnitude)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < kNumBlocksToSettle; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }

    std::vector<float> output;
    output.reserve (static_cast<size_t> (6 * kBlockSize));

    for (int block = 0; block < 6; ++block)
    {
        buffer.clear();

        if (block == 0)
        {
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);
        }

        processor.processBlock (buffer, midi);

        for (int i = 0; i < kBlockSize; ++i)
            output.push_back (buffer.getSample (0, i));
    }

    auto peakIndex = 0;
    peakMagnitude = 0.0f;

    for (size_t i = 0; i < output.size(); ++i)
    {
        const auto magnitude = std::abs (output[i]);

        if (magnitude > peakMagnitude)
        {
            peakMagnitude = magnitude;
            peakIndex = static_cast<int> (i);
        }
    }

    return peakIndex;
}

} // namespace

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("PRISM Processor", "prism") {}

    void runTest() override
    {
        testLatencyReporting();
        testImpulseThroughProcessor();
    }

private:
    void testLatencyReporting()
    {
        beginTest ("processor reports 1152 samples latency");

        PrismAudioProcessor processor;
        expectEquals (processor.getLatencySamples(), prism::PrismDsp::kLatencySamples);

        processor.prepareToPlay (48000.0, kBlockSize);
        expectEquals (processor.getLatencySamples(), 1152);

        // 采样率与块大小变化后仍应重建并回报同一延迟。
        processor.prepareToPlay (96000.0, 128);
        expectEquals (processor.getLatencySamples(), 1152);

        processor.releaseResources();
        expectEquals (processor.getLatencySamples(), 1152);
    }

    void testImpulseThroughProcessor()
    {
        beginTest ("impulse through processBlock peaks at latency 1152");

        PrismAudioProcessor processor;
        processor.prepareToPlay (48000.0, kBlockSize);

        // AMOUNT 0 + MIX 100 时引擎是恒等的（仅为对齐延迟的湿声）。
        setParameterValue (processor.apvts, "amount", 0.0f);
        setParameterValue (processor.apvts, "mix", 100.0f);

        float peakMagnitude = 0.0f;
        const auto peakIndex = runImpulse (processor, peakMagnitude);

        logMessage ("impulse peak at sample " + juce::String (peakIndex)
                    + ", magnitude " + juce::String (static_cast<double> (peakMagnitude), 6));

        expectEquals (peakIndex, prism::PrismDsp::kLatencySamples);
        expectWithinAbsoluteError (peakMagnitude, 1.0f, 1.0e-3f);

        processor.releaseResources();
    }
};

static ProcessorTests processorTests;
