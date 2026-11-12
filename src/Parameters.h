#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>
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
    { spectrum, "SPECTRUM", -100.0f, 100.0f, 0.0f, " %", 0 },
    { mix, "MIX", 0.0f, 100.0f, 100.0f, " %", 0 },
    { input, "INPUT", -12.0f, 24.0f, 0.0f, " dB", 1 },
    { output, "OUTPUT", -24.0f, 12.0f, 0.0f, " dB", 1 },
} };

// 唯一的数值格式化路径：参数 getText 与界面旋钮都调用它。
// 规则：按 Spec::decimals 四舍五入，dB 固定 1 位、百分比固定 0 位；
// 只有 SPECTRUM 的正值带 "+"；-0.0 归一成 0，避免 "-0.0 dB"。
inline juce::String formatValue (const Spec& spec, float value)
{
    const auto scale = std::pow (10.0, static_cast<double> (spec.decimals));
    auto rounded = std::round (static_cast<double> (value) * scale) / scale;

    if (std::abs (rounded) < 1.0e-12)
        rounded = 0.0;

    const bool showPlus = juce::String (spec.id) == juce::String (spectrum);
    const auto sign = showPlus && rounded > 0.0 ? juce::String ("+") : juce::String();
    return sign + juce::String (rounded, spec.decimals) + juce::String (spec.suffix);
}

// 文本回读：去掉单位后缀与 "+"，按 Spec 范围夹取。
inline float parseValue (const Spec& spec, const juce::String& text)
{
    auto trimmed = text.trim();
    const auto trimmedSuffix = juce::String (spec.suffix != nullptr ? spec.suffix : "").trim();

    if (trimmedSuffix.isNotEmpty() && trimmed.endsWithIgnoreCase (trimmedSuffix))
        trimmed = trimmed.dropLastCharacters (trimmedSuffix.length()).trim();

    if (trimmed.startsWithChar ('+'))
        trimmed = trimmed.substring (1);

    return juce::jlimit (spec.minValue, spec.maxValue, trimmed.getFloatValue());
}

} // namespace parameters

// 建 5 个 AudioParameterFloat。用 make_unique 转移所有权，不写裸 new/delete。
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : parameters::kSpecs)
    {
        const juce::NormalisableRange<float> range (spec.minValue, spec.maxValue);

        // 参数文本与界面文本共用 formatValue / parseValue，避免两套小数位规则。
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withStringFromValueFunction ([spec] (float value, int)
                              {
                                  return parameters::formatValue (spec, value);
                              })
                              .withValueFromStringFunction ([spec] (const juce::String& text)
                              {
                                  return parameters::parseValue (spec, text);
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
