#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// PRISM 七段光电压缩器。
// M1 只做透传骨架：音频原样通过，延迟 0，用于验证三格式构建链路。
class PrismAudioProcessor final : public juce::AudioProcessor
{
public:
    PrismAudioProcessor();
    ~PrismAudioProcessor() override = default;

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

private:
    double currentSampleRate = 0.0;
    int currentBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessor)
};
