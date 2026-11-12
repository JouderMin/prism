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

            for (int i = 0; i < editor->getNumChildComponents(); ++i)
            {
                if (auto* slider = dynamic_cast<juce::Slider*> (editor->getChildComponent (i)))
                {
                    ++sliderCount;

                    if (slider->getName() == "AMOUNT")
                        amountKnob = slider;

                    if (slider->getName() == "SPECTRUM")
                        spectrumKnob = slider;
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
            }

            editor.reset();
        }

        beginTest ("editor snapshot is written as 720x480 PNG");

        std::unique_ptr<juce::AudioProcessorEditor> snapshotEditor (processor.createEditor());
        expect (snapshotEditor != nullptr);

        if (snapshotEditor == nullptr)
            return;

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
