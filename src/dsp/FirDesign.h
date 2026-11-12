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

#pragma once

#include <vector>

namespace prism
{

/** 分频段数与分频点数。 */
inline constexpr int kNumBands = 7;
inline constexpr int kNumCrossovers = 6;

/** 6 个分频点，单位 Hz。 */
inline constexpr double kCrossoverPointsHz[kNumCrossovers] = { 150.0, 500.0, 1200.0, 3000.0, 8000.0, 14000.0 };

/** 默认抽头数与 Kaiser beta。 */
inline constexpr int kDefaultTaps = 2049;
inline constexpr double kKaiserBeta = 8.0;

/** 窗函数法设计对称线性相位低通。

    返回 taps 个系数。窗为 Kaiser(beta)，DC 增益归一到 1.0。
    taps 必须为奇数；cutoffHz 必须落在 (0, sampleRate/2) 内。
*/
std::vector<float> designLowpass (float cutoffHz,
                                  double sampleRate,
                                  int taps,
                                  double kaiserBeta = kKaiserBeta);

} // namespace prism
