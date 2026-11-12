#include "PluginEditor.h"

#include <cmath>

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

// 频段文字标签，从左到右与 kBands 对齐。
const char* const kBandRanges[7] = { "20-80", "80-200", "200-500", "500-1.2k",
                                     "1.2-3k", "3-8k", "8k+" };

void configureKnob (juce::Slider& knob)
{
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

PrismAudioProcessorEditor::PrismAudioProcessorEditor (PrismAudioProcessor& processorToEdit)
    : AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    setOpaque (true);
    setResizable (false, false);

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

        // 数值读数直接用参数自己的格式化（含单位），SliderAttachment 只负责值。
        if (auto* parameter = audioProcessor.apvts.getParameter (spec.id))
        {
            knob->textFromValueFunction = [parameter] (double value)
            {
                return parameter->getText (static_cast<float> (value), 0);
            };
        }

        addAndMakeVisible (*knob);
        knobs[static_cast<size_t> (i)] = std::move (knob);

        attachments[static_cast<size_t> (i)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                audioProcessor.apvts, spec.id, *knobs[static_cast<size_t> (i)]);
    }

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

    auto title = panel.removeFromTop (52.0f).reduced (20.0f, 0.0f);

    g.setColour (titleColour);
    g.setFont (juce::Font (juce::FontOptions { 30.0f, juce::Font::bold }));
    g.drawText ("PRISM", title, juce::Justification::centredLeft, false);

    g.setColour (hintColour);
    g.setFont (juce::Font (juce::FontOptions { 12.0f, juce::Font::plain }));
    g.drawText ("SEVEN-BAND OPTO COMPRESSOR", title, juce::Justification::centredRight, false);

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
        g.drawText (kBandRanges[band],
                    labels.withX (static_cast<int> (slot.getX()))
                          .withWidth (static_cast<int> (columnWidth)),
                    juce::Justification::centred, false);
    }
}

void PrismAudioProcessorEditor::resized()
{
    auto inner = getLocalBounds().reduced (12);
    inner.removeFromTop (52);

    meterArea = inner.removeFromTop (212).reduced (20, 6);

    auto knobRow = inner.removeFromBottom (176).reduced (16, 0);
    const auto cellWidth = knobRow.getWidth() / kNumKnobs;

    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto cell = knobRow.withX (knobRow.getX() + i * cellWidth).withWidth (cellWidth).reduced (4, 0);

        knobLabels[static_cast<size_t> (i)]->setBounds (cell.removeFromTop (18));
        knobs[static_cast<size_t> (i)]->setBounds (cell.withSizeKeepingCentre (108, 140));
    }
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
