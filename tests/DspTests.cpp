#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "dsp/PrismDsp.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kProcessBlock = 512;

void fillSine (juce::AudioBuffer<float>& buffer, double frequency, float amplitude)
{
    const auto twoPi = juce::MathConstants<double>::twoPi;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.getWritePointer (ch)[i] = amplitude
                * static_cast<float> (std::sin (twoPi * frequency
                                                * static_cast<double> (i) / kSampleRate));
}

void processInBlocks (prism::PrismDsp& dsp, juce::AudioBuffer<float>& buffer, int blockSize)
{
    for (int offset = 0; offset < buffer.getNumSamples(); offset += blockSize)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - offset);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                       buffer.getNumChannels(), offset, count);
        dsp.processBlock (view);
    }
}

double rmsOf (const juce::AudioBuffer<float>& buffer, int start, int length, int channel)
{
    double sum = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const auto value = static_cast<double> (buffer.getReadPointer (channel)[start + i]);
        sum += value * value;
    }

    return std::sqrt (sum / static_cast<double> (juce::jmax (1, length)));
}

class DspTests final : public juce::UnitTest
{
public:
    DspTests() : juce::UnitTest ("PRISM Dsp", "prism") {}

    void runTest() override
    {
        testIdentity();
        testCompression();
        testSilence();
        testLatency();
        testSmoothing();
    }

private:
    void testIdentity()
    {
        beginTest ("identity: AMOUNT 0, MIX 1, unity gain");

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 0.0f;
        params.mix01 = 1.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> buffer (2, 8192);
        juce::Random random (1234);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.getWritePointer (ch)[i] = (random.nextFloat() * 2.0f - 1.0f) * 0.25f;

        const juce::AudioBuffer<float> reference (buffer);

        processInBlocks (dsp, buffer, kProcessBlock);

        double maxError = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples() - prism::PrismDsp::kLatencySamples; ++i)
            {
                const auto output = static_cast<double> (
                    buffer.getReadPointer (ch)[i + prism::PrismDsp::kLatencySamples]);
                const auto input = static_cast<double> (reference.getReadPointer (ch)[i]);
                maxError = juce::jmax (maxError, std::abs (output - input));
            }

        logMessage ("max identity error after latency: " + juce::String (maxError, 12));
        expectLessThan (maxError, 1.0e-4);
    }

    void testCompression()
    {
        beginTest ("compression: AMOUNT 100 reduces a 1 kHz tone");

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 1.0f;
        params.mix01 = 1.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        const auto amplitude = static_cast<float> (std::pow (10.0, -6.0 / 20.0));
        constexpr int kLength = 72000; // 1.5 s

        juce::AudioBuffer<float> buffer (2, kLength);
        fillSine (buffer, 1000.0, amplitude);

        processInBlocks (dsp, buffer, kProcessBlock);

        const auto inputRms = static_cast<double> (amplitude) / std::sqrt (2.0);
        const auto outputRms = rmsOf (buffer, kLength - 24000, 24000, 0);
        const auto reductionDb = 20.0 * std::log10 (outputRms / inputRms);

        logMessage ("1 kHz -6 dBFS, output vs input: " + juce::String (reductionDb, 2) + " dB");
        expectLessThan (reductionDb, -3.0);
    }

    void testSilence()
    {
        beginTest ("silence produces no NaN/Inf");

        prism::PrismDsp dsp;
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> buffer (2, 48000);
        buffer.clear();

        processInBlocks (dsp, buffer, kProcessBlock);

        bool finite = true;
        float peak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (buffer.getReadPointer (ch)[i]));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                finite = finite && std::isfinite (buffer.getReadPointer (ch)[i]);

        for (auto& meter : dsp.meters())
        {
            const auto value = meter.load (std::memory_order_relaxed);
            finite = finite && std::isfinite (value) && value >= 0.0f && value < 1.0f;
        }

        logMessage ("silence peak: " + juce::String (peak, 12));
        expect (finite);
        expectLessThan (peak, 1.0e-6f);
    }

    void testLatency()
    {
        beginTest ("latency: impulse peak lands at 1152");

        expectEquals (prism::PrismDsp::kLatencySamples, 1152);

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 0.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, kProcessBlock, 2);

        juce::AudioBuffer<float> buffer (2, 4096);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);

        processInBlocks (dsp, buffer, kProcessBlock);

        int peakIndex = 0;
        float peakValue = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = std::abs (buffer.getReadPointer (ch)[i]);

                if (value > peakValue)
                {
                    peakValue = value;
                    peakIndex = i;
                }
            }

        logMessage ("impulse peak at sample " + juce::String (peakIndex)
                    + ", value " + juce::String (peakValue, 6));
        expectEquals (peakIndex, 1152);
        expectWithinAbsoluteError (peakValue, 1.0f, 1.0e-2f);
    }

    void testSmoothing()
    {
        beginTest ("smoothing: AMOUNT jump settles within 20 ms, no step");

        prism::PrismDsp dsp;
        prism::EngineParams params;
        params.amount01 = 0.0f;
        dsp.setParams (params);
        dsp.prepare (kSampleRate, 1024, 2);

        constexpr int kSettleSamples = 960; // 20 ms at 48 kHz
        constexpr int kLength = 4800 + kSettleSamples;

        juce::AudioBuffer<float> buffer (2, kLength);
        fillSine (buffer, 1000.0, 0.5f);

        // 先跑 100 ms 稳定状态，再突跳 AMOUNT。
        {
            juce::AudioBuffer<float> head (buffer.getArrayOfWritePointers(), 2, 0, 4800);
            processInBlocks (dsp, head, 480);
        }

        dsp.setParams ([&] {
            auto jumped = params;
            jumped.amount01 = 1.0f;
            return jumped;
        }());

        {
            juce::AudioBuffer<float> tail (buffer.getArrayOfWritePointers(), 2, 4800, kSettleSamples);
            dsp.processBlock (tail);
        }

        const auto smoothed = dsp.currentAmount01();
        logMessage ("smoothed AMOUNT after 20 ms: " + juce::String (smoothed, 6));

        float maxDelta = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxDelta = juce::jmax (maxDelta,
                                       std::abs (buffer.getReadPointer (ch)[i]
                                                 - buffer.getReadPointer (ch)[i - 1]));

        logMessage ("max adjacent sample delta: " + juce::String (maxDelta, 6));
        expectGreaterThan (smoothed, 0.99f);
        expectLessThan (maxDelta, 0.2f);
    }
};

DspTests dspTests;

} // namespace
