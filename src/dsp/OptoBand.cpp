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

#include "OptoBand.h"

#include <algorithm>
#include <cmath>

namespace prism
{

namespace
{
constexpr double kAttackSeconds = 0.001 * static_cast<double> (kDetectorAttackMs);
constexpr float kLightFloor = 1.0e-12f;

// 峰值下方 12 dB 内走快段；跨过这个门限后切到尾段。
const float kPeakHoldThreshold = static_cast<float> (
    std::pow (10.0, -static_cast<double> (kPeakHoldReleaseRangeDb) / 20.0));

constexpr float kLog2PerDb = 0.16609640474436813f; // log2(10) / 20
constexpr float kLog2Reference = -2.9897356f;      // log2(kReferenceLevel)

constexpr float kLightMinDb = -120.0f;
constexpr float kLightMaxDb = 12.0f;
constexpr float kLightMinLog2 = kLightMinDb * kLog2PerDb;
constexpr float kLightMaxLog2 = kLightMaxDb * kLog2PerDb;
constexpr float kLightSpanLog2 = kLightMaxLog2 - kLightMinLog2;
constexpr float kLightPosScale = static_cast<float> (OptoBand::kTableSize - 1) / kLightSpanLog2;
constexpr int kLightMaxChunk = 3;

// 光阻按 log2(light) 建表；下限覆盖 kLightFloor，上限覆盖 +24 dB 光强。
constexpr float kResistanceMinLog2 = -40.0f;
constexpr float kResistanceMaxLog2 = 24.0f;
constexpr float kResistancePosScale = static_cast<float> (OptoBand::kTableSize - 1)
                                    / (kResistanceMaxLog2 - kResistanceMinLog2);

constexpr float kDriveMinDb = -60.0f;
constexpr float kDriveMaxDb = 60.0f;
constexpr float kDrivePosScale = static_cast<float> (OptoBand::kTableSize - 1)
                               / (kDriveMaxDb - kDriveMinDb);

float interpolate (const std::array<float, OptoBand::kTableSize>& table, float position) noexcept
{
    const auto clamped = std::clamp (position, 0.0f, static_cast<float> (OptoBand::kTableSize - 2));
    const auto index = static_cast<int> (clamped);
    const auto fraction = clamped - static_cast<float> (index);
    const auto a = table[static_cast<size_t> (index)];
    const auto b = table[static_cast<size_t> (index + 1)];
    return a + (b - a) * fraction;
}
} // namespace

OptoBand::OptoBand (int bandIndex)
    : bandIndex_ (std::clamp (bandIndex, 0, static_cast<int> (kBands.size()) - 1)),
      params_ (kBands[static_cast<size_t> (bandIndex_)])
{
    prepare (48000.0);
}

void OptoBand::prepare (double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    sampleRate_ = sampleRate;

    // 一阶插值权重：env += (1 − exp(−1/(τ·fs)))·(x − env)，τ 秒到 63%。
    // 释放的两段长度来自 BandParams，M4 起真正参与音频路径。
    attackCoef_ = static_cast<float> (1.0 - std::exp (-1.0 / (kAttackSeconds * sampleRate_)));
    releaseCoef_ = static_cast<float> (1.0 - std::exp (
        -1.0 / (0.001 * static_cast<double> (params_.releaseFastMs) * sampleRate_)));
    releaseTailCoef_ = static_cast<float> (1.0 - std::exp (
        -1.0 / (0.001 * static_cast<double> (params_.releaseTailMs) * sampleRate_)));
    peakHoldDecay_ = static_cast<float> (
        std::pow (10.0, -static_cast<double> (kPeakHoldDecayDbPerSecond) / (20.0 * sampleRate_)));
    ldrFollow_ = static_cast<float> (1.0 - std::exp (-1.0 / (static_cast<double> (params_.tauLdrSeconds) * sampleRate_)));

    sensitivityGain_ = static_cast<float> (std::pow (10.0, static_cast<double> (params_.sensitivityDb) / 20.0));
    sensitivityLog2_ = fastLog2 (sensitivityGain_);

    rebuildTables();
    setDrive (driveDb_);

    reset();
}

void OptoBand::rebuildTables()
{
    constexpr auto last = static_cast<double> (kTableSize - 1);

    for (int i = 0; i < kTableSize; ++i)
    {
        const auto fraction = static_cast<double> (i) / last;
        const auto index = static_cast<size_t> (i);

        const auto lightLog2 = static_cast<double> (kLightMinLog2)
                             + fraction * static_cast<double> (kLightSpanLog2);
        const auto ratio = std::exp2 (lightLog2);
        lightTable_[index] = static_cast<float> (std::pow (ratio, static_cast<double> (kLightExponent)));

        const auto resistanceLog2 = static_cast<double> (kResistanceMinLog2)
                                  + fraction * static_cast<double> (kResistanceMaxLog2 - kResistanceMinLog2);
        const auto light = std::exp2 (resistanceLog2);
        resistanceTable_[index] = static_cast<float> (1.0 / (1.0 + std::pow (light, static_cast<double> (params_.gamma))));

        const auto driveDb = static_cast<double> (kDriveMinDb)
                           + fraction * static_cast<double> (kDriveMaxDb - kDriveMinDb);
        driveGainTable_[index] = static_cast<float> (std::pow (10.0, driveDb / 20.0));
    }

    // 超出 +12 dB 表上限的输入按 span 分块；每块只需乘一个预计算因子，避免逐样本 pow。
    const auto chunkBase = std::exp2 (static_cast<double> (kLightExponent) * static_cast<double> (kLightSpanLog2));
    lightChunkGain_[0] = 1.0f;
    for (int n = 1; n <= kLightMaxChunk; ++n)
        lightChunkGain_[static_cast<size_t> (n)] =
            lightChunkGain_[static_cast<size_t> (n - 1)] * static_cast<float> (chunkBase);
}

void OptoBand::reset()
{
    env_ = 0.0f;
    peakHold_ = 0.0f;
    tailLatched_ = false;
    ldr_ = kDarkResistanceOhm;
    gain_ = 1.0f;
    lightIntensity_ = 0.0f;
}

void OptoBand::setDrive (float driveDb)
{
    driveDb_ = std::clamp (driveDb, kDriveMinDb, kDriveMaxDb);
    driveGain_ = interpolate (driveGainTable_, (driveDb_ - kDriveMinDb) * kDrivePosScale);
    log2DriveScale_ = driveDb_ * kLog2PerDb + sensitivityLog2_;
}

float OptoBand::processSample (float x) noexcept
{
    // 1) 整流后一阶包络：带内输入直接进检测器（前馈）。
    //    释放是程序相关的：峰值 12 dB 内用快段，掉出窗口后用长尾。
    const auto rectified = std::abs (x);

    if (rectified > env_)
    {
        env_ += attackCoef_ * (rectified - env_);
        tailLatched_ = false;
    }
    else
    {
        // 一旦跌出峰值下方 12 dB 就锁存到尾段，避免门限附近来回抖动。
        if (env_ <= peakHold_ * kPeakHoldThreshold)
            tailLatched_ = true;

        const auto release = tailLatched_ ? releaseTailCoef_ : releaseCoef_;
        env_ += release * (rectified - env_);
    }

    // peakHold 只负责选择释放段：跟随包络上升，下降按 6 dB/s。
    if (env_ > peakHold_)
    {
        peakHold_ = env_;
    }
    else if (tailLatched_)
    {
        // 进入尾段后 peakHold 才按 6 dB/s 慢衰减；快段期间保持峰值。
        peakHold_ *= peakHoldDecay_;
    }

    // 2) 光强：EL 面板的陡峭亮度曲线。log2 只用位运算与级数近似，曲线值查表。
    const auto log2Ratio = fastLog2 (env_) - kLog2Reference;

    auto tableLog2 = log2Ratio;
    auto chunk = 0;

    if (tableLog2 > kLightMaxLog2)
    {
        chunk = std::min (kLightMaxChunk,
                          static_cast<int> (std::floor ((tableLog2 - kLightMinLog2) / kLightSpanLog2)));
        tableLog2 -= static_cast<float> (chunk) * kLightSpanLog2;
    }

    const auto lightPosition = (std::clamp (tableLog2, kLightMinLog2, kLightMaxLog2) - kLightMinLog2)
                             * kLightPosScale;
    const auto shaped = interpolate (lightTable_, lightPosition)
                      * lightChunkGain_[static_cast<size_t> (chunk)];
    lightIntensity_ = std::max (driveGain_ * sensitivityGain_ * shaped, kLightFloor);

    // 3) 光敏电阻：光越强电阻越小，按段 τ_ldr 形成记忆。同样查表 + 插值。
    const auto log2Light = log2DriveScale_ + kLightExponent * log2Ratio;
    const auto resistanceLog2 = std::clamp (log2Light, kResistanceMinLog2, kResistanceMaxLog2);
    const auto targetResistance = kDarkResistanceOhm
                                * interpolate (resistanceTable_,
                                               (resistanceLog2 - kResistanceMinLog2) * kResistancePosScale);
    ldr_ += ldrFollow_ * (targetResistance - ldr_);

    // 4) 增益：串联分压的归一化形式，暗态 1、全亮 g_min；夹子必须在实现里显式写出。
    const auto linearGain = (kSeriesResistanceOhm + ldr_) / (kSeriesResistanceOhm + kDarkResistanceOhm);
    gain_ = std::clamp (linearGain, kMinGain, 1.0f);

    return x * gain_;
}

} // namespace prism
