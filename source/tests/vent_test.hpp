#pragma once
//
// vent tests.
// minimal self-contained test harness.
// ——————————————————————
//
// a tiny, dependency-free test runner. deliberately NOT doctest/catch2: the
// container storm tests below don't need rich assertion macros or subcases —
// their real "assertion" is "after N threads hammer the structure, the tallies
// match". a header we own keeps the test target buildable offline (no
// FetchContent), which matters for a reproducible ci storm rig. the macro names
// (TEST_CASE / CHECK / REQUIRE) mirror doctest on purpose, so swapping to the
// real thing later is a mechanical change.
//
// usage:
//   TEST_CASE("name") { CHECK(cond); REQUIRE(cond); }
//   int main() { return vent::test::run_all(); }
//
// CHECK  - records a failure and continues (thread-safe, callable from workers).
// REQUIRE- records a failure and aborts the current test case (main thread).

#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <vector>

namespace vent::test {

// --- registry ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief a single registered test case: a name and the function to run.
struct test_case {
    const char* name;  ///< human-readable case name.
    void (*fn)();      ///< the test body.
};

/// @brief global test registry. function-local static avoids static-init-order
///        issues between translation units registering cases.
/// @return the mutable list of registered cases.
inline auto registry() -> std::vector<test_case>& {
    static std::vector<test_case> cases;
    return cases;
}

/// @brief registers a test case at static-init time (one instance per TEST_CASE).
struct registrar {
    registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

// --- counters & reporting ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief total number of CHECK/REQUIRE evaluations across all cases.
inline auto checks_total() -> std::atomic<long>& {
    static std::atomic<long> v {0};
    return v;
}

/// @brief number of failed CHECK/REQUIRE evaluations across all cases.
inline auto checks_failed() -> std::atomic<long>& {
    static std::atomic<long> v {0};
    return v;
}

/// @brief serializes failure prints so concurrent CHECKs don't interleave.
inline auto io_mutex() -> std::mutex& {
    static std::mutex m;
    return m;
}

/// @brief thrown by REQUIRE to abort the current test case.
struct require_failed : std::exception {};

/// @brief record one assertion result. thread-safe: storm tests call this from
///        worker threads.
/// @param ok the asserted condition.
/// @param expr the stringized expression (for the failure message).
/// @param file source file of the assertion.
/// @param line source line of the assertion.
/// @return the value of `ok` (so REQUIRE can branch on it).
inline auto report(bool ok, const char* expr, const char* file, int line)
    -> bool {
    checks_total().fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        checks_failed().fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(io_mutex());
        std::fprintf(stderr, "    [fail] %s:%d: CHECK(%s)\n", file, line, expr);
    }
    return ok;
}

/// @brief run every registered case, print a summary, and return an exit code.
/// @return 0 if all cases passed, 1 otherwise (ctest reads this).
inline auto run_all() -> int {
    int failed_cases = 0;
    for (auto& tc : registry()) {
        const long before = checks_failed().load();
        std::printf("[ run  ] %s\n", tc.name);

        bool crashed = false;
        try {
            tc.fn();
        } catch (const require_failed&) {
            crashed = true;  // already reported by report().
        } catch (const std::exception& e) {
            crashed = true;
            std::printf("    [exc ] %s\n", e.what());
        } catch (...) {
            crashed = true;
            std::printf("    [exc ] unknown exception\n");
        }

        const bool ok = !crashed && (checks_failed().load() == before);
        std::printf(ok ? "[ pass ] %s\n" : "[ FAIL ] %s\n", tc.name);
        if (!ok) {
            ++failed_cases;
        }
    }

    const long total  = checks_total().load();
    const long failed = checks_failed().load();
    std::printf("\n==== %ld/%ld checks passed · %d case(s) failed ====\n",
                total - failed,
                total,
                failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace vent::test

// --- macros ---
// —————————————————————————————————————————————————————————————————————————————

#define VENT_TEST_CONCAT_(a, b) a##b
#define VENT_TEST_CONCAT(a, b) VENT_TEST_CONCAT_(a, b)

/// @brief define and auto-register a test case. body follows the macro.
#define TEST_CASE(NAME)                                                        \
    static void VENT_TEST_CONCAT(vent_test_fn_, __LINE__)();                   \
    static ::vent::test::registrar VENT_TEST_CONCAT(vent_test_reg_, __LINE__) {\
        NAME, &VENT_TEST_CONCAT(vent_test_fn_, __LINE__)};                     \
    static void VENT_TEST_CONCAT(vent_test_fn_, __LINE__)()

/// @brief non-fatal assertion. records failure, keeps going.
#define CHECK(COND) ::vent::test::report((COND), #COND, __FILE__, __LINE__)

/// @brief fatal assertion. records failure and aborts this case.
#define REQUIRE(COND)                                                          \
    do {                                                                       \
        if (!::vent::test::report((COND), #COND, __FILE__, __LINE__)) {        \
            throw ::vent::test::require_failed {};                             \
        }                                                                      \
    } while (0)
