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

// spec 第 6 节：5 个连续参数 + LINK/DELTA 两个开关，顺序固定。
constexpr std::array<ExpectedParam, 7> kExpectedParams { {
    { "amount", "AMOUNT", 0.0f, 100.0f, 50.0f },
    { "spectrum", "SPECTRUM", -100.0f, 100.0f, 0.0f },
    { "mix", "MIX", 0.0f, 100.0f, 100.0f },
    { "input", "INPUT", -12.0f, 24.0f, 0.0f },
    { "link", "LINK", 0.0f, 1.0f, 0.0f },
    { "output", "OUTPUT", -24.0f, 12.0f, 0.0f },
    { "delta", "DELTA", 0.0f, 1.0f, 0.0f },
} };

constexpr std::array<float, 7> kRoundTripValues { { 77.0f, -42.0f, 33.0f, 9.5f, 1.0f, -13.25f, 1.0f } };

// 读数格式：33.33 / 12.345 / -0.04 / 最小 / 最大 / 默认 的期望字符串。
// 33.33 对 INPUT/OUTPUT 超出范围，只用于 formatValue 纯函数；参数 getText 会先夹取。
struct FormatCase
{
    const char* id;
    std::array<float, 6> values;
    std::array<const char*, 6> expected;
};

constexpr std::array<FormatCase, 5> kFormatCases { {
    { "amount",   { 33.33f, 12.345f, -0.04f, 0.0f, 100.0f, 50.0f },
                  { "33 %", "12 %", "0 %", "0 %", "100 %", "50 %" } },
    { "spectrum", { 33.33f, 12.345f, -0.04f, -100.0f, 100.0f, 0.0f },
                  { "+33 %", "+12 %", "0 %", "-100 %", "+100 %", "0 %" } },
    { "mix",      { 33.33f, 12.345f, -0.04f, 0.0f, 100.0f, 100.0f },
                  { "33 %", "12 %", "0 %", "0 %", "100 %", "100 %" } },
    { "input",    { 33.33f, 12.345f, -0.04f, -12.0f, 24.0f, 0.0f },
                  { "33.3 dB", "12.3 dB", "0.0 dB", "-12.0 dB", "24.0 dB", "0.0 dB" } },
    { "output",   { 33.33f, 12.345f, -0.04f, -24.0f, 12.0f, 0.0f },
                  { "33.3 dB", "12.3 dB", "0.0 dB", "-24.0 dB", "12.0 dB", "0.0 dB" } },
} };

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
        beginTest ("parameter table: 5 continuous params + LINK/DELTA with IDs, ranges and defaults from spec 6");

        {
            PrismAudioProcessor processor;

            expectEquals (processor.getParameters().size(),
                          static_cast<int> (kExpectedParams.size()));

            const auto& parameters = processor.getParameters();
            for (size_t i = 0; i < kExpectedParams.size(); ++i)
                expectEquals (parameters[static_cast<int> (i)]->getName (64),
                              juce::String (kExpectedParams[i].name));

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

        beginTest ("state round-trip preserves all seven parameter values");

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

            // 版本属性也要随状态一起往返；LINK 是 v3 新增。
            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 3);
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

        beginTest ("APVTS state version is 3 and LINK/DELTA are automatable");

        {
            PrismAudioProcessor processor;
            expectEquals (static_cast<int> (processor.apvts.state.getProperty ("version")), 3);

            auto* delta = processor.apvts.getParameter ("delta");
            auto* link = processor.apvts.getParameter ("link");
            expect (delta != nullptr);
            expect (link != nullptr);

            if (delta != nullptr)
                expect (delta->isAutomatable());
            if (link != nullptr)
                expect (link->isAutomatable());
        }

        beginTest ("version 1 state migrates with DELTA and LINK off");

        {
            // 构造一个 v1 状态：没有 delta/link 子节点，版本属性为 1。
            PrismAudioProcessor legacy;
            legacy.apvts.state.setProperty (juce::Identifier ("version"), 1, nullptr);

            for (const auto* id : { "delta", "link" })
            {
                auto child = legacy.apvts.state.getChildWithProperty (
                    juce::Identifier ("id"), juce::String (id));

                if (child.isValid())
                    legacy.apvts.state.removeChild (child, nullptr);
            }

            juce::MemoryBlock legacyState;
            legacy.getStateInformation (legacyState);

            PrismAudioProcessor destination;
            setParameter (destination, "delta", 1.0f);
            setParameter (destination, "link", 1.0f);

            destination.setStateInformation (legacyState.getData(),
                                              static_cast<int> (legacyState.getSize()));

            for (const auto* id : { "delta", "link" })
            {
                const auto* value = destination.apvts.getRawParameterValue (id);
                expect (value != nullptr);
                if (value != nullptr)
                    expectLessThan (value->load(), 0.5f);
            }

            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 3);
        }

        beginTest ("version 2 state migrates LINK off without losing other values");

        {
            // v2 状态有 delta、没有 link；迁移后 delta/link 都回落 false，其它值保留。
            PrismAudioProcessor legacy;
            legacy.apvts.state.setProperty (juce::Identifier ("version"), 2, nullptr);
            setParameter (legacy, "amount", 64.0f);
            setParameter (legacy, "delta", 1.0f);

            auto linkChild = legacy.apvts.state.getChildWithProperty (
                juce::Identifier ("id"), juce::String ("link"));

            if (linkChild.isValid())
                legacy.apvts.state.removeChild (linkChild, nullptr);

            juce::MemoryBlock legacyState;
            legacy.getStateInformation (legacyState);

            PrismAudioProcessor destination;
            setParameter (destination, "link", 1.0f);
            destination.setStateInformation (legacyState.getData(),
                                              static_cast<int> (legacyState.getSize()));

            const auto* linkValue = destination.apvts.getRawParameterValue ("link");
            const auto* deltaValue = destination.apvts.getRawParameterValue ("delta");
            const auto* amountValue = destination.apvts.getRawParameterValue ("amount");
            expect (linkValue != nullptr);
            expect (deltaValue != nullptr);
            expect (amountValue != nullptr);

            if (linkValue != nullptr)
                expectLessThan (linkValue->load(), 0.5f);
            if (deltaValue != nullptr)
                expectLessThan (deltaValue->load(), 0.5f);
            if (amountValue != nullptr)
                expectWithinAbsoluteError (amountValue->load(), 64.0f, 1.0e-3f);

            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 3);
        }

        beginTest ("value formatting: 0.1 dB, integer percent, SPECTRUM plus sign");

        {
            PrismAudioProcessor processor;

            for (const auto& formatCase : kFormatCases)
            {
                const prism::parameters::Spec* spec = nullptr;

                for (const auto& candidate : prism::parameters::kSpecs)
                    if (juce::String (candidate.id) == juce::String (formatCase.id))
                        spec = &candidate;

                expect (spec != nullptr, juce::String ("missing spec ") + formatCase.id);

                if (spec == nullptr)
                    continue;

                auto* parameter = processor.apvts.getParameter (formatCase.id);
                expect (parameter != nullptr, juce::String ("missing parameter ") + formatCase.id);

                if (parameter == nullptr)
                    continue;

                for (size_t i = 0; i < formatCase.values.size(); ++i)
                {
                    const auto value = formatCase.values[i];
                    const auto expected = juce::String (formatCase.expected[i]);
                    const auto context = juce::String (formatCase.id) + " @ "
                                       + juce::String (value, 3);

                    expectEquals (prism::parameters::formatValue (*spec, value),
                                  expected, context);

                    // 参数文本走同一条路径；超出范围时由 NormalisableRange 夹到端点。
                    const auto clamped = juce::jlimit (spec->minValue, spec->maxValue, value);
                    expectEquals (parameter->getText (parameter->convertTo0to1 (value), 0),
                                  prism::parameters::formatValue (*spec, clamped),
                                  context + " (parameter text)");
                }
            }

            // 文本回读：单位与 "+" 都要能吃进去。
            const auto& spectrumSpec = prism::parameters::kSpecs[1];
            const auto& inputSpec = prism::parameters::kSpecs[3];

            expectWithinAbsoluteError (prism::parameters::parseValue (inputSpec, "3.7 dB"), 3.7f, 1.0e-5f);
            expectWithinAbsoluteError (prism::parameters::parseValue (inputSpec, "+12.0 dB"), 12.0f, 1.0e-5f);
            expectWithinAbsoluteError (prism::parameters::parseValue (spectrumSpec, "+25 %"), 25.0f, 1.0e-5f);
            expectWithinAbsoluteError (prism::parameters::parseValue (spectrumSpec, "-50 %"), -50.0f, 1.0e-5f);
        }
    }
};

static ParamTests paramTests;
