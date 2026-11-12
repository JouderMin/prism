#pragma once

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "FirDesign.h"

namespace prism
{

/** 7 段线性相位 FIR 分频器。

    段 k = LP_k − LP_{k−1}，LP_0 = 0，LP_7 = δ。
    求和恒等于输入（延迟对齐后）。
    延迟 = taps/2 + 块长，48 kHz、2049 抽头、块 128 时为 1152 样本。

    processBlock 走 FFT 重叠保留；所有缓冲在构造与 prepare 中分配，
    音频线程不分配、不加锁。
*/
class Filterbank
{
public:
    static constexpr int kNumBands = prism::kNumBands;
    static constexpr int kBlockSize = 128;
    static constexpr int kMaxChannels = 2;
    static constexpr int kRingCapacity = 4 * kBlockSize;

    explicit Filterbank (int taps = kDefaultTaps);

    /** 重建分频系数并清空状态。可在任意采样率调用。 */
    void prepare (double sampleRate);

    /** 清空历史与输出队列，但保留系数。 */
    void reset();

    /** 上报延迟：taps/2 + kBlockSize。 */
    int latencySamples() const noexcept { return taps_ / 2 + kBlockSize; }

    int taps() const noexcept { return taps_; }
    int fftSize() const noexcept { return fftSize_; }
    double sampleRate() const noexcept { return sampleRate_; }

    /** 处理一块输入，写入 7 段。

        bands 必须已按输入通道数与样本数分配好；本函数不分配内存。
        输入通道数最多 2。
    */
    void processBlock (const juce::AudioBuffer<float>& input,
                       std::array<juce::AudioBuffer<float>, kNumBands>& bands);

private:
    static int fftSizeFor (int taps) noexcept;
    static int fftOrderFor (int fftSize) noexcept;

    void rebuild();
    void processHop (int channel);

    int taps_;
    int fftSize_;
    double sampleRate_ = 0.0;
    std::array<double, kNumCrossovers> crossovers_ {};
    juce::dsp::FFT fft_;

    std::array<std::vector<float>, kNumBands> bandSpectra_;
    std::array<std::vector<float>, kMaxChannels> history_;
    std::array<std::vector<float>, kMaxChannels> pending_;
    std::array<int, kMaxChannels> pendingCount_ {};
    std::array<std::array<std::vector<float>, kNumBands>, kMaxChannels> rings_;
    std::array<std::array<int, kNumBands>, kMaxChannels> ringRead_ {};
    std::array<std::array<int, kNumBands>, kMaxChannels> ringWrite_ {};
    std::vector<float> work_;
    std::vector<float> product_;
};

} // namespace prism
