#pragma once
//
// vent public sdk.
// client-facing log_system interface.
// ——————————————————————
//
// provides logging functionality for clients and engine-internals.
// different implementations might come with different functionality. this
// interfaces lays a general ground for logging.

// todo: this code is aweful and should be rewritten at some point. also think
// todo: about the 4096 buffer at the stack!

#include <_vent/vent_sdk.hpp>

#include <algorithm>
#include <concepts>
#include <format>
#include <source_location>
#include <string_view>

namespace vent {

enum class log_level : u8 {
    trace = 0,  ///< very detailed debug information. disabled in release.
    debug,      ///< general debug information. disabled in release.
    info,       ///< general runtime information.
    warning,    ///< unexpected issues that are recoverable.
    error,  ///< serious issues that may or may not lead to failure right now or
            ///< later.
    critical  ///< unrecoverable errors that lead to immediate failure.
};

enum class log_channel : u8 {
    none    = 0,              ///< no output.
    console = 1 << 0,         ///< output to console only.
    file    = 1 << 1,         ///< output to file only.
    all     = console | file  ///< all outputs.
};
inline auto operator|(log_channel a, log_channel b) -> log_channel {
    return static_cast<log_channel>(static_cast<u8>(a) | static_cast<u8>(b));
}
inline auto operator&(log_channel a, log_channel b) -> log_channel {
    return static_cast<log_channel>(static_cast<u8>(a) & static_cast<u8>(b));
}
inline auto has_flag(log_channel flags, log_channel flag) -> bool {
    return (static_cast<u8>(flags) & static_cast<u8>(flag)) != 0;
}

namespace detail {

// wrapper that captures source location at compile time (on caller's side).
template <typename... args_t>
struct format_string_with_loc {
    std::format_string<args_t...> fmt;
    std::source_location          loc;

    template <typename T>
        requires std::convertible_to<const T&, std::string_view>
    consteval format_string_with_loc(
        const T&                    s,
        const std::source_location& l = std::source_location::current())
        : fmt(s),
          loc(l) {}
};

}  // namespace detail

class ic_log {
public:
    virtual ~ic_log() = default;

    static constexpr std::string_view system_name = "vent.system.log";

private:
    // --- main logging methods ---
    // —————————————————————————————————————————————————————————————————————————
    // accessible only by weird template wrappers in public section.

    /// @brief log a message with the specified level (short format, no source
    /// location).
    /// @param lvl severity level.
    /// @param module module name (e.g., "renderer", "physics").
    /// @param message the message text.
    virtual auto message(log_level lvl, const char* module, const char* message)
        -> void = 0;

    /// @brief log a message with full source location info (3-line format).
    /// @param lvl severity level.
    /// @param module module name.
    /// @param message the message text.
    /// @param file source file name.
    /// @param function function name.
    /// @param line line number.
    virtual auto message_full(log_level   lvl,
                              const char* module,
                              const char* message,
                              const char* file,
                              const char* function,
                              u32         line) -> void = 0;

public:
    VENT_API void format_and_log(log_level        lvl,
                                 const char*      module,
                                 std::string_view fmt,
                                 std::format_args args);
    VENT_API void format_and_log_full(log_level                   lvl,
                                      const char*                 module,
                                      const std::source_location& loc,
                                      std::string_view            fmt,
                                      std::format_args            args);

    // --- short format ---
    // —————————————————————————————————————————————————————————————————————————
    // 1-line format convenience methods.
    // usage: log->info("module", "message with {} args", 42);
    // note: we could use std::format for better code readability, but using a
    // buffer and format_to_n avoids dynamic memory allocations.

    template <typename... args_t>
    auto trace(const char*                   module,
               std::format_string<args_t...> fmt,
               args_t&&... args) -> void {
        format_and_log(log_level::trace,
                       module,
                       fmt.get(),
                       std::make_format_args(args...));
    }

    template <typename... args_t>
    auto debug(const char*                   module,
               std::format_string<args_t...> fmt,
               args_t&&... args) -> void {
        format_and_log(log_level::debug,
                       module,
                       fmt.get(),
                       std::make_format_args(args...));
    }

    template <typename... args_t>
    auto info(const char*                   module,
              std::format_string<args_t...> fmt,
              args_t&&... args) -> void {
        format_and_log(
            log_level::info, module, fmt.get(), std::make_format_args(args...));
    }

    template <typename... args_t>
    auto warn(const char*                   module,
              std::format_string<args_t...> fmt,
              args_t&&... args) -> void {
        format_and_log(log_level::warning,
                       module,
                       fmt.get(),
                       std::make_format_args(args...));
    }

    template <typename... args_t>
    auto error(const char*                   module,
               std::format_string<args_t...> fmt,
               args_t&&... args) -> void {
        format_and_log(log_level::error,
                       module,
                       fmt.get(),
                       std::make_format_args(args...));
    }

    template <typename... args_t>
    auto critical(const char*                   module,
                  std::format_string<args_t...> fmt,
                  args_t&&... args) -> void {
        format_and_log(log_level::critical,
                       module,
                       fmt.get(),
                       std::make_format_args(args...));
    }

    // --- full format ---
    // —————————————————————————————————————————————————————————————————————————
    // 3-line format with source location.
    // usage: log->info_f("module", "message with {} args", 42);

    template <typename... args_t>
    auto trace_f(
        const char*                                                     module,
        detail::format_string_with_loc<std::type_identity_t<args_t>...> fmt,
        args_t&&... args) -> void {
        format_and_log_full(log_level::trace,
                            module,
                            fmt.loc,
                            fmt.fmt.get(),
                            std::make_format_args(args...));
    }

    template <typename... args_t>
    auto debug_f(
        const char*                                                     module,
        detail::format_string_with_loc<std::type_identity_t<args_t>...> fmt,
        args_t&&... args) -> void {
        format_and_log_full(log_level::debug,
                            module,
                            fmt.loc,
                            fmt.fmt.get(),
                            std::make_format_args(args...));
    }

    template <typename... args_t>
    auto info_f(
        const char*                                                     module,
        detail::format_string_with_loc<std::type_identity_t<args_t>...> fmt,
        args_t&&... args) -> void {
        format_and_log_full(log_level::info,
                            module,
                            fmt.loc,
                            fmt.fmt.get(),
                            std::make_format_args(args...));
    }

    template <typename... args_t>
    auto warn_f(
        const char*                                                     module,
        detail::format_string_with_loc<std::type_identity_t<args_t>...> fmt,
        args_t&&... args) -> void {
        format_and_log_full(log_level::warning,
                            module,
                            fmt.loc,
                            fmt.fmt.get(),
                            std::make_format_args(args...));
    }

    template <typename... args_t>
    auto error_f(
        const char*                                                     module,
        detail::format_string_with_loc<std::type_identity_t<args_t>...> fmt,
        args_t&&... args) -> void {
        format_and_log_full(log_level::error,
                            module,
                            fmt.loc,
                            fmt.fmt.get(),
                            std::make_format_args(args...));
    }

    template <typename... args_t>
    auto critical_f(
        const char*                                                     module,
        detail::format_string_with_loc<std::type_identity_t<args_t>...> fmt,
        args_t&&... args) -> void {
        format_and_log_full(log_level::critical,
                            module,
                            fmt.loc,
                            fmt.fmt.get(),
                            std::make_format_args(args...));
    }
};

}  // namespace vent