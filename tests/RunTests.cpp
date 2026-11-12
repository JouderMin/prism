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
