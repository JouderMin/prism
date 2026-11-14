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

// spec 第 6 节：6 个连续参数 + DELTA 开关，顺序固定。
constexpr std::array<ExpectedParam, 7> kExpectedParams { {
    { "input", "INPUT", -12.0f, 24.0f, 0.0f },
    { "push", "PUSH", 0.0f, 24.0f, 0.0f },
    { "amount", "AMOUNT", 0.0f, 100.0f, 50.0f },
    { "spectrum", "SPECTRUM", -100.0f, 100.0f, 0.0f },
    { "mix", "MIX", 0.0f, 100.0f, 100.0f },
    { "output", "OUTPUT", -24.0f, 12.0f, 0.0f },
    { "delta", "DELTA", 0.0f, 1.0f, 0.0f },
} };

constexpr std::array<float, 7> kRoundTripValues { { 9.5f, 12.0f, 77.0f, -42.0f, 33.0f, -13.25f, 1.0f } };

// 读数格式：33.33 / 12.345 / -0.04 / 最小 / 最大 / 默认 的期望字符串。
struct FormatCase
{
    const char* id;
    std::array<float, 6> values;
    std::array<const char*, 6> expected;
};

constexpr std::array<FormatCase, 6> kFormatCases { {
    { "input",    { 33.33f, 12.345f, -0.04f, -12.0f, 24.0f, 0.0f },
                  { "33.3 dB", "12.3 dB", "0.0 dB", "-12.0 dB", "24.0 dB", "0.0 dB" } },
    { "push",     { 12.345f, 24.0f, -0.04f, 0.0f, 24.0f, 0.0f },
                  { "12.3 dB", "24.0 dB", "0.0 dB", "0.0 dB", "24.0 dB", "0.0 dB" } },
    { "amount",   { 33.33f, 12.345f, -0.04f, 0.0f, 100.0f, 50.0f },
                  { "33 %", "12 %", "0 %", "0 %", "100 %", "50 %" } },
    { "spectrum", { 33.33f, 12.345f, -0.04f, -100.0f, 100.0f, 0.0f },
                  { "+33 %", "+12 %", "0 %", "-100 %", "+100 %", "0 %" } },
    { "mix",      { 33.33f, 12.345f, -0.04f, 0.0f, 100.0f, 100.0f },
                  { "33 %", "12 %", "0 %", "0 %", "100 %", "100 %" } },
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

const prism::parameters::Spec* findSpec (const char* id)
{
    for (const auto& spec : prism::parameters::kSpecs)
        if (juce::String (spec.id) == juce::String (id))
            return &spec;

    return nullptr;
}

void removeStateChild (juce::ValueTree& state, const char* id)
{
    const auto child = state.getChildWithProperty (juce::Identifier ("id"), juce::String (id));

    if (child.isValid())
        state.removeChild (child, nullptr);
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
        beginTest ("parameter table: 6 continuous params + DELTA with IDs, ranges and defaults from spec 6");

        {
            PrismAudioProcessor processor;

            expectEquals (processor.getParameters().size(),
                          static_cast<int> (kExpectedParams.size()));

            const auto& parameters = processor.getParameters();
            for (size_t i = 0; i < kExpectedParams.size(); ++i)
                expectEquals (parameters[static_cast<int> (i)]->getName (64),
                              juce::String (kExpectedParams[i].name));

            // kSpecs 的顺序就是参数数组与界面的顺序。
            for (size_t i = 0; i < prism::parameters::kSpecs.size(); ++i)
                expectEquals (juce::String (prism::parameters::kSpecs[i].id),
                              juce::String (kExpectedParams[i].id));

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

            auto* push = processor.apvts.getParameter ("push");
            expect (push != nullptr);
            if (push != nullptr)
            {
                expectEquals (push->getText (0.0f, 0), juce::String ("0.0 dB"));
                expectEquals (push->getText (0.5f, 0), juce::String ("12.0 dB"));
                expectEquals (push->getText (1.0f, 0), juce::String ("24.0 dB"));
                expectWithinAbsoluteError (push->getNormalisableRange().interval, 0.1f, 1.0e-4f);
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

            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 4);
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

        beginTest ("APVTS state version is 4 and PUSH/DELTA are automatable");

        {
            PrismAudioProcessor processor;
            expectEquals (static_cast<int> (processor.apvts.state.getProperty ("version")), 4);

            auto* push = processor.apvts.getParameter ("push");
            auto* delta = processor.apvts.getParameter ("delta");
            expect (push != nullptr);
            expect (delta != nullptr);

            if (push != nullptr)
                expect (push->isAutomatable());
            if (delta != nullptr)
                expect (delta->isAutomatable());
        }

        beginTest ("version 1 state migrates with PUSH and DELTA off");

        {
            // 构造一个 v1 状态：没有 delta/push 子节点，版本属性为 1。
            PrismAudioProcessor legacy;
            legacy.apvts.state.setProperty (juce::Identifier ("version"), 1, nullptr);
            removeStateChild (legacy.apvts.state, "delta");
            removeStateChild (legacy.apvts.state, "push");

            juce::MemoryBlock legacyState;
            legacy.getStateInformation (legacyState);

            PrismAudioProcessor destination;
            setParameter (destination, "delta", 1.0f);
            setParameter (destination, "push", 12.0f);

            destination.setStateInformation (legacyState.getData(),
                                              static_cast<int> (legacyState.getSize()));

            expectSwitchOff (destination, "delta");
            expectSwitchOff (destination, "push");
            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 4);
        }

        beginTest ("version 2 state keeps DELTA and migrates PUSH off");

        {
            PrismAudioProcessor legacy;
            legacy.apvts.state.setProperty (juce::Identifier ("version"), 2, nullptr);
            setParameter (legacy, "amount", 64.0f);
            setParameter (legacy, "delta", 1.0f);
            removeStateChild (legacy.apvts.state, "push");

            juce::MemoryBlock legacyState;
            legacy.getStateInformation (legacyState);

            PrismAudioProcessor destination;
            setParameter (destination, "push", 12.0f);
            destination.setStateInformation (legacyState.getData(),
                                              static_cast<int> (legacyState.getSize()));

            expectSwitchOff (destination, "push");

            const auto* delta = destination.apvts.getRawParameterValue ("delta");
            const auto* amount = destination.apvts.getRawParameterValue ("amount");
            expect (delta != nullptr);
            expect (amount != nullptr);

            if (delta != nullptr)
                expectGreaterThan (delta->load(), 0.5f);
            if (amount != nullptr)
                expectWithinAbsoluteError (amount->load(), 64.0f, 1.0e-3f);

            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 4);
        }

        beginTest ("version 3 state keeps DELTA and migrates PUSH off");

        {
            PrismAudioProcessor legacy;
            legacy.apvts.state.setProperty (juce::Identifier ("version"), 3, nullptr);
            setParameter (legacy, "amount", 64.0f);
            setParameter (legacy, "delta", 1.0f);
            removeStateChild (legacy.apvts.state, "push");

            // v3 状态可能带已删除的 LINK 节点；迁移应移除它。
            juce::ValueTree linkNode ("PARAM");
            linkNode.setProperty ("id", "link", nullptr);
            linkNode.setProperty ("value", 1.0, nullptr);
            legacy.apvts.state.appendChild (linkNode, nullptr);

            juce::MemoryBlock legacyState;
            legacy.getStateInformation (legacyState);

            PrismAudioProcessor destination;
            setParameter (destination, "push", 12.0f);
            destination.setStateInformation (legacyState.getData(),
                                              static_cast<int> (legacyState.getSize()));

            expectSwitchOff (destination, "push");

            const auto* delta = destination.apvts.getRawParameterValue ("delta");
            const auto* amount = destination.apvts.getRawParameterValue ("amount");
            expect (delta != nullptr);
            expect (amount != nullptr);

            if (delta != nullptr)
                expectGreaterThan (delta->load(), 0.5f);
            if (amount != nullptr)
                expectWithinAbsoluteError (amount->load(), 64.0f, 1.0e-3f);

            const auto linkChild = destination.apvts.state.getChildWithProperty (
                juce::Identifier ("id"), juce::String ("link"));
            expect (! linkChild.isValid(), "migration must remove stale LINK state");

            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 4);
        }

        beginTest ("legacy v4 percentage PUSH migrates to 0 dB");

        {
            PrismAudioProcessor legacy;
            auto legacyTree = legacy.apvts.copyState();
            auto pushChild = legacyTree.getChildWithProperty (
                juce::Identifier ("id"), juce::String ("push"));
            expect (pushChild.isValid());

            if (pushChild.isValid())
                pushChild.setProperty ("value", 50.0, nullptr);

            // 直接序列化修改后的 ValueTree，避免 copyState 的 flush 把 50 覆盖回参数值。
            juce::MemoryBlock legacyState;
            if (const auto xml = legacyTree.createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, legacyState);

            PrismAudioProcessor destination;
            setParameter (destination, "push", 12.0f);
            destination.setStateInformation (legacyState.getData(),
                                              static_cast<int> (legacyState.getSize()));

            const auto* push = destination.apvts.getRawParameterValue ("push");
            expect (push != nullptr);
            if (push != nullptr)
                expectWithinAbsoluteError (push->load(), 0.0f, 1.0e-4f);

            expectEquals (static_cast<int> (destination.apvts.state.getProperty ("version")), 4);
        }

        beginTest ("value formatting: 0.1 dB, integer percent, SPECTRUM plus sign");

        {
            PrismAudioProcessor processor;

            for (const auto& formatCase : kFormatCases)
            {
                const auto* spec = findSpec (formatCase.id);
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
            const auto* spectrumSpec = findSpec ("spectrum");
            const auto* inputSpec = findSpec ("input");
            expect (spectrumSpec != nullptr);
            expect (inputSpec != nullptr);

            if (spectrumSpec != nullptr)
            {
                expectWithinAbsoluteError (prism::parameters::parseValue (*spectrumSpec, "+25 %"), 25.0f, 1.0e-5f);
                expectWithinAbsoluteError (prism::parameters::parseValue (*spectrumSpec, "-50 %"), -50.0f, 1.0e-5f);
            }

            if (inputSpec != nullptr)
            {
                expectWithinAbsoluteError (prism::parameters::parseValue (*inputSpec, "3.7 dB"), 3.7f, 1.0e-5f);
                expectWithinAbsoluteError (prism::parameters::parseValue (*inputSpec, "+12.0 dB"), 12.0f, 1.0e-5f);
            }
        }
    }

private:
    void expectSwitchOff (PrismAudioProcessor& processor, const char* id)
    {
        const auto* value = processor.apvts.getRawParameterValue (id);
        expect (value != nullptr, juce::String ("missing raw value ") + id);

        if (value != nullptr)
            expectLessThan (value->load(), 0.5f);
    }
};

static ParamTests paramTests;
