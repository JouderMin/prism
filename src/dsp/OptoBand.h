#pragma once

#include "BandTable.h"

namespace prism
{

/** 单段光电链路：检测器 → 光强 → 光敏电阻 → 反馈增益。

    每样本处理，无分配、无锁、无 I/O。所有状态在构造与 prepare 中确定。

    检测器：整流后一阶包络，attack 10 ms、release 60 ms（固定）。
    光强：d = A · s_b · (env / ref)^1.6，ref = −18 dBFS，A = 10^(drive/20)。
    光敏电阻：R = R_dark / (1 + (d/d0)^γ)，按段 τ_ldr 一阶跟随。
    增益：g = (R_s + R)/(R_s + R_dark)，夹到 [g_min, 1]。
*/
class OptoBand
{
public:
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

    int bandIndex() const noexcept { return bandIndex_; }

private:
    int bandIndex_;
    BandParams params_;

    double sampleRate_ = 48000.0;
    float attackCoef_ = 0.0f;
    float releaseCoef_ = 0.0f;
    float ldrFollow_ = 0.0f;
    float driveGain_ = 1.0f;
    float sensitivityGain_ = 1.0f;

    float env_ = 0.0f;
    float ldr_ = kDarkResistanceOhm;
    float gain_ = 1.0f;
    float lightIntensity_ = 0.0f;
};

} // namespace prism
