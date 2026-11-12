#include "OptoBand.h"

#include <algorithm>
#include <cmath>

namespace prism
{

namespace
{
constexpr double kAttackSeconds = 0.001 * static_cast<double> (kDetectorAttackMs);
constexpr double kReleaseSeconds = 0.001 * static_cast<double> (kDetectorReleaseMs);
constexpr float kLightFloor = 1.0e-12f;
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
    attackCoef_ = static_cast<float> (1.0 - std::exp (-1.0 / (kAttackSeconds * sampleRate_)));
    releaseCoef_ = static_cast<float> (1.0 - std::exp (-1.0 / (kReleaseSeconds * sampleRate_)));
    ldrFollow_ = static_cast<float> (1.0 - std::exp (-1.0 / (static_cast<double> (params_.tauLdrSeconds) * sampleRate_)));

    sensitivityGain_ = static_cast<float> (std::pow (10.0, static_cast<double> (params_.sensitivityDb) / 20.0));

    reset();
}

void OptoBand::reset()
{
    env_ = 0.0f;
    ldr_ = kDarkResistanceOhm;
    gain_ = 1.0f;
    lightIntensity_ = 0.0f;
}

void OptoBand::setDrive (float driveDb)
{
    const auto clamped = std::clamp (driveDb, -60.0f, 60.0f);
    driveGain_ = std::pow (10.0f, clamped / 20.0f);
}

float OptoBand::processSample (float x) noexcept
{
    // 1) 整流后一阶包络：带内输入直接进检测器（前馈）。
    const auto rectified = std::abs (x);
    const auto coef = rectified > env_ ? attackCoef_ : releaseCoef_;
    env_ += coef * (rectified - env_);

    // 2) 光强：EL 面板的陡峭亮度曲线，下限防止 log/pow 在静音时出 NaN。
    const auto ratio = std::max (env_ / kReferenceLevel, 0.0f);
    const auto shaped = std::pow (ratio, kLightExponent);
    lightIntensity_ = std::max (driveGain_ * sensitivityGain_ * shaped, kLightFloor);

    // 3) 光敏电阻：光越强电阻越小，按段 τ_ldr 形成记忆。
    const auto targetResistance = kDarkResistanceOhm / (1.0f + std::pow (lightIntensity_, params_.gamma));
    ldr_ += ldrFollow_ * (targetResistance - ldr_);

    // 4) 增益：串联分压的归一化形式，暗态 1、全亮 g_min；夹子必须在实现里显式写出。
    const auto linearGain = (kSeriesResistanceOhm + ldr_) / (kSeriesResistanceOhm + kDarkResistanceOhm);
    gain_ = std::clamp (linearGain, kMinGain, 1.0f);

    return x * gain_;
}

} // namespace prism
