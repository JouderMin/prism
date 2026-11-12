#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Filterbank.h"
#include "OptoBand.h"

namespace prism
{

/** 整机参数。数值已在参数层归一化，引擎不再重复映射范围。 */
struct EngineParams
{
    float amount01 = 0.5f;  // 0..1，总光强驱动
    float spectrum = 0.0f;  // -1..+1，光谱倾斜：负压低段，正压高段
    float mix01 = 1.0f;     // 0..1，干湿比
    float inputDb = 0.0f;   // -12..+24 dB
    float outputDb = 0.0f;  // -24..+12 dB
};

/** 7 段光电压缩器引擎。

    数据流：输入增益 → Filterbank 分频 → 每段光电链路 → 每段 auto-gain
    → 求和 → 干湿混合 → 输出增益。

    延迟只来自 Filterbank（taps/2 + 128 = 1152 样本）。干声支路用同样长度的
    延时线对齐，引擎本身不额外增加缓冲延迟。

    所有缓冲在 prepare 中分配；processBlock 内不分配、不加锁、不做 I/O。
*/
class PrismDsp
{
public:
    static constexpr int kNumBands = prism::kNumBands;
    static constexpr int kLatencySamples = 1152;
    static constexpr int kDryDelaySamples = kLatencySamples;

    PrismDsp();

    /** 分配缓冲并重建分频系数。numChannels 只支持 1..2。 */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** 清空全部状态，参数平滑器直接跳到当前目标值。 */
    void reset();

    /** 只写目标值；实际平滑在 processBlock 内逐样本进行。 */
    void setParams (const EngineParams& params) noexcept;

    /** 就地处理一块音频。块长不得超过 prepare 时的 maxBlockSize。 */
    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

    /** 每段光强（0..1），界面线程 30 Hz 读取。 */
    std::array<std::atomic<float>, kNumBands>& meters() noexcept { return meters_; }

    /** 平滑后的 AMOUNT，测试与界面用。 */
    [[nodiscard]] float currentAmount01() const noexcept { return amount_.current(); }

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int maxBlockSize() const noexcept { return maxBlockSize_; }

private:
    /** 一阶低通平滑器：setParams 写目标，processBlock 逐样本逼近。 */
    struct SmoothedValue
    {
        void prepare (double sampleRate, double timeConstantSeconds) noexcept
        {
            coefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (timeConstantSeconds * sampleRate)));
        }

        void setTarget (float newTarget) noexcept { target_ = newTarget; }
        void snapToTarget() noexcept { current_ = target_; }
        void snap (float value) noexcept { current_ = target_ = value; }

        float next() noexcept
        {
            current_ += coefficient_ * (target_ - current_);

            if (std::abs (target_ - current_) < 1.0e-6f)
                current_ = target_;

            return current_;
        }

        [[nodiscard]] float current() const noexcept { return current_; }

    private:
        float current_ = 0.0f;
        float target_ = 0.0f;
        float coefficient_ = 1.0f;
    };

    static float decibelsToGain (float decibels) noexcept
    {
        return static_cast<float> (std::pow (10.0, static_cast<double> (decibels) / 20.0));
    }

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 2;

    Filterbank filterbank_;
    std::array<OptoBand, kNumBands> optos_ {
        OptoBand (0), OptoBand (1), OptoBand (2), OptoBand (3),
        OptoBand (4), OptoBand (5), OptoBand (6)
    };

    std::array<juce::AudioBuffer<float>, kNumBands> bands_;
    juce::AudioBuffer<float> inputGainBuffer_;
    std::array<std::vector<float>, 2> dryDelay_;
    int dryWrite_ = 0;

    std::array<float, kNumBands> autoGainDb_ {};
    float autoGainCoefficient_ = 0.0f;

    std::array<std::atomic<float>, kNumBands> meters_ {};

    SmoothedValue amount_;
    SmoothedValue spectrum_;
    SmoothedValue mix_;
    SmoothedValue inputDb_;
    SmoothedValue outputDb_;

    EngineParams targets_;
};

} // namespace prism
