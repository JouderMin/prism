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
    apvts.state.setProperty (juce::Identifier ("version"), 1, nullptr);

    // 当前仍透传，DSP 接线后改为 prism::PrismDsp::kLatencySamples。
    setLatencySamples (0);
}

void PrismAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // 透传，无内部延迟。
    setLatencySamples (0);
}

void PrismAudioProcessor::releaseResources()
{
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

    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // 多出来的输出通道清零；输入通道原样保留（透传）。
    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

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

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return std::make_unique<PrismAudioProcessor>().release();
}
