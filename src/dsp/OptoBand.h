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

#include "BandTable.h"

namespace prism
{

/** 单段光电链路：检测器 → 光强 → 光敏电阻 → 反馈增益。

    每样本处理，无分配、无锁、无 I/O。所有状态在构造与 prepare 中确定。

    检测器：整流后一阶包络，attack 10 ms；释放按程序相关双段，峰值 6 dB
    内 60 ms、跌出后 1200 ms；peakHold 在快段保持峰值，进尾段后以 6 dB/s 慢衰减。
    光强：d = A · s_b · (env / ref)^1.6，ref = −18 dBFS，A = 10^(drive/20)。
    光敏电阻：R = R_dark / (1 + (d/d0)^γ)，按段 τ_ldr 一阶跟随。
    增益：g = (R_s + R)/(R_s + R_dark)，夹到 [g_min, 1]。
*/
class OptoBand
{
public:
    /** 每张曲线表的点数；表在 prepare 中生成，音频线程只读。 */
    static constexpr int kTableSize = 1024;

    /** bandIndex 越界时夹到 [0, 6]。 */
    explicit OptoBand (int bandIndex = 0);

    /** 采样率变化时重算时间常数，并清空状态。 */
    void prepare (double sampleRate);

    /** 清空包络、光敏电阻与增益状态。 */
    void reset();

    /** driveDb 为光强增益，单位 dB，内部夹到 [−60, 60]。 */
    void setDrive (float driveDb);

    /** 处理一个样本，返回压缩后的样本。 */
    float processSample (float x) noexcept;

    /** 归一化光强 [0, 1)，用于表头显示。 */
    float currentLight() const noexcept { return lightIntensity_ / (1.0f + lightIntensity_); }

    /** 当前增益，恒在 [kMinGain, 1]。 */
    float currentGain() const noexcept { return gain_; }

    /** 当前整流包络，供检测器时间常数测试与诊断用。 */
    float currentEnvelope() const noexcept { return env_; }

    /** 当前慢衰减峰值，供双时间常数测试与诊断用。 */
    float currentPeakHold() const noexcept { return peakHold_; }

    /** 是否已锁存到 1200 ms 尾段；供双段释放测试直接验证 6 dB 拐点。 */
    bool isTailLatched() const noexcept { return tailLatched_; }

    int bandIndex() const noexcept { return bandIndex_; }

private:
    void rebuildTables();

    int bandIndex_;
    BandParams params_;

    double sampleRate_ = 48000.0;
    float attackCoef_ = 0.0f;
    float releaseCoef_ = 0.0f;
    float releaseTailCoef_ = 0.0f;
    float peakHoldDecay_ = 0.0f;
    bool tailLatched_ = false;
    float ldrFollow_ = 0.0f;
    float driveGain_ = 1.0f;
    float sensitivityGain_ = 1.0f;
    float sensitivityLog2_ = 0.0f;
    float driveDb_ = 0.0f;
    float log2DriveScale_ = 0.0f;

    std::array<float, kTableSize> lightTable_ {};
    std::array<float, kTableSize> resistanceTable_ {};
    std::array<float, kTableSize> driveGainTable_ {};
    std::array<float, 4> lightChunkGain_ { 1.0f, 1.0f, 1.0f, 1.0f };

    float env_ = 0.0f;
    float peakHold_ = 0.0f;
    float ldr_ = kDarkResistanceOhm;
    float gain_ = 1.0f;
    float lightIntensity_ = 0.0f;
};

} // namespace prism
