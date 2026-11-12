#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <memory>

PrismAudioProcessor::PrismAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // M1 透传：不引入延迟。DSP 任务会改为 1152。
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

void PrismAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // M1 无状态可存。
}

void PrismAudioProcessor::setStateInformation (const void*, int)
{
    // M1 无状态可恢复。
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return std::make_unique<PrismAudioProcessor>().release();
}
