#pragma once
//
// vent public sdk.
// ——————————————————————
//
// general types and macros for vent sdk and vent engine.

// --- platform detetion ---
// —————————————————————————————————————————————————————————————————————————————
// these macros are set by cmake.
// as a fallback, we define them here.
// warning: not using cmake is undefined behavior and
// not tested.

// defines one of:
//   VENT_LINUX   - linux platform.
//   VENT_WINDOWS - windows platform.
//   VENT_MACOS   - macos platform.

#if !defined(VENT_LINUX) && !defined(VENT_WINDOWS) && !defined(VENT_MACOS)
    #if defined(__linux__)
        #define VENT_LINUX
    #elif defined(_WIN32) || defined(_WIN64)
        #define VENT_WINDOWS
        #ifndef NOMINMAX
            #define NOMINMAX
        #endif
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
    #elif defined(__APPLE__) && defined(__MACH__)
        #define VENT_MACOS
    #endif
#endif

// --- export / import macros ---
// —————————————————————————————————————————————————————————————————————————————
// this defines the VENT_API macro for exporting / importing symbols.

#ifdef VENT_WINDOWS
    #define VENT_API
    #define VENT_INTERNAL
#else
    #define VENT_API __attribute__((visibility("default")))
    #define VENT_INTERNAL __attribute__((visibility("hidden")))
#endif

// --- plugin export / import macros ---
// —————————————————————————————————————————————————————————————————————————————
// plugins are shared libraries (.dll/.so) and need proper export/import on
// windows. VENT_PLUGIN_EXPORT should be defined when building the plugin dll
// itself.

#ifdef VENT_WINDOWS
    #ifdef VENT_PLUGIN_EXPORT
        #define VENT_PLUGIN_API __declspec(dllexport)
    #else
        #define VENT_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define VENT_PLUGIN_API __attribute__((visibility("default")))
#endif

// --- vent-specific macros ---
// —————————————————————————————————————————————————————————————————————————————

#define VENT_EXTERN_C extern "C"

// --- forced inlines ---
#if defined(_MSC_VER)
    #define VENT_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define VENT_INLINE inline __attribute__((always_inline))
#else
    #define VENT_INLINE inline
#endif

// --- non-copyable class ---
#define VENT_NO_COPY(class_name)                       \
    class_name(const class_name&)            = delete; \
    class_name& operator=(const class_name&) = delete;

// --- non-moveable class ---
#define VENT_NO_MOVE(class_name)                        \
    class_name(const class_name&&)            = delete; \
    class_name& operator=(const class_name&&) = delete;

// --- non-copy & non-move ---
#define VENT_NO_COPY_MOVE(class_name) \
    VENT_NO_COPY(class_name)          \
    VENT_NO_MOVE(class_name)

// --- quick & dirty profiling with TIC and TOC ---
#include <chrono>
#include <print>

#define TIC auto __vent_tic = std::chrono::high_resolution_clock::now();

#define TOC                                                              \
    do {                                                                 \
        using namespace std::chrono;                                     \
        auto __vent_toc = high_resolution_clock::now();                  \
        auto __vent_ns =                                                 \
            duration_cast<nanoseconds>(__vent_toc - __vent_tic).count(); \
        if (__vent_ns < 1'000) {                                         \
            std::print("PROFILE: {} ns\n", __vent_ns);                   \
        } else {                                                         \
            std::print("PROFILE: {:.3f} µs\n", __vent_ns / 1'000.0);     \
        }                                                                \
    } while (0);

// --- base types ---
// —————————————————————————————————————————————————————————————————————————————

#include <cstdint>
#include <cstddef>

namespace vent {

// fixed-size integer types.
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;

/// @brief cache line size for padding/alignment. this is cpu dependent, but
/// usually 64 bit.
/// we could check this with {$ getconf LEVEL1_DCACHE_LINESIZE} as a
/// terminal call, but i don't bother with that. also, i have no idea
/// how this works on windows.
constexpr auto CACHE_LINE = 64u;

}  // namespace vent

// --- engine entry point ---
// —————————————————————————————————————————————————————————————————————————————
// defines the engine entry point function that the launcher will call.
// the default launcher loads libvent_engine.so, finds this entry point and
// calls it.

namespace vent {

// --- engine configuration ---
// —————————————————————————————————————————————————————————————————————————————
// passed from launcher to engine at startup.

/// @brief configuration passed from launcher to engine at startup.
/// contains application metadata and list of startup plugins to load.
/// note: startup plugins are loaded automatically at engine init. other plugins
/// (listed in PLUGINS but not STARTUP_PLUGINS) are available for dynamic
/// loading by systems.
struct engine_config {
    const char*        app_id;   ///< application identifier.
    const char* const* plugins;  ///< array of startup plugin names to load.
    u32                plugin_count;  ///< number of startup plugins.
    int                argc;          ///< command line argument count.
    char**             argv;          ///< command line argument values.
};

/// @brief engine entry point.
/// @param config engine configuration from launcher.
/// @return exit code (0 = success).
using vent_engine_entry_fn = auto (*)(const engine_config& config) -> int;

}  // namespace vent