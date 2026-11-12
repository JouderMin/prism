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

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{

constexpr int kEditorWidth = 720;
constexpr int kEditorHeight = 480;

} // namespace

// 界面用例：开关编辑器三次、检查尺寸与旋钮绑定。
// 真正的绘制与拖动由视觉/宿主验证覆盖；这里守住生命周期与 attachment 初值。
class EditorTests final : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("PRISM Editor", "prism") {}

    void runTest() override
    {
        beginTest ("editor opens, binds knobs and closes repeatedly");

        PrismAudioProcessor processor;

        for (int pass = 0; pass < 3; ++pass)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            expect (editor != nullptr);

            if (editor == nullptr)
                return;

            expectEquals (editor->getWidth(), kEditorWidth);
            expectEquals (editor->getHeight(), kEditorHeight);

            int sliderCount = 0;
            juce::Slider* amountKnob = nullptr;
            juce::Slider* spectrumKnob = nullptr;
            juce::Slider* inputKnob = nullptr;

            for (int i = 0; i < editor->getNumChildComponents(); ++i)
            {
                if (auto* slider = dynamic_cast<juce::Slider*> (editor->getChildComponent (i)))
                {
                    ++sliderCount;

                    if (slider->getName() == "AMOUNT")
                        amountKnob = slider;

                    if (slider->getName() == "SPECTRUM")
                        spectrumKnob = slider;

                    if (slider->getName() == "INPUT")
                        inputKnob = slider;
                }
            }

            expectEquals (sliderCount, 5);
            expect (amountKnob != nullptr);

            if (amountKnob != nullptr)
            {
                expectWithinAbsoluteError (amountKnob->getValue(), 50.0, 1.0e-3);
                expectEquals (amountKnob->getTextFromValue (amountKnob->getValue()),
                              juce::String ("50 %"));
            }

            expect (spectrumKnob != nullptr);

            if (spectrumKnob != nullptr)
            {
                expectEquals (spectrumKnob->getTextFromValue (25.0), juce::String ("+25 %"));
                expectEquals (spectrumKnob->getTextFromValue (0.0), juce::String ("0 %"));
                expectEquals (spectrumKnob->getTextFromValue (-50.0), juce::String ("-50 %"));
                expectEquals (spectrumKnob->getTextFromValue (33.33), juce::String ("+33 %"));
            }

            // SliderAttachment 构造后会覆盖格式化函数；这里确认编辑器在附件之后
            // 重新装上了 parameters::formatValue，且输入文本也走同一条路径。
            expect (inputKnob != nullptr);

            if (inputKnob != nullptr)
            {
                expectEquals (inputKnob->getTextFromValue (3.7), juce::String ("3.7 dB"));
                expectEquals (inputKnob->getTextFromValue (33.33), juce::String ("33.3 dB"));
                expectEquals (inputKnob->getTextFromValue (-12.0), juce::String ("-12.0 dB"));
                expectWithinAbsoluteError (inputKnob->valueFromTextFunction ("3.7 dB"), 3.7, 1.0e-4);
            }

            editor.reset();
        }

        beginTest ("editor snapshot is written as 720x480 PNG");

        // 截图前把三个读数改成非默认值，便于用快照核对格式。
        const auto setPlainValue = [&processor] (const char* id, float plainValue)
        {
            if (auto* parameter = processor.apvts.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        };

        setPlainValue ("input", 3.7f);
        setPlainValue ("spectrum", 25.0f);
        setPlainValue ("amount", 37.0f);

        std::unique_ptr<juce::AudioProcessorEditor> snapshotEditor (processor.createEditor());
        expect (snapshotEditor != nullptr);

        if (snapshotEditor == nullptr)
            return;

        for (int i = 0; i < snapshotEditor->getNumChildComponents(); ++i)
        {
            auto* slider = dynamic_cast<juce::Slider*> (snapshotEditor->getChildComponent (i));

            if (slider == nullptr)
                continue;

            if (slider->getName() == "AMOUNT")
                expectEquals (slider->getTextFromValue (slider->getValue()), juce::String ("37 %"));

            if (slider->getName() == "SPECTRUM")
                expectEquals (slider->getTextFromValue (slider->getValue()), juce::String ("+25 %"));

            if (slider->getName() == "INPUT")
                expectEquals (slider->getTextFromValue (slider->getValue()), juce::String ("3.7 dB"));
        }

        const auto image = snapshotEditor->createComponentSnapshot (snapshotEditor->getLocalBounds());

        expect (image.isValid());
        expectEquals (image.getWidth(), kEditorWidth);
        expectEquals (image.getHeight(), kEditorHeight);

        const auto snapshotFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                      .getParentDirectory()
                                      .getChildFile ("editor-snapshot.png");

        snapshotFile.deleteFile();

        juce::FileOutputStream stream (snapshotFile);
        expect (stream.openedOk());

        juce::PNGImageFormat png;
        expect (png.writeImageToStream (image, stream));
        stream.flush();

        expect (snapshotFile.existsAsFile());
        expectGreaterThan (snapshotFile.getSize(), static_cast<juce::int64> (0));

        const auto reloaded = juce::ImageFileFormat::loadFrom (snapshotFile);
        expectEquals (reloaded.getWidth(), kEditorWidth);
        expectEquals (reloaded.getHeight(), kEditorHeight);

        logMessage ("editor snapshot: " + snapshotFile.getFullPathName()
                    + " (" + juce::String (snapshotFile.getSize()) + " bytes)");
    }
};

static EditorTests editorTests;
