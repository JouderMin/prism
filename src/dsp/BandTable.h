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

#include <array>
#include <cstdint>

namespace prism
{

/** 每段固定的光电参数，数值照 spec 5.4。

    sensitivityDb 是每段光敏电阻的标定 trim：让 −18 → −6 dBFS 的静态斜率
    落在 targetSlope ±0.5 内。标定值由 48 kHz、1 kHz 正弦、稳态 RMS 实测得到。

    拓扑说明（spec 5.3 修订版）：检测器取带内输入（前馈），增益
    g = (R_s + R) / (R_s + R_dark)，暗态为 1、全亮趋近 g_min。
    初版 spec 的 g = R_s/(R_s + R) + 反馈方向相反，实测静态曲线是 0.96:1 的
    轻微膨胀，与 5.4 的 1.5–3.0:1 压缩目标矛盾；已按队长裁决改为前馈反向增益。
*/
struct BandParams
{
    const char* colorName; // ASCII 名，UI 侧自行映射中文；红/橙/黄/绿/青/蓝/紫
    std::uint32_t colorArgb;
    int wavelengthNm;
    float tauLdrSeconds;
    // 设计目标，不是当前实现的独立时间常数：实际释放形状 = 检测器 60 ms
    // 与 τ_ldr 一阶跟随叠加；这两个字段留作 M4 双时间常数释放的设计参考。
    float releaseFastMs;
    float releaseTailMs;
    float gamma;
    float targetSlope;
    float sensitivityDb;
};

/** 检测器固定时间常数（spec 5.3）。 */
inline constexpr float kDetectorAttackMs = 10.0f;
inline constexpr float kDetectorReleaseMs = 60.0f;

/** 光强与光敏电阻的固定常量（spec 5.3）。 */
inline constexpr float kReferenceDbfs = -18.0f;
inline constexpr float kReferenceLevel = 0.12589254117941673f; // 10^(−18/20)
inline constexpr float kLightExponent = 1.6f;
inline constexpr float kDarkResistanceOhm = 150.0e3f;
inline constexpr float kSeriesResistanceOhm = 10.0e3f;
inline constexpr float kMinGain = kSeriesResistanceOhm / (kSeriesResistanceOhm + kDarkResistanceOhm);

inline constexpr std::array<BandParams, 7> kBands = { {
    { "red", 0xffe05a4f, 700, 0.080f, 120.0f, 2500.0f, 0.90f, 1.5f, -18.14f },
    { "orange", 0xffe8913a, 620, 0.065f, 100.0f, 1800.0f, 0.95f, 1.8f, -14.33f },
    { "yellow", 0xffe3c34a, 580, 0.050f, 90.0f, 1400.0f, 1.00f, 2.0f, -13.05f },
    { "green", 0xff5fbf7a, 530, 0.038f, 75.0f, 1000.0f, 1.05f, 2.2f, -12.22f },
    { "cyan", 0xff4fbfc4, 500, 0.028f, 60.0f, 700.0f, 1.10f, 2.5f, -11.26f },
    { "blue", 0xff5a7fd6, 470, 0.018f, 45.0f, 500.0f, 1.15f, 2.8f, -10.72f },
    { "violet", 0xffa06bd6, 420, 0.010f, 30.0f, 350.0f, 1.20f, 3.0f, -10.67f },
} };

static_assert (kBands.size() == 7, "spec 5.4 固定 7 段");

} // namespace prism
