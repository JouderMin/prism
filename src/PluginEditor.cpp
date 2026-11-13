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

#include "PluginEditor.h"

#include <cmath>

#include "BinaryData.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
const juce::Colour backgroundColour { 0xff0b0b0f };
const juce::Colour panelColour      { 0xff13131a };
const juce::Colour trackColour      { 0xff1c1c26 };
const juce::Colour titleColour      { 0xffe8e6df };
const juce::Colour accentColour     { 0xffe2a33c };
const juce::Colour hintColour       { 0xff6a6a78 };

constexpr int kTimerHz = 30;

// 频段文字标签，从左到右与 kBands 对齐。用 UTF-8 的 en dash（U+2013），
// 避免 juce::String(const char*) 的 ASCII 断言；绘制时走 String::fromUTF8。
const char* const kBandRanges[7] = { "20\xe2\x80\x93" "150",
                                     "150\xe2\x80\x93" "500",
                                     "500\xe2\x80\x93" "1.2k",
                                     "1.2\xe2\x80\x93" "3k",
                                     "3\xe2\x80\x93" "8k",
                                     "8\xe2\x80\x93" "14k",
                                     "14k+" };

// dvisvgm 把每个字形写成 <use x=... y=...>，而 JUCE 8 的 SVGParser 只解析
// xlink:href、不读 use 的 x/y（juce_SVGParser.cpp 的 parseUsePath）。
// 这里把 x/y 折成 transform，交给 parseShape 的变换路径处理。
void foldUseOffsets (juce::XmlElement& element)
{
    if (element.hasTagName ("use"))
    {
        const auto x = element.getStringAttribute ("x");
        const auto y = element.getStringAttribute ("y");

        if (x.isNotEmpty() || y.isNotEmpty())
        {
            element.setAttribute ("transform",
                                  "translate(" + (x.isEmpty() ? juce::String ("0") : x)
                                      + "," + (y.isEmpty() ? juce::String ("0") : y) + ")");
            element.removeAttribute ("x");
            element.removeAttribute ("y");
        }
    }

    for (auto* child = element.getFirstChildElement(); child != nullptr; child = child->getNextElement())
        foldUseOffsets (*child);
}

void configureKnob (juce::Slider& knob){
    knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 18);
    knob.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    knob.setColour (juce::Slider::rotarySliderOutlineColourId, trackColour.brighter (0.35f));
    knob.setColour (juce::Slider::thumbColourId, titleColour);
    knob.setColour (juce::Slider::textBoxTextColourId, titleColour.withAlpha (0.85f));
    knob.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.setColour (juce::Slider::textBoxHighlightColourId, accentColour.withAlpha (0.25f));
}
} // namespace

PrismAudioProcessorEditor::DeltaButton::DeltaButton()
    : juce::TextButton ("Delta")
{
    setName ("Delta");
    setTooltip ("Delta monitor (wet minus dry)");
    setClickingTogglesState (true);
    setButtonText ({});
}

void PrismAudioProcessorEditor::DeltaButton::paintButton (juce::Graphics& g,
                                                          bool shouldDrawButtonAsHighlighted,
                                                          bool)
{
    const auto circle = getLocalBounds().toFloat().withSizeKeepingCentre (22.0f, 22.0f);
    const auto on = getToggleState();

    if (on)
    {
        g.setColour (findColour (juce::TextButton::buttonOnColourId));
        g.fillEllipse (circle);
    }
    else
    {
        g.setColour (findColour (juce::TextButton::buttonColourId));
        g.fillEllipse (circle);
        g.setColour (findColour (juce::TextButton::textColourOffId)
                         .withAlpha (shouldDrawButtonAsHighlighted ? 1.0f : 0.85f));
        g.drawEllipse (circle, 1.2f);
    }

    g.setColour (on ? findColour (juce::TextButton::textColourOnId)
                    : findColour (juce::TextButton::textColourOffId));
    g.setFont (juce::Font (juce::FontOptions { 12.0f, juce::Font::bold }));
    g.drawText (juce::String::fromUTF8 ("\xe2\x88\x86"),
                circle.toNearestInt(), juce::Justification::centred, false);
}

bool PrismAudioProcessorEditor::DeltaButton::hitTest (int x, int y)
{
    const auto centre = getLocalBounds().getCentre();
    const auto dx = static_cast<float> (x - centre.x);
    const auto dy = static_cast<float> (y - centre.y);
    return dx * dx + dy * dy <= 16.0f * 16.0f;
}

PrismAudioProcessorEditor::LinkButton::LinkButton()
    : juce::TextButton ("LINK")
{
    setName ("Link");
    setTooltip ("Link OUTPUT to INPUT level");
    setClickingTogglesState (true);
}

void PrismAudioProcessorEditor::LinkButton::paintButton (juce::Graphics& g,
                                                         bool shouldDrawButtonAsHighlighted,
                                                         bool)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    if (on)
    {
        g.setColour (findColour (juce::TextButton::buttonOnColourId));
        g.fillRoundedRectangle (bounds, 10.0f);
    }
    else
    {
        g.setColour (findColour (juce::TextButton::buttonColourId));
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (findColour (juce::TextButton::textColourOffId)
                         .withAlpha (shouldDrawButtonAsHighlighted ? 1.0f : 0.85f));
        g.drawRoundedRectangle (bounds, 10.0f, 1.2f);
    }

    g.setColour (on ? findColour (juce::TextButton::textColourOnId)
                    : findColour (juce::TextButton::textColourOffId));
    g.setFont (juce::Font (juce::FontOptions { 10.0f, juce::Font::bold }));
    g.drawText ("LINK", bounds.toNearestInt(), juce::Justification::centred, false);
}

PrismAudioProcessorEditor::PrismAudioProcessorEditor (PrismAudioProcessor& processorToEdit)
    : AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    setOpaque (true);
    setResizable (false, false);

    // 字标：直接解析 SVG（JUCE 8 SVGParser 支持 <use>），并换成标题色。
    // 解析失败时只断言、保留空指针，paint() 会跳过绘制。
    if (auto xml = juce::XmlDocument::parse (
            juce::String::fromUTF8 (reinterpret_cast<const char*> (BinaryData::logo_svg),
                                    BinaryData::logo_svgSize)))
    {
        foldUseOffsets (*xml);
        logo = juce::Drawable::createFromSVG (*xml);
    }

    if (logo != nullptr)
        logo->replaceColour (juce::Colours::black, titleColour);
    else
        jassertfalse;

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const auto& spec = prism::parameters::kSpecs[static_cast<size_t> (i)];

        auto label = std::make_unique<juce::Label>();
        label->setText (spec.name, juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, hintColour);
        label->setFont (juce::Font (juce::FontOptions { 12.0f, juce::Font::bold }));
        label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*label);
        knobLabels[static_cast<size_t> (i)] = std::move (label);

        auto knob = std::make_unique<juce::Slider>();
        configureKnob (*knob);
        knob->setName (spec.name);

        addAndMakeVisible (*knob);
        knobs[static_cast<size_t> (i)] = std::move (knob);

        attachments[static_cast<size_t> (i)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                audioProcessor.apvts, spec.id, *knobs[static_cast<size_t> (i)]);

        // SliderAttachment 构造时会把 textFromValueFunction / valueFromTextFunction
        // 改成它自己的版本（JUCE juce_ParameterAttachments.cpp），必须在其后再装一次，
        // 让旋钮读数与参数文本共用 parameters::formatValue。
        auto& knobRef = *knobs[static_cast<size_t> (i)];
        knobRef.textFromValueFunction = [spec] (double value)
        {
            return prism::parameters::formatValue (spec, static_cast<float> (value));
        };
        knobRef.valueFromTextFunction = [spec] (const juce::String& text)
        {
            return static_cast<double> (prism::parameters::parseValue (spec, text));
        };
    }

    // DELTA：MIX 旋钮中央的圆形监听开关。开时填充强调色、深色 ∆；关时强调色描边。
    deltaButton = std::make_unique<DeltaButton>();
    deltaButton->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    deltaButton->setColour (juce::TextButton::buttonOnColourId, accentColour);
    deltaButton->setColour (juce::TextButton::textColourOffId, accentColour);
    deltaButton->setColour (juce::TextButton::textColourOnId, backgroundColour);
    addAndMakeVisible (*deltaButton);

    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, prism::parameters::delta, *deltaButton);

    // LINK：INPUT 与 OUTPUT 之间的胶囊开关。开时强调色填充 + 深色字，关时强调色描边。
    linkButton = std::make_unique<LinkButton>();
    linkButton->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    linkButton->setColour (juce::TextButton::buttonOnColourId, accentColour);
    linkButton->setColour (juce::TextButton::textColourOffId, accentColour);
    linkButton->setColour (juce::TextButton::textColourOnId, backgroundColour);
    addAndMakeVisible (*linkButton);

    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, prism::parameters::link, *linkButton);

    // 先建好控件再定尺寸，setSize 会触发 resized()。
    setSize (720, 480);
    startTimerHz (kTimerHz);
}

PrismAudioProcessorEditor::~PrismAudioProcessorEditor()
{
    stopTimer();
}

void PrismAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    auto panel = getLocalBounds().toFloat().reduced (12.0f);
    g.setColour (panelColour);
    g.fillRoundedRectangle (panel, 10.0f);

    panel.removeFromTop (52.0f);

    // 字标按 17 px 高、原始宽高比缩放，贴在标题行左端；PRISM 从右侧 14 px 起。
    if (logo != nullptr)
    {
        const auto source = logo->getDrawableBounds();

        if (source.getWidth() > 0.0f && source.getHeight() > 0.0f)
        {
            const auto scale = logoBounds.getWidth() / source.getWidth();
            const auto transform = juce::AffineTransform::scale (scale)
                                       .translated (logoBounds.getX() - source.getX() * scale,
                                                    logoBounds.getY() - source.getY() * scale);
            logo->draw (g, 1.0f, transform);
        }
    }

    g.setColour (titleColour);
    g.setFont (juce::Font (juce::FontOptions { 30.0f, juce::Font::bold }));
    g.drawText ("PRISM", prismTextBounds, juce::Justification::centredLeft, false);

    g.setColour (hintColour);
    g.setFont (juce::Font (juce::FontOptions { 12.0f, juce::Font::plain }));
    g.drawText ("SEVEN-BAND OPTO COMPRESSOR", titleRowBounds, juce::Justification::centredRight, false);

    g.setColour (accentColour.withAlpha (0.45f));
    g.fillRect (panel.getX() + 20.0f, panel.getY() + 1.0f, panel.getWidth() - 40.0f, 1.0f);

    drawMeters (g);
}

void PrismAudioProcessorEditor::drawMeters (juce::Graphics& g) const
{
    auto area = meterArea;
    auto labels = area.removeFromBottom (18);

    const auto gap = 8.0f;
    const auto columnWidth = (static_cast<float> (area.getWidth()) - gap * (kNumBands - 1))
                             / static_cast<float> (kNumBands);

    for (int band = 0; band < kNumBands; ++band)
    {
        const auto& params = prism::kBands[static_cast<size_t> (band)];
        const auto colour = juce::Colour (params.colorArgb);
        const auto level = juce::jlimit (0.0f, 1.0f, displayedLevels[static_cast<size_t> (band)]);

        const juce::Rectangle<float> slot (static_cast<float> (area.getX()) + columnWidth * static_cast<float> (band) + gap * static_cast<float> (band),
                                          static_cast<float> (area.getY()),
                                          columnWidth,
                                          static_cast<float> (area.getHeight()));

        g.setColour (trackColour);
        g.fillRoundedRectangle (slot, 4.0f);

        // 0 电平时也画一层低亮度辉光：柱内自下而上约 5% 高度，顶部透明。
        // 有信号时真实电平条叠在辉光上，不会把辉光误认成信号。
        const auto glowHeight = slot.getHeight() * 0.05f;
        const auto glow = slot.withTop (slot.getBottom() - glowHeight).reduced (1.0f, 0.0f);

        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.30f),
                                                 glow.getX(), glow.getBottom(),
                                                 colour.withAlpha (0.0f),
                                                 glow.getX(), glow.getY(), false));
        g.fillRoundedRectangle (glow, 4.0f);

        const auto litHeight = std::max (2.0f, slot.getHeight() * level);
        auto lit = slot.withTop (slot.getBottom() - litHeight).reduced (1.0f, 0.0f);

        // 压缩越深越暗：光强高时柱体退到暗色，只留顶部亮边。
        const auto brightness = 0.30f + 0.70f * (1.0f - level);

        g.setColour (colour.withAlpha (0.16f));
        g.fillRoundedRectangle (lit.expanded (2.0f, 0.0f), 5.0f);

        g.setGradientFill (juce::ColourGradient (colour.withMultipliedBrightness (brightness).withAlpha (0.55f),
                                                 lit.getX(), lit.getBottom(),
                                                 colour.withMultipliedBrightness (brightness).brighter (0.5f).withAlpha (0.95f),
                                                 lit.getX(), lit.getY(), false));
        g.fillRoundedRectangle (lit, 4.0f);

        g.setColour (colour.brighter (0.35f).withAlpha (0.9f));
        g.fillRect (lit.getX() + 2.0f, lit.getY(), lit.getWidth() - 4.0f, 1.5f);

        g.setColour (hintColour);
        g.setFont (juce::Font (juce::FontOptions { 10.0f, juce::Font::plain }));
        g.drawText (juce::String::fromUTF8 (kBandRanges[band]),
                    labels.withX (static_cast<int> (slot.getX()))
                          .withWidth (static_cast<int> (columnWidth)),
                    juce::Justification::centred, false);
    }

    // 单位只标一次：标签行最右侧的暗淡 "Hz"，不在每个频段重复。
    auto unitArea = labels.removeFromRight (22);
    g.setColour (hintColour.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions { 12.0f, juce::Font::plain }));
    g.drawText ("Hz", unitArea, juce::Justification::centredRight, false);
}

void PrismAudioProcessorEditor::resized()
{
    auto inner = getLocalBounds().reduced (12);
    titleRowBounds = inner.removeFromTop (52).reduced (20, 0);

    constexpr float logoHeight = 17.0f;
    constexpr float logoAspect = 5.646f; // Assets/logo.svg: 73.237 / 12.969
    const auto logoWidth = logoHeight * logoAspect;

    // 字标是小写字重、带 g 的下伸部，墨量偏上；几何居中后视觉上比 PRISM 高约 2 px，
    // 因此下沉 2 px 做光学对齐（m01133）。
    constexpr float logoOpticalOffsetY = 2.0f;

    logoBounds = juce::Rectangle<float> (static_cast<float> (titleRowBounds.getX()),
                                         static_cast<float> (titleRowBounds.getCentreY()) - logoHeight * 0.5f
                                             + logoOpticalOffsetY,
                                         logoWidth,
                                         logoHeight);
    prismTextBounds = titleRowBounds.withTrimmedLeft (juce::roundToInt (logoWidth) + 14);

    meterArea = inner.removeFromTop (212).reduced (20, 6);

    auto knobRow = inner.removeFromBottom (176).reduced (16, 0);
    constexpr int kLinkSlotWidth = 56;
    const auto cellWidth = (knobRow.getWidth() - kLinkSlotWidth) / kNumKnobs;

    for (int i = 0; i < kNumKnobs; ++i)
    {
        // OUTPUT 前预留 56 px 放 LINK 胶囊，其余四列等宽。
        const auto columnX = knobRow.getX() + i * cellWidth
                             + (i == kNumKnobs - 1 ? kLinkSlotWidth : 0);
        auto cell = juce::Rectangle<int> (columnX, knobRow.getY(), cellWidth, knobRow.getHeight())
                        .reduced (4, 0);

        knobLabels[static_cast<size_t> (i)]->setBounds (cell.removeFromTop (18));
        knobs[static_cast<size_t> (i)]->setBounds (cell.withSizeKeepingCentre (108, 140));
    }

    const auto linkSlot = juce::Rectangle<int> (knobRow.getX() + 4 * cellWidth,
                                                knobRow.getY(),
                                                kLinkSlotWidth,
                                                knobRow.getHeight());
    const auto knobCentreY = knobs[0]->getBounds().withTrimmedBottom (18).getCentreY();
    linkButton->setBounds (juce::Rectangle<int> (44, 20)
                               .withCentre ({ linkSlot.getCentreX(), knobCentreY }));

    const auto mixRotary = knobs[2]->getBounds().withTrimmedBottom (18);
    deltaButton->setBounds (juce::Rectangle<int> (32, 32).withCentre (mixRotary.getCentre()));
}

void PrismAudioProcessorEditor::timerCallback()
{
    // 音频线程只做 relaxed store；这里同样 relaxed 读，不引入额外同步。
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto target = audioProcessor.meters()[static_cast<size_t> (band)]
                                .load (std::memory_order_relaxed);

        auto& shown = displayedLevels[static_cast<size_t> (band)];
        shown += 0.25f * (target - shown);
    }

    repaint (meterArea);
}
