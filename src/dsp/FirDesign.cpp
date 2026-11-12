#include "FirDesign.h"

#include <cmath>
#include <juce_dsp/juce_dsp.h>

namespace prism
{

std::vector<float> designLowpass (float cutoffHz,
                                  double sampleRate,
                                  int taps,
                                  double kaiserBeta)
{
    jassert (sampleRate > 0.0);
    jassert (taps >= 3 && (taps % 2) == 1);
    jassert (cutoffHz > 0.0f && static_cast<double> (cutoffHz) < sampleRate * 0.5);

    const auto numTaps = static_cast<size_t> (taps);
    std::vector<float> h (numTaps, 0.0f);
    std::vector<float> window (numTaps, 1.0f);

    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.data(), numTaps,
        juce::dsp::WindowingFunction<float>::kaiser,
        false, static_cast<float> (kaiserBeta));

    const auto centre = taps / 2;
    const auto twoFcOverFs = 2.0 * static_cast<double> (cutoffHz) / sampleRate;
    const auto pi = juce::MathConstants<double>::pi;

    for (int n = 0; n < taps; ++n)
    {
        const auto x = twoFcOverFs * static_cast<double> (n - centre);
        const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
        h[static_cast<size_t> (n)] = static_cast<float> (twoFcOverFs * sinc
                                                          * static_cast<double> (window[static_cast<size_t> (n)]));
    }

    // 强制对称：浮点计算可能留下 1 ulp 的不对称。
    for (int n = 0; n < centre; ++n)
        h[static_cast<size_t> (taps - 1 - n)] = h[static_cast<size_t> (n)];

    // DC 归一化：sum(h) = 1。
    double sum = 0.0;

    for (const auto value : h)
        sum += static_cast<double> (value);

    if (std::abs (sum) > 1.0e-20)
    {
        const auto invSum = static_cast<float> (1.0 / sum);

        for (auto& value : h)
            value *= invSum;
    }

    return h;
}

} // namespace prism
