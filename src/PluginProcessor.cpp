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

#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

#include <memory>

juce::AudioProcessorValueTreeState::ParameterLayout PrismAudioProcessor::createParameterLayout()
{
    return prism::createLayout();
}

PrismAudioProcessor::PrismAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, juce::Identifier ("PRISM"), createParameterLayout())
{
    // 状态格式版本；将来改布局时靠它做迁移。
    apvts.state.setProperty (juce::Identifier ("version"), 4, nullptr);

    cacheParameterPointers();

    // 延迟与 PrismDsp::kLatencySamples 保持一致（FIR 1024 + overlap-save 块 128）。
    setLatencySamples (prism::PrismDsp::kLatencySamples);
}

void PrismAudioProcessor::cacheParameterPointers()
{
    amountParam = apvts.getRawParameterValue (prism::parameters::amount);
    spectrumParam = apvts.getRawParameterValue (prism::parameters::spectrum);
    mixParam = apvts.getRawParameterValue (prism::parameters::mix);
    inputParam = apvts.getRawParameterValue (prism::parameters::input);
    outputParam = apvts.getRawParameterValue (prism::parameters::output);
    pushParam = apvts.getRawParameterValue (prism::parameters::push);
    deltaParam = apvts.getRawParameterValue (prism::parameters::delta);
}

void PrismAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
        return;

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // 采样率或块大小变化时，prepare 内部重建 FIR 系数、时间常数与全部缓冲。
    dsp.prepare (sampleRate, samplesPerBlock, 2);
    setLatencySamples (prism::PrismDsp::kLatencySamples);
}

void PrismAudioProcessor::releaseResources()
{
    dsp.reset();
}

bool PrismAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 只接受立体声进、立体声出；单声道或带侧链的布局一律拒绝。
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.inputBuses.size() == 1
        && layouts.outputBuses.size() == 1;
}

void PrismAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    // 总线只允许立体声；万一宿主给了别的布局，清空比出声更安全。
    if (buffer.getNumChannels() != 2)
    {
        buffer.clear();
        return;
    }

    if (amountParam == nullptr)
        return;

    prism::EngineParams params;

    // 参数层是 0..100 % 与 dB，引擎要 0..1 与 dB；逐块读一次，块内由引擎逐样本平滑。
    params.amount01 = amountParam->load (std::memory_order_relaxed) * 0.01f;
    params.spectrum = spectrumParam->load (std::memory_order_relaxed) * 0.01f;
    params.mix01 = mixParam->load (std::memory_order_relaxed) * 0.01f;
    params.inputDb = inputParam->load (std::memory_order_relaxed);
    params.outputDb = outputParam->load (std::memory_order_relaxed);
    params.push01 = pushParam != nullptr
                  ? pushParam->load (std::memory_order_relaxed) * 0.01f
                  : 0.0f;
    params.delta = deltaParam != nullptr
                && deltaParam->load (std::memory_order_relaxed) > 0.5f;

    dsp.setParams (params);
    dsp.processBlock (buffer);

    juce::ignoreUnused (currentSampleRate, currentBlockSize);
}

juce::AudioProcessorEditor* PrismAudioProcessor::createEditor()
{
    // JUCE 要求返回裸所有权指针；用 unique_ptr 转移，不写 new/delete。
    return std::make_unique<PrismAudioProcessorEditor> (*this).release();
}

void PrismAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto incoming = juce::ValueTree::fromXml (*xml);
    const auto version = static_cast<int> (incoming.getProperty ("version", 1));

    apvts.replaceState (incoming);

    // v1 没有 DELTA；v2/v3 的 DELTA 读实际值。v1–v3 都还没有 PUSH，
    // 迁移时 PUSH 回落到 0，LINK 参数已删除，旧状态里的残留节点一并移除。
    if (version < 2)
    {
        if (auto* deltaParameter = apvts.getParameter (prism::parameters::delta))
            deltaParameter->setValueNotifyingHost (0.0f);
    }

    if (version < 4)
    {
        if (auto* pushParameter = apvts.getParameter (prism::parameters::push))
            pushParameter->setValueNotifyingHost (0.0f);

        const auto linkChild = apvts.state.getChildWithProperty (
            juce::Identifier ("id"), juce::String ("link"));
        if (linkChild.isValid())
            apvts.state.removeChild (linkChild, nullptr);
    }

    apvts.state.setProperty (juce::Identifier ("version"), 4, nullptr);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return std::make_unique<PrismAudioProcessor>().release();
}
