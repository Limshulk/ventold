#pragma once
//
// core module.
// fallback log system.
// ——————————————————————
//
// provides a minimal printf-based ic_log implementation for use during
// early initialization before the real log system is available, or after
// the log system has shut down.
//
// this is a singleton that exists in the core module, so it's always available
// even if the log module isn't linked. it provides the same interface as
// ic_log but outputs directly to console via std::print.
//
// usage:
//   - the log() accessor automatically returns this fallback when the
//     real log system isn't available or has shut down.
//   - code can use log()->info(...) uniformly throughout the engine.

#include <_vent/interfaces/ic_log.hpp>

#include <core/thread_registry.hpp>

#include <print>

namespace vent {

/// @brief minimal log implementation using std::print. used before the real
///        log system is initialized, or when the log module isn't linked.
class fallback_log final : public ic_log {
private:
    fallback_log()           = default;
    ~fallback_log() override = default;

    VENT_NO_COPY_MOVE(fallback_log);

public:
    // --- singleton access ---
    // —————————————————————————————————————————————————————————————————————————
    // the fallback log is a singleton - statically allocated in the core module
    // and always available.

    /// @brief get the singleton fallback log instance.
    static auto instance() -> fallback_log& {
        static fallback_log instance;
        return instance;
    }

private:
    // --- constants ---
    // —————————————————————————————————————————————————————————————————————————

    static constexpr auto level_name(log_level lvl) -> const char* {
        switch (lvl) {
            case log_level::trace: return "T";
            case log_level::debug: return "D";
            case log_level::info: return "I";
            case log_level::warning: return "W";
            case log_level::error: return "E";
            case log_level::critical: return "F";
            default: return "?";
        }
    }

    // ansi color codes.
    static constexpr auto level_color(log_level lvl) -> const char* {
        switch (lvl) {
            case log_level::trace: return "\033[90m";       // gray.
            case log_level::debug: return "\033[36m";       // cyan.
            case log_level::info: return "\033[32m";        // green.
            case log_level::warning: return "\033[33m";     // yellow.
            case log_level::error: return "\033[31m";       // red.
            case log_level::critical: return "\033[35;1m";  // bold magenta.
            default: return "";
        }
    }

    static constexpr auto reset_color() -> const char* { return "\033[0m"; }

    /// @brief get friendly name for current thread.
    /// @return thread name as string (not a pointer, since it's a temporary).
    static auto get_thread_name() -> std::string {
        return thread_registry::get_name();
    }

    // --- ic_log implementation ---
    // —————————————————————————————————————————————————————————————————————————

    auto message(log_level   lvl,
                 const char* module,
                 const char* msg) -> void override {
#if !defined(VENT_DEBUG)
        // skip trace/debug in release.
        if (lvl <= log_level::debug)
            return;
#endif

        // format: [LEVEL] THREAD | [module] message.
        std::println("{}[{}]{} {:>4} | [{}] {}",
                     level_color(lvl),
                     level_name(lvl),
                     reset_color(),
                     get_thread_name(),
                     module,
                     msg);
    }

    auto message_full(log_level   lvl,
                      const char* module,
                      const char* msg,
                      const char* file,
                      const char* function,
                      u32         line) -> void override {
#if !defined(VENT_DEBUG)
        // skip trace/debug in release.
        if (lvl <= log_level::debug)
            return;
#endif

        // format: [LEVEL] NAME | [module] file:line | function
        //         msg: message.
        std::println("{}[{}]{} {:>4} | [{}] {}:{} | {}\n         msg: {}",
                     level_color(lvl),
                     level_name(lvl),
                     reset_color(),
                     get_thread_name(),
                     module,
                     file,
                     line,
                     function,
                     msg);
    }
};

}  // namespace vent
