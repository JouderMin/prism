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

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "dsp/PrismDsp.h"

// PRISM 七段光电压缩器。
// 5 个连续参数 + DELTA 开关经 APVTS 读入 PrismDsp；延迟 1152 样本由引擎上报。
class PrismAudioProcessor final : public juce::AudioProcessor
{
public:
    PrismAudioProcessor();
    ~PrismAudioProcessor() override = default;

    // 参数定义在 Parameters.h；这里只做转发，便于测试与后续接线。
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- AudioProcessor ---------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) noexcept override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // 只有立体声总线；宿主给单声道或非立体声布局时由宿主重配总线。
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    const juce::String getName() const override { return "PRISM"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 公开给测试与处理器内部使用；生命周期与 processor 相同。
    juce::AudioProcessorValueTreeState apvts;

    // 每段光强（0..1），界面线程 30 Hz 以 relaxed 读取。
    std::array<std::atomic<float>, prism::PrismDsp::kNumBands>& meters() noexcept
    {
        return dsp.meters();
    }

private:
    // 参数指针在构造时缓存，音频线程只做 atomic load，不查表、不加锁。
    void cacheParameterPointers();

    prism::PrismDsp dsp;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* spectrumParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* pushParam = nullptr;
    std::atomic<float>* deltaParam = nullptr;

    double currentSampleRate = 0.0;
    int currentBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessor)
};
