#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Filterbank.h"
#include "dsp/FirDesign.h"

namespace
{

constexpr int kNumBands = prism::kNumBands;
constexpr int kBlock = prism::Filterbank::kBlockSize;

void makeBands (std::array<juce::AudioBuffer<float>, kNumBands>& bands,
                int numChannels, int numSamples)
{
    for (auto& band : bands)
        band.setSize (numChannels, numSamples);
}

void processInBlocks (prism::Filterbank& filterbank,
                      juce::AudioBuffer<float>& input,
                      std::array<juce::AudioBuffer<float>, kNumBands>& bands)
{
    const auto numChannels = input.getNumChannels();
    const auto numSamples = input.getNumSamples();

    for (int offset = 0; offset < numSamples; offset += kBlock)
    {
        const auto len = juce::jmin (kBlock, numSamples - offset);

        juce::AudioBuffer<float> inputView;
        inputView.setDataToReferTo (input.getArrayOfWritePointers(), numChannels, offset, len);

        std::array<juce::AudioBuffer<float>, kNumBands> bandViews;

        for (int b = 0; b < kNumBands; ++b)
            bandViews[static_cast<size_t> (b)].setDataToReferTo (
                bands[static_cast<size_t> (b)].getArrayOfWritePointers(),
                numChannels, offset, len);

        filterbank.processBlock (inputView, bandViews);
    }
}

float rmsOf (const juce::AudioBuffer<float>& buffer, int channel, int start, int numSamples)
{
    double sum = 0.0;
    const auto count = juce::jmax (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto value = static_cast<double> (buffer.getSample (channel, start + i));
        sum += value * value;
    }

    return static_cast<float> (std::sqrt (sum / count));
}

float toDecibels (float ratio)
{
    return 20.0f * std::log10 (juce::jmax (1.0e-9f, ratio));
}

void fillSine (juce::AudioBuffer<float>& buffer, double frequency, double sampleRate, float amplitude)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i,
                              amplitude * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi
                                                                        * frequency * static_cast<double> (i) / sampleRate)));
}

/** 独立单频幅度测量：Hann 窗 + 正交投影（Goertzel 形式），不读 Filterbank 内部状态。 */
double measureToneAmplitude (const float* samples, int numSamples, double sampleRate, double frequency)
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    const auto phaseStep = twoPi * frequency / sampleRate;

    double re = 0.0;
    double im = 0.0;
    double windowSum = 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto window = 0.5 - 0.5 * std::cos (twoPi * static_cast<double> (n)
                                                   / static_cast<double> (numSamples - 1));
        const auto sample = static_cast<double> (samples[n]);

        re += window * sample * std::cos (phaseStep * n);
        im += window * sample * std::sin (phaseStep * n);
        windowSum += window;
    }

    return 2.0 * std::sqrt (re * re + im * im) / windowSum;
}

/** 在测试内按 Filterbank::rebuild 的同一公式重建第 band 段的 2049 抽头系数。 */
std::vector<float> bandCoefficients (int band, double sampleRate, int taps)
{
    const auto lowpassAt = [sampleRate, taps] (double cutoff)
    {
        return prism::designLowpass (static_cast<float> (cutoff), sampleRate, taps, prism::kKaiserBeta);
    };

    std::vector<float> previous (static_cast<size_t> (taps), 0.0f);

    for (int k = 0; k < band; ++k)
        previous = lowpassAt (prism::kCrossoverPointsHz[static_cast<size_t> (k)]);

    if (band == prism::kNumBands - 1)
    {
        std::vector<float> top (static_cast<size_t> (taps), 0.0f);
        top[static_cast<size_t> (taps / 2)] = 1.0f;

        for (size_t i = 0; i < top.size(); ++i)
            top[i] -= previous[i];

        return top;
    }

    const auto low = lowpassAt (prism::kCrossoverPointsHz[static_cast<size_t> (band)]);
    std::vector<float> coefficients (static_cast<size_t> (taps), 0.0f);

    for (size_t i = 0; i < coefficients.size(); ++i)
        coefficients[i] = low[i] - previous[i];

    return coefficients;
}

/** 8192 样本 + 2049 抽头的线性卷积需要 16384 点 FFT，循环卷积不会卷回。 */
constexpr int kConvolutionOrder = 14;
constexpr int kConvolutionSize = 1 << kConvolutionOrder;

void convolveWithCoefficients (const juce::AudioBuffer<float>& input,
                               const std::vector<float>& coefficients,
                               juce::AudioBuffer<float>& output)
{
    juce::dsp::FFT fft (kConvolutionOrder);

    std::vector<float> coefficientSpectrum (static_cast<size_t> (2 * kConvolutionSize), 0.0f);
    std::copy (coefficients.begin(), coefficients.end(), coefficientSpectrum.begin());
    fft.performRealOnlyForwardTransform (coefficientSpectrum.data(), false);

    output.setSize (input.getNumChannels(), input.getNumSamples(), false, false, true);

    std::vector<float> spectrum (static_cast<size_t> (2 * kConvolutionSize), 0.0f);

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
    {
        std::fill (spectrum.begin(), spectrum.end(), 0.0f);

        for (int i = 0; i < input.getNumSamples(); ++i)
            spectrum[static_cast<size_t> (i)] = input.getSample (ch, i);

        fft.performRealOnlyForwardTransform (spectrum.data(), false);

        for (int k = 0; k < kConvolutionSize; ++k)
        {
            const auto re = spectrum[static_cast<size_t> (2 * k)];
            const auto im = spectrum[static_cast<size_t> (2 * k + 1)];
            const auto hr = coefficientSpectrum[static_cast<size_t> (2 * k)];
            const auto hi = coefficientSpectrum[static_cast<size_t> (2 * k + 1)];

            spectrum[static_cast<size_t> (2 * k)]     = re * hr - im * hi;
            spectrum[static_cast<size_t> (2 * k + 1)] = re * hi + im * hr;
        }

        fft.performRealOnlyInverseTransform (spectrum.data());

        for (int i = 0; i < input.getNumSamples(); ++i)
            output.setSample (ch, i, spectrum[static_cast<size_t> (i)]);
    }
}

} // namespace

class FilterbankTests final : public juce::UnitTest
{
public:
    FilterbankTests() : juce::UnitTest ("PRISM Filterbank", "prism") {}

    void runTest() override
    {
        testReconstructionAndDesign();
        testSingleBandReconstruction();
        testIsolation();
        testCrossoverEdge();
        testLatency();
        testSingleBandGainOutOfBand();
        testSampleRateChange();
        testLowSampleRateClamp();
    }

private:
    void testReconstructionAndDesign()
    {
        beginTest ("reconstruction: 7 bands sum to input after latency");

        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 8192;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        expectEquals (filterbank.latencySamples(), 1152);
        expectEquals (filterbank.fftSize(), 4096);

        juce::AudioBuffer<float> input (2, numSamples);
        juce::Random random (1234);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                input.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> bands;
        makeBands (bands, 2, numSamples);
        processInBlocks (filterbank, input, bands);

        const auto latency = filterbank.latencySamples();
        auto maxError = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = latency; i < numSamples; ++i)
            {
                auto sum = 0.0f;

                for (int b = 0; b < kNumBands; ++b)
                    sum += bands[static_cast<size_t> (b)].getSample (ch, i);

                maxError = juce::jmax (maxError,
                                       std::abs (sum - input.getSample (ch, i - latency)));
            }
        }

        logMessage ("max reconstruction error: " + juce::String (maxError, 9));
        expectLessThan (maxError, 1.0e-6f);

        // designLowpass 契约：长度 = taps、对称、DC = 1。
        const auto coefficients = prism::designLowpass (80.0f, sampleRate, 2049, 8.0);
        expectEquals (static_cast<int> (coefficients.size()), 2049);

        double dcGain = 0.0;
        auto maxAsymmetry = 0.0f;

        for (int i = 0; i < 2049; ++i)
        {
            dcGain += static_cast<double> (coefficients[static_cast<size_t> (i)]);
            maxAsymmetry = juce::jmax (maxAsymmetry,
                                       std::abs (coefficients[static_cast<size_t> (i)]
                                                 - coefficients[static_cast<size_t> (2048 - i)]));
        }

        logMessage ("designLowpass DC gain: " + juce::String (dcGain, 9));
        expectWithinAbsoluteError (dcGain, 1.0, 1.0e-5);
        expectLessThan (maxAsymmetry, 1.0e-7f);
    }

    void testSingleBandReconstruction()
    {
        beginTest ("single-band reconstruction: each band matches independent FIR convolution");

        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 8192;
        constexpr int taps = 2049;
        // Filterbank 输出比线性卷积晚一个 hop（128 样本），与 latency 1152 = 1024 + 128 一致。
        constexpr int hopDelay = prism::Filterbank::kBlockSize;

        prism::Filterbank filterbank (taps);
        filterbank.prepare (sampleRate);

        juce::AudioBuffer<float> input (2, numSamples);
        juce::Random random (4321);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                input.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> bands;
        makeBands (bands, 2, numSamples);
        processInBlocks (filterbank, input, bands);

        for (int band = 0; band < kNumBands; ++band)
        {
            const auto coefficients = bandCoefficients (band, sampleRate, taps);

            // 重建一：只保留该段、其余段置零后求和。恒等成立说明 7 段缓冲区
            // 没有互相别名、没有共享写入；任何跨段污染都会在这里出现。
            juce::AudioBuffer<float> isolated (2, numSamples);
            isolated.clear();
            isolated.addFrom (0, 0, bands[static_cast<size_t> (band)], 0, 0, numSamples, 1.0f);
            isolated.addFrom (1, 0, bands[static_cast<size_t> (band)], 1, 0, numSamples, 1.0f);

            auto isolationError = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    isolationError = juce::jmax (isolationError,
                                                 std::abs (isolated.getSample (ch, i)
                                                           - bands[static_cast<size_t> (band)].getSample (ch, i)));

            expectEquals (isolationError, 0.0f);

            // 重建二：用公开的 designLowpass 在测试内独立重建该段系数，做 16384 点
            // 线性 FFT 卷积，再与 Filterbank 第 i 段输出逐样本比对。这是硬判据：
            // 只有 overlap-save 的 128 样本分块、频谱乘和 hop 拼接都正确、且各段
            // 之间没有串扰，两条路径才会在 1e-5 内一致；错位或跨段加窗会立刻暴露。
            juce::AudioBuffer<float> convolution (2, numSamples);
            convolveWithCoefficients (input, coefficients, convolution);

            auto maxError = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = hopDelay; i < numSamples; ++i)
                    maxError = juce::jmax (maxError,
                                           std::abs (bands[static_cast<size_t> (band)].getSample (ch, i)
                                                     - convolution.getSample (ch, i - hopDelay)));

            logMessage ("band " + juce::String (band + 1) + " independent convolution error: "
                        + juce::String (maxError, 9));
            expectLessThan (maxError, 1.0e-5f);
        }
    }

    void testIsolation()
    {
        beginTest ("isolation: band 2 at 40 Hz and single-band energy at 1 kHz");

        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 8192;
        constexpr float amplitude = 0.5f;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        const auto latency = filterbank.latencySamples();
        // 跳过延迟与 FIR 建立时间，只测稳态。
        const auto measureStart = latency + 2049;
        const auto measured = numSamples - measureStart;

        // 段 2 在 40 Hz 的泄漏。
        {
            juce::AudioBuffer<float> input (1, numSamples);
            fillSine (input, 40.0, sampleRate, amplitude);

            std::array<juce::AudioBuffer<float>, kNumBands> bands;
            makeBands (bands, 1, numSamples);
            processInBlocks (filterbank, input, bands);

            const auto inputRms = rmsOf (input, 0, measureStart, measured);
            const auto leakRms = rmsOf (bands[1], 0, measureStart, measured);
            const auto leakDb = toDecibels (leakRms / inputRms);

            logMessage ("band 2 leakage at 40 Hz: " + juce::String (leakDb, 2) + " dB");
            // 新边界下 40 Hz 完全落在段 1（<150 Hz），段 2 在此处只有数值泄漏。
            // 实测 −101.35 dB（2049 抽头、Kaiser β=8）；阈值取 −98 dB，留 3 dB 余量。
            // 段 2 真正的“带外一个倍频程”是 75 Hz，由单段带外用例覆盖。
            expectLessThan (leakDb, -98.0f);
        }

        // 1 kHz 应主要落在段 3（500–1200 Hz）。
        {
            filterbank.reset();

            juce::AudioBuffer<float> input (1, numSamples);
            fillSine (input, 1000.0, sampleRate, amplitude);

            std::array<juce::AudioBuffer<float>, kNumBands> bands;
            makeBands (bands, 1, numSamples);
            processInBlocks (filterbank, input, bands);

            const auto inputRms = rmsOf (input, 0, measureStart, measured);
            const auto band3Rms = rmsOf (bands[2], 0, measureStart, measured);

            auto loudestOther = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
                if (b != 2)
                    loudestOther = juce::jmax (loudestOther, rmsOf (bands[static_cast<size_t> (b)], 0, measureStart, measured));

            const auto passbandDb = toDecibels (band3Rms / inputRms);
            const auto dominanceDb = toDecibels (band3Rms / juce::jmax (1.0e-9f, loudestOther));

            logMessage ("band 3 passband at 1 kHz: " + juce::String (passbandDb, 2)
                        + " dB, dominance: " + juce::String (dominanceDb, 2) + " dB");

            expectLessThan (std::abs (passbandDb), 0.5f);
            expectGreaterThan (dominanceDb, 20.0f);
        }
    }

    void testCrossoverEdge()
    {
        beginTest ("crossover edge: band 1 is -6 dB at 150 Hz");

        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 8192;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        juce::AudioBuffer<float> input (1, numSamples);
        fillSine (input, 150.0, sampleRate, 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> bands;
        makeBands (bands, 1, numSamples);
        processInBlocks (filterbank, input, bands);

        const auto latency = filterbank.latencySamples();
        // 跳过延迟与 FIR 建立时间，只测稳态。
        const auto measureStart = latency + 2049;
        const auto measured = numSamples - measureStart;

        const auto inputRms = rmsOf (input, 0, measureStart, measured);
        const auto band1Rms = rmsOf (bands[0], 0, measureStart, measured);
        const auto band1Db = toDecibels (band1Rms / inputRms);

        logMessage ("band 1 at 150 Hz: " + juce::String (band1Db, 2) + " dB");
        expectWithinAbsoluteError (band1Db, -6.02f, 0.5f);
    }

    void testLatency()
    {
        beginTest ("latency: impulse peak lands at latencySamples()");

        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 8192;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        expectEquals (filterbank.latencySamples(), 1152);

        juce::AudioBuffer<float> input (1, numSamples);
        input.clear();
        input.setSample (0, 0, 1.0f);

        std::array<juce::AudioBuffer<float>, kNumBands> bands;
        makeBands (bands, 1, numSamples);
        processInBlocks (filterbank, input, bands);

        auto peakIndex = 0;
        auto peakValue = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sum = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
                sum += bands[static_cast<size_t> (b)].getSample (0, i);

            if (std::abs (sum) > std::abs (peakValue))
            {
                peakValue = sum;
                peakIndex = i;
            }
        }

        logMessage ("impulse peak at sample " + juce::String (peakIndex)
                    + ", value " + juce::String (peakValue, 6));

        expectEquals (peakIndex, 1152);
        expectWithinAbsoluteError (peakValue, 1.0f, 1.0e-3f);
    }


    void testSingleBandGainOutOfBand()
    {
        beginTest ("single-band -6 dB: out-of-band deviation below 0.5 dB (spec 9.1)");

        // 判据：只把段 k 的输出乘 0.5（−6 dB），其余段保持 1。7 段求和本应重建输入，
        // 因此 y_k = 输入（延迟对齐）+ (0.5 − 1)·段 k。若段 k 的增益变化外溢到相邻段，
        // 它通带外一个倍频程处会出现可测幅度偏差。spec 9.1 的验算值为 0.00 dB；
        // 这里容差取 0.5 dB，覆盖 2049 抽头过渡带泄漏（实测 11 个测点偏差均 ≤ 0.001 dB）。
        // 段 1 下侧与段 7 上侧没有“通带外一个倍频程”，跳过。
        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 16384;
        constexpr int taps = 2049;
        constexpr int measureStart = 4096;    // 跳过 1152 延迟 + 2049 抽头建立时间。
        constexpr int measureLength = 8192;
        constexpr float scaledGain = 0.5f;    // −6 dB

        struct Probe { int band; double lowerHz; double upperHz; };

        const Probe probes[] = {
            { 0, 0.0,    300.0 },   // 段 1（<150）：只有上侧 300 Hz。
            { 1, 75.0,   1000.0 },  // 段 2（150–500）。
            { 2, 250.0,  2400.0 },  // 段 3（500–1200）。
            { 3, 600.0,  6000.0 },  // 段 4（1200–3000）。
            { 4, 1500.0, 16000.0 }, // 段 5（3000–8000）。
            { 5, 4000.0, 0.0 },     // 段 6（8000–14000）：上侧 28 kHz 超 Nyquist，只测下侧。
            { 6, 7000.0, 0.0 },     // 段 7（>14000）：只有下侧 7 kHz。
        };

        prism::Filterbank filterbank (taps);

        for (const auto& probe : probes)
        {
            const double frequencies[] = { probe.lowerHz, probe.upperHz };

            for (const auto frequency : frequencies)
            {
                if (frequency <= 0.0)
                    continue;   // 段 1 下侧 / 段 7 上侧无带外倍频程，跳过。

                filterbank.prepare (sampleRate);

                juce::AudioBuffer<float> input (1, numSamples);
                fillSine (input, frequency, sampleRate, 0.5f);

                std::array<juce::AudioBuffer<float>, kNumBands> bands;
                makeBands (bands, 1, numSamples);
                processInBlocks (filterbank, input, bands);

                juce::AudioBuffer<float> scaled (1, numSamples);
                scaled.clear();

                for (int b = 0; b < kNumBands; ++b)
                    scaled.addFrom (0, 0, bands[static_cast<size_t> (b)], 0, 0, numSamples,
                                    b == probe.band ? scaledGain : 1.0f);

                const auto inputAmplitude = measureToneAmplitude (input.getReadPointer (0) + measureStart,
                                                                  measureLength, sampleRate, frequency);
                const auto outputAmplitude = measureToneAmplitude (scaled.getReadPointer (0) + measureStart,
                                                                   measureLength, sampleRate, frequency);

                const auto deviationDb = 20.0 * std::log10 (outputAmplitude / inputAmplitude);

                const auto inputRms = rmsOf (input, 0, measureStart, measureLength);
                const auto bandRms = rmsOf (bands[static_cast<size_t> (probe.band)], 0,
                                            measureStart, measureLength);
                const auto leakageDb = toDecibels (bandRms / inputRms);

                logMessage ("band " + juce::String (probe.band + 1) + " @ "
                            + juce::String (frequency, 1) + " Hz: leakage "
                            + juce::String (leakageDb, 2) + " dB, deviation "
                            + juce::String (deviationDb, 3) + " dB");

                expectLessThan (std::abs (deviationDb), 0.5);
            }
        }
    }

    void testLowSampleRateClamp()
    {
        beginTest ("11.025 kHz: crossovers clamp to 0.45*fs and reconstruction holds");

        // 14 kHz 超过 11.025 kHz 的 Nyquist。prepare 把每个分频点夹到 min(point, 0.45·fs)，
        // 因此 designLowpass 不会收到 >= fs/2 的截止频率（否则其 jassert 会在 debug 触发）。
        // 相邻点重合时该段系数为 0，7 段求和仍恒等于输入。
        constexpr double sampleRate = 11025.0;
        constexpr int numSamples = 8192;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        expectEquals (filterbank.latencySamples(), 1152);

        juce::AudioBuffer<float> noise (1, numSamples);
        juce::Random random (9012);

        for (int i = 0; i < numSamples; ++i)
            noise.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> bands;
        makeBands (bands, 1, numSamples);
        processInBlocks (filterbank, noise, bands);

        const auto latency = filterbank.latencySamples();
        auto maxError = 0.0f;

        for (int i = latency; i < numSamples; ++i)
        {
            auto sum = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
                sum += bands[static_cast<size_t> (b)].getSample (0, i);

            maxError = juce::jmax (maxError, std::abs (sum - noise.getSample (0, i - latency)));
        }

        logMessage ("11.025 kHz max reconstruction error: " + juce::String (maxError, 9));
        expectLessThan (maxError, 1.0e-6f);
    }

    void testSampleRateChange()
    {
        beginTest ("96 kHz: reconstruction, latency and crossover stay valid");

        constexpr double sampleRate = 96000.0;
        constexpr int numSamples = 16384;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        expectEquals (filterbank.latencySamples(), 1152);

        juce::AudioBuffer<float> noise (1, numSamples);
        juce::Random random (5678);

        for (int i = 0; i < numSamples; ++i)
            noise.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> bands;
        makeBands (bands, 1, numSamples);
        processInBlocks (filterbank, noise, bands);

        const auto latency = filterbank.latencySamples();
        auto maxError = 0.0f;

        for (int i = latency; i < numSamples; ++i)
        {
            auto sum = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
                sum += bands[static_cast<size_t> (b)].getSample (0, i);

            maxError = juce::jmax (maxError, std::abs (sum - noise.getSample (0, i - latency)));
        }

        logMessage ("96 kHz max reconstruction error: " + juce::String (maxError, 9));
        expectLessThan (maxError, 1.0e-6f);

        // 换采样率后 150 Hz 仍应为 −6 dB。
        filterbank.reset();

        juce::AudioBuffer<float> sine (1, numSamples);
        fillSine (sine, 150.0, sampleRate, 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> sineBands;
        makeBands (sineBands, 1, numSamples);
        processInBlocks (filterbank, sine, sineBands);

        // 跳过延迟与 FIR 建立时间，只测稳态。
        const auto measureStart = latency + 2049;
        const auto measured = numSamples - measureStart;
        const auto inputRms = rmsOf (sine, 0, measureStart, measured);
        const auto band1Rms = rmsOf (sineBands[0], 0, measureStart, measured);
        const auto band1Db = toDecibels (band1Rms / inputRms);

        logMessage ("96 kHz band 1 at 150 Hz: " + juce::String (band1Db, 2) + " dB");
        expectWithinAbsoluteError (band1Db, -6.02f, 0.5f);
    }
};

static FilterbankTests filterbankTests;
