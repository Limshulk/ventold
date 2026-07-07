# vent build system.
# ——————————————————————
# builds the vent test runner: a single executable that storm-tests the
# hand-rolled lock-free containers (mpmc_queue, mpsc_queue, work_stealing_deque)
# and the circular_array they build on.
#
# the containers are header-only templates, so this target links no engine
# module — it just needs their include directories. run via ctest, or directly:
#   cmake --build --preset windows-debug --target vent_tests
#   ctest --preset windows-debug            (or run the exe in build/.../tests/)
#
# for real data-race detection, configure the linux preset with
# -DVENT_TESTS_TSAN=ON (gcc/clang only) and run the exe under ThreadSanitizer.

vent_log("configuring tests...")

add_executable(vent_tests
    test_main.cpp
    test_circular_array.cpp
    test_mpmc_queue.cpp
    test_mpsc_queue.cpp
    test_work_stealing_deque.cpp
    test_co_task.cpp
)

# common warnings/visibility + (on mingw) stdc++exp for std::print-family.
vent_set_common_properties(vent_tests)

# include paths: the containers live in module public/private trees.
target_include_directories(vent_tests PRIVATE
    "${VENT_ENGINE_DIR}"                    # <_vent/vent_sdk.hpp>.
    "${VENT_MODULES_DIR}/core/public"       # <core/containers/*.hpp>.
    "${VENT_MODULES_DIR}/job/private"       # <work_stealing_deque.hpp> (job-private).
    "${CMAKE_CURRENT_SOURCE_DIR}"           # vent_test.hpp.
)

# std::thread needs the platform threading library.
find_package(Threads REQUIRED)
target_link_libraries(vent_tests PRIVATE Threads::Threads)

# --- optional thread sanitizer (linux, gcc/clang) ---
# tsan is the real prize: it turns "passes by luck at -O0" into "provably free of
# data races on the exercised interleavings". only meaningful on linux gcc/clang;
# mingw has no tsan, so we warn and ignore elsewhere.

option(VENT_TESTS_TSAN "build vent_tests with ThreadSanitizer" OFF)
if(VENT_TESTS_TSAN)
    if(VENT_PLATFORM_LINUX AND (VENT_COMPILER_GCC OR VENT_COMPILER_CLANG))
        vent_log("  vent_tests: ThreadSanitizer ENABLED")
        # -O1 keeps tsan output readable while still optimizing enough to be useful.
        target_compile_options(vent_tests PRIVATE -fsanitize=thread -g -O1)
        target_link_options(vent_tests PRIVATE -fsanitize=thread)
    else()
        message(WARNING "[vent] VENT_TESTS_TSAN is only supported on linux gcc/clang; ignoring.")
    endif()
endif()

# keep the exe out of the sdk tree; tests are not shipped.
set_target_properties(vent_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# register with ctest. the exe returns non-zero if any case fails.
add_test(NAME vent_tests COMMAND vent_tests)

vent_log("  -> ${CMAKE_BINARY_DIR}/tests/vent_tests")
