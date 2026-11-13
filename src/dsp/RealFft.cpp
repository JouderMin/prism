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

#include "RealFft.h"

#include <cstdint>

#include <juce_core/juce_core.h>

namespace prism
{

namespace
{

#if ! defined(PRISM_USE_PFFFT)
int orderFor (int size) noexcept
{
    auto order = 0;

    while ((1 << order) < size)
        ++order;

    return order;
}
#endif

#if defined(PRISM_USE_PFFFT)
[[maybe_unused]] bool isAligned16 (const float* data) noexcept
{
    return (reinterpret_cast<std::uintptr_t> (data) & 0xFu) == 0;
}
#endif

} // namespace

#if defined(PRISM_USE_PFFFT)

void RealFft::SetupDeleter::operator() (PFFFT_Setup* setup) const noexcept
{
    if (setup != nullptr)
        pffft_destroy_setup (setup);
}

RealFft::RealFft (int size)
    : size_ (size),
      setup_ (pffft_new_setup (size, PFFFT_REAL)),
      packed_ (static_cast<size_t> (size), 0.0f),
      work_ (static_cast<size_t> (size), 0.0f)
{
    jassert (size_ >= pffft_min_fft_size (PFFFT_REAL));
    jassert (pffft_is_valid_size (size_, PFFFT_REAL) != 0);
    jassert (setup_ != nullptr);
}

RealFft::~RealFft() = default;

void RealFft::forwardReal (float* data) noexcept
{
    jassert (data != nullptr);
    jassert (isAligned16 (data));
    jassert (isAligned16 (packed_.data()));
    jassert (isAligned16 (work_.data()));

    if (setup_ == nullptr)
        return;

    // pffft ordered：packed[0]=DC、packed[1]=Nyquist、packed[2k/2k+1]=bin k。
    pffft_transform_ordered (setup_.get(), data, packed_.data(), work_.data(), PFFFT_FORWARD);

    // 展开成 JUCE 的完整复数谱：DC 在 bin 0，Nyquist 在 bin size/2，
    // 负频率 bin size−k 为共轭。
    const auto half = size_ / 2;

    data[0] = packed_[0];
    data[1] = 0.0f;
    data[size_] = packed_[1];
    data[size_ + 1] = 0.0f;

    for (int k = 1; k < half; ++k)
    {
        const auto re = packed_[static_cast<size_t> (2 * k)];
        const auto im = packed_[static_cast<size_t> (2 * k + 1)];

        data[static_cast<size_t> (2 * k)] = re;
        data[static_cast<size_t> (2 * k + 1)] = im;

        const auto mirror = static_cast<size_t> (2 * (size_ - k));
        data[mirror] = re;
        data[mirror + 1] = -im;
    }
}

void RealFft::inverseReal (float* data) noexcept
{
    jassert (data != nullptr);
    jassert (isAligned16 (data));
    jassert (isAligned16 (packed_.data()));
    jassert (isAligned16 (work_.data()));

    if (setup_ == nullptr)
        return;

    // 从完整谱压回 pffft ordered 格式。
    const auto half = size_ / 2;

    packed_[0] = data[0];
    packed_[1] = data[size_];

    for (int k = 1; k < half; ++k)
    {
        packed_[static_cast<size_t> (2 * k)] = data[static_cast<size_t> (2 * k)];
        packed_[static_cast<size_t> (2 * k + 1)] = data[static_cast<size_t> (2 * k + 1)];
    }

    pffft_transform_ordered (setup_.get(), packed_.data(), data, work_.data(), PFFFT_BACKWARD);

    // pffft 的反变换不归一化；JUCE 的 inverse 已含 1/N。
    const auto scale = 1.0f / static_cast<float> (size_);

    for (int i = 0; i < size_; ++i)
        data[i] *= scale;
}

#else

RealFft::RealFft (int size)
    : size_ (size),
      fft_ (orderFor (size))
{
    jassert (size_ > 0 && (size_ & (size_ - 1)) == 0);
}

RealFft::~RealFft() = default;

void RealFft::forwardReal (float* data) noexcept
{
    jassert (data != nullptr);
    fft_.performRealOnlyForwardTransform (data, false);
}

void RealFft::inverseReal (float* data) noexcept
{
    jassert (data != nullptr);
    fft_.performRealOnlyInverseTransform (data);
}

#endif

} // namespace prism
