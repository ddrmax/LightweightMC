// Minimal hand-rolled test harness for LightweightMC native C++ tests (Phase 12).
// Zero third-party deps by design; see wiki/Testing.md for the decision note.
#pragma once
#include <string>
#include <vector>

namespace lwmc_test {

struct TestCase {
    int failures = 0;
    std::string messages;

    void check(bool cond, const std::string &what) {
        if (!cond) {
            ++failures;
            messages += "  FAIL " + what + "\n";
        }
    }
};

} // namespace lwmc_test
