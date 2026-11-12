#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"

#include <array>

namespace
{

struct ExpectedParam
{
    const char* id;
    const char* name;
    float minValue;
    float maxValue;
    float defaultValue;
};

// spec 第 6 节的 5 个参数，顺序固定。
constexpr std::array<ExpectedParam, 5> kExpectedParams { {
    { "amount", "AMOUNT", 0.0f, 100.0f, 50.0f },
    { "spectrum", "SPECTRUM", -100.0f, 100.0f, 0.0f },
    { "mix", "MIX", 0.0f, 100.0f, 100.0f },
    { "input", "INPUT", -12.0f, 24.0f, 0.0f },
    { "output", "OUTPUT", -24.0f, 12.0f, 0.0f },
} };

constexpr std::array<float, 5> kRoundTripValues { { 77.0f, -42.0f, 33.0f, 9.5f, -13.25f } };

void setParameter (PrismAudioProcessor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.apvts.getParameter (id);

    if (parameter == nullptr)
        return;

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

juce::AudioProcessor::BusesLayout makeLayout (const juce::AudioChannelSet& input,
                                              const juce::AudioChannelSet& output)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (input);
    layout.outputBuses.add (output);
    return layout;
}

} // namespace

class ParamTests final : public juce::UnitTest
{
public:
    ParamTests()
        : juce::UnitTest ("PRISM Parameters", "prism")
    {
    }

    void runTest() override
    {
        beginTest ("parameter table: 5 params with IDs, ranges and defaults from spec 6");

        {
            PrismAudioProcessor processor;

            expectEquals (processor.getParameters().size(),
                          static_cast<int> (kExpectedParams.size()));

            for (const auto& expected : kExpectedParams)
            {
                auto* parameter = processor.apvts.getParameter (expected.id);
                expect (parameter != nullptr, juce::String ("missing parameter ") + expected.id);

                if (parameter == nullptr)
                    continue;

                const auto range = parameter->getNormalisableRange();
                expectWithinAbsoluteError (range.start, expected.minValue, 1.0e-4f);
                expectWithinAbsoluteError (range.end, expected.maxValue, 1.0e-4f);

                const auto plainDefault = parameter->convertFrom0to1 (parameter->getDefaultValue());
                expectWithinAbsoluteError (plainDefault, expected.defaultValue, 1.0e-4f);

                expectEquals (parameter->getName (64), juce::String (expected.name));
            }
        }

        beginTest ("state round-trip preserves all five parameter values");

        {
            PrismAudioProcessor source;

            for (size_t i = 0; i < kExpectedParams.size(); ++i)
                setParameter (source, kExpectedParams[i].id, kRoundTripValues[i]);

            juce::MemoryBlock state;
            source.getStateInformation (state);
            expect (state.getSize() > 0, "state must not be empty");

            PrismAudioProcessor destination;
            destination.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

            juce::String readBack;

            for (size_t i = 0; i < kExpectedParams.size(); ++i)
            {
                const auto* value = destination.apvts.getRawParameterValue (kExpectedParams[i].id);
                expect (value != nullptr,
                        juce::String ("missing raw value ") + kExpectedParams[i].id);

                if (value != nullptr)
                {
                    readBack += juce::String (kExpectedParams[i].id) + "="
                              + juce::String (value->load(), 3) + " ";
                    expectWithinAbsoluteError (value->load(), kRoundTripValues[i], 1.0e-3f);
                }
            }

            logMessage ("round-trip values: " + readBack.trim());

            // 版本属性也要随状态一起往返。
            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 1);
        }

        beginTest ("bus layout: stereo in/out accepted, mono rejected");

        {
            PrismAudioProcessor processor;

            const auto stereoIn = juce::AudioChannelSet::stereo();
            const auto stereoOut = juce::AudioChannelSet::stereo();
            const auto mono = juce::AudioChannelSet::mono();

            expect (processor.isBusesLayoutSupported (makeLayout (stereoIn, stereoOut)),
                    "stereo in/out must be accepted");

            expect (! processor.isBusesLayoutSupported (makeLayout (mono, stereoOut)),
                    "mono input must be rejected");

            expect (! processor.isBusesLayoutSupported (makeLayout (stereoIn, mono)),
                    "mono output must be rejected");

            expect (! processor.isBusesLayoutSupported (makeLayout (mono, mono)),
                    "mono in/out must be rejected");
        }

        beginTest ("APVTS state version is 1");

        {
            PrismAudioProcessor processor;
            expectEquals (static_cast<int> (processor.apvts.state.getProperty ("version")), 1);
        }
    }
};

static ParamTests paramTests;
