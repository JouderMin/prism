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

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>
#include <memory>

// PRISM 的 6 个连续参数 + DELTA/CLIP 开关：ID、范围、默认值集中在这里。
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
inline constexpr const char* push = "push";
inline constexpr const char* delta = "delta";
inline constexpr const char* clip = "clip";

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

// 顺序即界面顺序：INPUT、PUSH、AMOUNT、SPECTRUM、MIX、OUTPUT；DELTA/CLIP 是开关。
// 范围与默认值照 spec 第 6 节。
inline constexpr std::array<Spec, 6> kSpecs { {
    { input, "INPUT", -12.0f, 24.0f, 0.0f, " dB", 1 },
    { push, "PUSH", 0.0f, 24.0f, 0.0f, " dB", 1 },
    { amount, "AMOUNT", 0.0f, 100.0f, 50.0f, " %", 0 },
    { spectrum, "SPECTRUM", -100.0f, 100.0f, 0.0f, " %", 0 },
    { mix, "MIX", 0.0f, 100.0f, 100.0f, " %", 0 },
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

// 建 6 个 AudioParameterFloat + 2 个 AudioParameterBool（DELTA、CLIP）。
// 用 make_unique 转移所有权，不写裸 new/delete。
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : parameters::kSpecs)
    {
        // PUSH 以 0.1 dB 为步进；其余连续参数保持默认连续范围，只在显示层取整。
        const auto interval = juce::String (spec.id) == juce::String (parameters::push)
                                  ? 0.1f
                                  : 0.0f;
        const juce::NormalisableRange<float> range (spec.minValue, spec.maxValue, interval);

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

    // DELTA 只切换监听差信号，不进入音频曲线；默认关，随状态保存。
    layout.add (std::make_unique<juce::AudioParameterBool> (parameters::delta,
                                                            "DELTA",
                                                            false));

    // CLIP 在 PUSH 之后插入 tanh 软削波；默认关，随状态保存。
    layout.add (std::make_unique<juce::AudioParameterBool> (parameters::clip,
                                                            "CLIP",
                                                            false));

    return layout;
}

} // namespace prism
