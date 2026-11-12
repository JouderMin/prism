#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

// PRISM 的 5 个参数：ID、范围、默认值集中在这里。
// 曲线是编译期常量（BandTable.h），参数只改驱动量与配比，不改变曲线形状。
namespace prism
{

namespace parameters
{

// 参数 ID 是状态文件与自动化里的稳定键，不要改。
inline constexpr const char* amount = "amount";
inline constexpr const char* spectrum = "spectrum";
inline constexpr const char* mix = "mix";
inline constexpr const char* input = "input";
inline constexpr const char* output = "output";

struct Spec
{
    const char* id;
    const char* name;
    float minValue;
    float maxValue;
    float defaultValue;
    const char* suffix;
    int decimals;
};

// 顺序即界面顺序：AMOUNT、SPECTRUM、MIX、INPUT、OUTPUT。
// 范围与默认值照 spec 第 6 节。
inline constexpr std::array<Spec, 5> kSpecs { {
    { amount, "AMOUNT", 0.0f, 100.0f, 50.0f, " %", 0 },
    { spectrum, "SPECTRUM", -100.0f, 100.0f, 0.0f, "", 0 },
    { mix, "MIX", 0.0f, 100.0f, 100.0f, " %", 0 },
    { input, "INPUT", -12.0f, 24.0f, 0.0f, " dB", 1 },
    { output, "OUTPUT", -24.0f, 12.0f, 0.0f, " dB", 1 },
} };

} // namespace parameters

// 建 5 个 AudioParameterFloat。用 make_unique 转移所有权，不写裸 new/delete。
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : parameters::kSpecs)
    {
        const juce::NormalisableRange<float> range (spec.minValue, spec.maxValue);
        const juce::String suffix (spec.suffix);
        const int decimals = spec.decimals;

        auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [suffix, decimals] (float value, int)
            {
                return juce::String (value, decimals) + suffix;
            });

        layout.add (std::make_unique<juce::AudioParameterFloat> (spec.id,
                                                                spec.name,
                                                                range,
                                                                spec.defaultValue,
                                                                attributes));
    }

    return layout;
}

} // namespace prism
