#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/BandTable.h"

class PrismAudioProcessor;

// PRISM 主界面：720×480，上面一条标题，中间 7 根光柱，下面 5 个旋钮。
// 光柱 30 Hz 从处理器 meters() 读取；旋钮经 SliderAttachment 绑定 APVTS。
// 全部成员是值或 unique_ptr，不使用裸 new/delete。
class PrismAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit PrismAudioProcessorEditor (PrismAudioProcessor& processorToEdit);
    ~PrismAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kNumBands = static_cast<int> (prism::kBands.size());
    static constexpr int kNumKnobs = 5;

    void timerCallback() override;
    void drawMeters (juce::Graphics& g) const;

    PrismAudioProcessor& audioProcessor;

    // 界面侧平滑后的光强，避免 30 Hz 采样台阶。
    std::array<float, kNumBands> displayedLevels {};

    std::array<std::unique_ptr<juce::Slider>, kNumKnobs> knobs;
    std::array<std::unique_ptr<juce::Label>, kNumKnobs> knobLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kNumKnobs> attachments;

    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessorEditor)
};
