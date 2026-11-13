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

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/BandTable.h"

class PrismAudioProcessor;

// PRISM 主界面：720×480，上面一条标题（右侧 DELTA 开关），中间 7 根光柱，下面 5 个旋钮。
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

    // 只读访问器，供 EditorTests 检查字标嵌入与布局；不参与音频路径。
    const juce::Drawable* getLogoForTesting() const noexcept { return logo.get(); }
    juce::Rectangle<float> getLogoBoundsForTesting() const noexcept { return logoBounds; }
    juce::Rectangle<int> getTitleRowBoundsForTesting() const noexcept { return titleRowBounds; }
    juce::Rectangle<int> getPrismTextBoundsForTesting() const noexcept { return prismTextBounds; }
    juce::TextButton* getDeltaButtonForTesting() const noexcept { return deltaButton.get(); }

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

    // 标题行右侧的 DELTA 监听开关；绑定 bool 参数，不改变 5 个旋钮的布局。
    std::unique_ptr<juce::TextButton> deltaButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment;

    juce::Rectangle<int> meterArea;

    // 标题行左侧的 Stargazer 字标（SVG 由 BinaryData 编入）。
    // 解析失败时保持空指针，paint() 跳过绘制，不崩溃。
    std::unique_ptr<juce::Drawable> logo;
    juce::Rectangle<float> logoBounds;
    juce::Rectangle<int> titleRowBounds;
    juce::Rectangle<int> prismTextBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessorEditor)
};
