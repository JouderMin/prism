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

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main()
{
    // APVTS 内部用 Timer；没有这个作用域，JUCE 关闭时 ShutdownDetector 会报泄漏。
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    auto failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    return failures == 0 ? 0 : 1;
}
