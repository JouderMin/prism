#include "PluginEditor.h"

#include "PluginProcessor.h"

namespace
{
const juce::Colour backgroundColour { 0xff0b0b0f };
const juce::Colour panelColour      { 0xff14141a };
const juce::Colour titleColour      { 0xffe8e6df };
const juce::Colour accentColour     { 0xffe2a33c };
const juce::Colour hintColour       { 0xff5a5a66 };
} // namespace

PrismAudioProcessorEditor::PrismAudioProcessorEditor (PrismAudioProcessor& processor)
    : AudioProcessorEditor (&processor)
{
    setOpaque (true);
    setResizable (false, false);
    setSize (720, 480);
}

void PrismAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // 深色底 + 稍亮的顶层面板。
    g.fillAll (backgroundColour);

    auto panel = bounds.reduced (16.0f);
    g.setColour (panelColour);
    g.fillRoundedRectangle (panel, 10.0f);

    g.setColour (accentColour.withAlpha (0.85f));
    g.fillRect (panel.getX() + 32.0f, panel.getY() + 40.0f, panel.getWidth() - 64.0f, 1.0f);

    // 标题。
    g.setColour (titleColour);
    g.setFont (juce::Font (juce::FontOptions { 72.0f, juce::Font::bold }));
    g.drawText ("PRISM", panel.withTrimmedTop (120.0f).withHeight (96.0f),
                juce::Justification::centred, false);

    // 副标题。
    g.setColour (hintColour);
    g.setFont (juce::Font (juce::FontOptions { 15.0f, juce::Font::plain }));
    g.drawText ("SEVEN-BAND OPTO COMPRESSOR",
                panel.withTrimmedTop (232.0f).withHeight (24.0f),
                juce::Justification::centred, false);

    // 版本角标。
    g.setFont (juce::Font (juce::FontOptions { 12.0f, juce::Font::plain }));
    g.drawText ("M1 passthrough  v0.1.0",
                panel.removeFromBottom (28.0f).reduced (16.0f, 0.0f),
                juce::Justification::bottomRight, false);
}

void PrismAudioProcessorEditor::resized()
{
    // M1 无子控件；后续任务在这里布局。
}
