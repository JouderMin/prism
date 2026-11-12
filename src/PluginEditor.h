#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PrismAudioProcessor;

// M1 最小编辑器：720x480 深色底 + PRISM 文字。
// 真正的界面在后续任务补；这里只验证编辑器创建与绘制链路。
class PrismAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PrismAudioProcessorEditor (PrismAudioProcessor& processor);
    ~PrismAudioProcessorEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessorEditor)
};
