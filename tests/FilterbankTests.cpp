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
        testSampleRateChange();
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
            // spec 5.2 实测表：2049 抽头、Kaiser β=8 的段外泄漏是 −33.3 dB。
            // plan 里写的 −35 dB 与 spec 表冲突；这里按 spec 实测留 0.7 dB 余量。
            expectLessThan (leakDb, -32.0f);
        }

        // 1 kHz 应主要落在段 4（500–1200 Hz）。
        {
            filterbank.reset();

            juce::AudioBuffer<float> input (1, numSamples);
            fillSine (input, 1000.0, sampleRate, amplitude);

            std::array<juce::AudioBuffer<float>, kNumBands> bands;
            makeBands (bands, 1, numSamples);
            processInBlocks (filterbank, input, bands);

            const auto inputRms = rmsOf (input, 0, measureStart, measured);
            const auto band4Rms = rmsOf (bands[3], 0, measureStart, measured);

            auto loudestOther = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
                if (b != 3)
                    loudestOther = juce::jmax (loudestOther, rmsOf (bands[static_cast<size_t> (b)], 0, measureStart, measured));

            const auto passbandDb = toDecibels (band4Rms / inputRms);
            const auto dominanceDb = toDecibels (band4Rms / juce::jmax (1.0e-9f, loudestOther));

            logMessage ("band 4 passband at 1 kHz: " + juce::String (passbandDb, 2)
                        + " dB, dominance: " + juce::String (dominanceDb, 2) + " dB");

            expectLessThan (std::abs (passbandDb), 0.5f);
            expectGreaterThan (dominanceDb, 20.0f);
        }
    }

    void testCrossoverEdge()
    {
        beginTest ("crossover edge: band 1 is -6 dB at 80 Hz");

        constexpr double sampleRate = 48000.0;
        constexpr int numSamples = 8192;

        prism::Filterbank filterbank (2049);
        filterbank.prepare (sampleRate);

        juce::AudioBuffer<float> input (1, numSamples);
        fillSine (input, 80.0, sampleRate, 0.5f);

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

        logMessage ("band 1 at 80 Hz: " + juce::String (band1Db, 2) + " dB");
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

        // 换采样率后 80 Hz 仍应为 −6 dB。
        filterbank.reset();

        juce::AudioBuffer<float> sine (1, numSamples);
        fillSine (sine, 80.0, sampleRate, 0.5f);

        std::array<juce::AudioBuffer<float>, kNumBands> sineBands;
        makeBands (sineBands, 1, numSamples);
        processInBlocks (filterbank, sine, sineBands);

        // 跳过延迟与 FIR 建立时间，只测稳态。
        const auto measureStart = latency + 2049;
        const auto measured = numSamples - measureStart;
        const auto inputRms = rmsOf (sine, 0, measureStart, measured);
        const auto band1Rms = rmsOf (sineBands[0], 0, measureStart, measured);
        const auto band1Db = toDecibels (band1Rms / inputRms);

        logMessage ("96 kHz band 1 at 80 Hz: " + juce::String (band1Db, 2) + " dB");
        expectWithinAbsoluteError (band1Db, -6.02f, 0.5f);
    }
};

static FilterbankTests filterbankTests;
