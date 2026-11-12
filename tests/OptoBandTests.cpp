#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "dsp/BandTable.h"
#include "dsp/OptoBand.h"

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr double kToneHz = 1000.0;
constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

float ampForDbfs (float levelDbfs)
{
    return juce::Decibels::decibelsToGain (levelDbfs);
}

/** 以 levelDbfs 的 1 kHz 正弦驱动 seconds 秒。

    gains 非空时逐样本记录当前增益。返回最后一段（后半程）的 RMS。
*/
float driveLevel (prism::OptoBand& band,
                  float levelDbfs,
                  double seconds,
                  std::vector<float>* gains = nullptr)
{
    const auto amp = ampForDbfs (levelDbfs);
    const auto numSamples = static_cast<int> (std::lround (seconds * kSampleRate));

    double sum = 0.0;
    auto count = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = amp * static_cast<float> (std::sin (kTwoPi * kToneHz * i / kSampleRate));
        const auto y = band.processSample (x);

        if (gains != nullptr)
            gains->push_back (band.currentGain());

        if (i >= numSamples / 2)
        {
            sum += static_cast<double> (y) * y;
            ++count;
        }
    }

    return static_cast<float> (std::sqrt (sum / juce::jmax (1, count)));
}

} // namespace

class OptoBandTests final : public juce::UnitTest
{
public:
    OptoBandTests() : juce::UnitTest ("PRISM OptoBand", "prism") {}

    void runTest() override
    {
        testStaticSlope();
        testReleaseLinearity();
        testSilenceAndTinyInput();
        testGainRange();
        testDetectorTimeConstants();
    }

private:
    void testStaticSlope()
    {
        beginTest ("static slope matches spec 5.4 targets (+/-0.5)");

        for (int b = 0; b < static_cast<int> (prism::kBands.size()); ++b)
        {
            prism::OptoBand band (b);
            band.prepare (kSampleRate);
            band.setDrive (0.0f);

            const auto out18 = driveLevel (band, -18.0f, 0.6);
            const auto out6 = driveLevel (band, -6.0f, 0.6);

            const auto ratio = 12.0f / juce::Decibels::gainToDecibels (out6 / out18);

            logMessage ("band " + juce::String (b + 1) + " (" + juce::String (prism::kBands[static_cast<size_t> (b)].colorName)
                        + ") slope = " + juce::String (static_cast<double> (ratio), 3) + " : 1, target "
                        + juce::String (static_cast<double> (prism::kBands[static_cast<size_t> (b)].targetSlope), 2));

            expectWithinAbsoluteError (ratio, prism::kBands[static_cast<size_t> (b)].targetSlope, 0.5f);
        }
    }

    void testReleaseLinearity()
    {
        beginTest ("release is near-linear in dB domain (20%-80% fit deviation < 1 dB)");

        for (int b = 0; b < static_cast<int> (prism::kBands.size()); ++b)
        {
            prism::OptoBand band (b);
            band.prepare (kSampleRate);
            band.setDrive (0.0f);

            driveLevel (band, -18.0f, 1.0);
            driveLevel (band, -6.0f, 0.4);

            std::vector<float> gains;
            gains.reserve (static_cast<size_t> (2.5 * kSampleRate));
            driveLevel (band, -18.0f, 2.5, &gains);

            std::vector<double> db;
            db.reserve (gains.size());

            for (const auto g : gains)
                db.push_back (20.0 * std::log10 (juce::jmax (1.0e-6, static_cast<double> (g))));

            const auto dbStart = db.front();
            const auto dbEnd = db.back();
            const auto span = dbEnd - dbStart;

            double sumT = 0.0, sumY = 0.0, sumTT = 0.0, sumTY = 0.0;
            auto count = 0;

            for (size_t i = 0; i < db.size(); ++i)
            {
                const auto progress = (db[i] - dbStart) / span;

                if (progress < 0.2 || progress > 0.8)
                    continue;

                const auto t = static_cast<double> (i) / kSampleRate;
                sumT += t;
                sumY += db[i];
                sumTT += t * t;
                sumTY += t * db[i];
                ++count;
            }

            expectGreaterThan (count, 8);

            const auto n = static_cast<double> (count);
            const auto denom = n * sumTT - sumT * sumT;
            expectGreaterThan (denom, 0.0);

            const auto slope = (n * sumTY - sumT * sumY) / denom;
            const auto intercept = (sumY - slope * sumT) / n;

            auto maxDeviation = 0.0;

            for (size_t i = 0; i < db.size(); ++i)
            {
                const auto progress = (db[i] - dbStart) / span;

                if (progress < 0.2 || progress > 0.8)
                    continue;

                const auto t = static_cast<double> (i) / kSampleRate;
                maxDeviation = juce::jmax (maxDeviation, std::abs (db[i] - (slope * t + intercept)));
            }

            logMessage ("band " + juce::String (b + 1) + " release " + juce::String (span, 2)
                        + " dB, max deviation = " + juce::String (maxDeviation, 3) + " dB");

            expectLessThan (maxDeviation, 1.0);
        }
    }

    void testSilenceAndTinyInput()
    {
        beginTest ("silence and tiny input produce no NaN/Inf");

        for (int b = 0; b < static_cast<int> (prism::kBands.size()); ++b)
        {
            prism::OptoBand band (b);
            band.prepare (kSampleRate);
            band.setDrive (12.0f);

            double silentSum = 0.0;

            for (int i = 0; i < 48000; ++i)
            {
                const auto y = band.processSample (0.0f);
                silentSum += static_cast<double> (y) * y;
                expect (std::isfinite (y));
            }

            expectEquals (silentSum, 0.0);
            expectWithinAbsoluteError (band.currentGain(), 1.0f, 1.0e-6f);

            const auto amp = 1.0e-9f;

            for (int i = 0; i < 48000; ++i)
            {
                const auto x = amp * static_cast<float> (std::sin (kTwoPi * kToneHz * i / kSampleRate));
                const auto y = band.processSample (x);
                expect (std::isfinite (y));
                expect (std::abs (y) <= std::abs (x) + 1.0e-12f);
            }

            expect (std::isfinite (band.currentLight()));
            expect (std::isfinite (band.currentGain()));
        }
    }

    void testGainRange()
    {
        beginTest ("light-to-gain mapping stays within [10k/160k, 1]");

        const float levels[] = { -60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f };
        const float drives[] = { 0.0f, 24.0f };

        for (const auto drive : drives)
        {
            for (int b = 0; b < static_cast<int> (prism::kBands.size()); ++b)
            {
                prism::OptoBand band (b);
                band.prepare (kSampleRate);
                band.setDrive (drive);

                auto minGain = 1.0f;
                auto maxGain = 0.0f;

                for (const auto level : levels)
                {
                    band.reset();
                    const auto amp = ampForDbfs (level);

                    for (int i = 0; i < static_cast<int> (0.3 * kSampleRate); ++i)
                    {
                        band.processSample (amp * static_cast<float> (std::sin (kTwoPi * kToneHz * i / kSampleRate)));
                        minGain = juce::jmin (minGain, band.currentGain());
                        maxGain = juce::jmax (maxGain, band.currentGain());

                        const auto light = band.currentLight();
                        expect (light >= 0.0f && light < 1.0f);
                    }
                }

                logMessage ("band " + juce::String (b + 1) + " drive " + juce::String (static_cast<double> (drive), 1)
                            + " dB: gain range [" + juce::String (static_cast<double> (minGain), 4) + ", "
                            + juce::String (static_cast<double> (maxGain), 4) + "]");

                expectGreaterOrEqual (minGain, prism::kMinGain - 1.0e-6f);
                expectLessOrEqual (maxGain, 1.0f + 1.0e-6f);
            }
        }
    }

    void testDetectorTimeConstants()
    {
        beginTest ("detector envelope: attack 10 ms +/-4, release 60 ms +/-25");

        for (int b = 0; b < static_cast<int> (prism::kBands.size()); ++b)
        {
            prism::OptoBand band (b);
            band.prepare (kSampleRate);
            band.setDrive (0.0f);

            // 恒值阶跃（不是正弦），包络按纯一阶上升/下降，63% 点即时间常数。
            const auto step = ampForDbfs (-12.0f);
            const auto attackTarget = 0.632f * step;

            auto attackSamples = -1;

            for (int i = 0; i < 20000; ++i)
            {
                band.processSample (step);

                if (band.currentEnvelope() >= attackTarget)
                {
                    attackSamples = i + 1;
                    break;
                }
            }

            expectGreaterThan (attackSamples, 0);

            const auto releaseStart = band.currentEnvelope();
            const auto releaseTarget = 0.368f * releaseStart;
            auto releaseSamples = -1;

            for (int i = 0; i < 200000; ++i)
            {
                band.processSample (0.0f);

                if (band.currentEnvelope() <= releaseTarget)
                {
                    releaseSamples = i + 1;
                    break;
                }
            }

            expectGreaterThan (releaseSamples, 0);

            const auto attackMs = 1000.0 * static_cast<double> (attackSamples) / kSampleRate;
            const auto releaseMs = 1000.0 * static_cast<double> (releaseSamples) / kSampleRate;

            logMessage ("band " + juce::String (b + 1) + " attack = " + juce::String (attackMs, 2)
                        + " ms, release = " + juce::String (releaseMs, 2) + " ms");

            expectWithinAbsoluteError (attackMs, 10.0, 4.0);
            expectWithinAbsoluteError (releaseMs, 60.0, 25.0);
        }
    }
};

static OptoBandTests optoBandTests;
