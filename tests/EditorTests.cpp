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

            for (int i = 0; i < editor->getNumChildComponents(); ++i)
            {
                if (auto* slider = dynamic_cast<juce::Slider*> (editor->getChildComponent (i)))
                {
                    ++sliderCount;

                    if (slider->getName() == "AMOUNT")
                        amountKnob = slider;
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

            editor.reset();
        }
    }
};

static EditorTests editorTests;
