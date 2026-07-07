//
// vent tests.
// test runner entry point.
// ——————————————————————
//
// all test cases self-register via the TEST_CASE macro (see vent_test.hpp), so
// main() just runs them. keeping the runner in its own translation unit means
// adding a new test file never touches this one.

#include "vent_test.hpp"

auto main() -> int {
    // unbuffered stdout: storm tests can crash a worker thread, and we must not
    // lose the "[ run ]" line that tells us which case was executing.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("=== vent container storm tests ===\n\n");
    return ::vent::test::run_all();
}
