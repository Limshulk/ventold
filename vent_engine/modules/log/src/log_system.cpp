// log module.
// log_system implementation.
// ——————————————————————
//

#include <log_system.hpp>

#include <core/thread_registry.hpp>
#include <core/system/registration.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <thread>

#ifdef __linux__
    #include <pthread.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace vent {

// --- crazy helper ---
// ——————————————————————————————————————————————————————————————————————————————

#if !defined(_MSC_VER)
static auto safe_strncpy(char* dest, u64 size, const char* src) -> void {
    if (size == 0)
        return;
    std::strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}
#endif

// --- lifecycle ---
// ——————————————————————————————————————————————————————————————————————————————

auto log_system::initialize() -> bool {
    // prevent double initialization (atomic flag).
    bool expected = false;
    if (!_initialized.compare_exchange_strong(expected, true))
        return true;  // already initialized.

    // enable console colors.
    if (has_flag(_config.channels, log_channel::console)) {
        enable_console_colors();
    }

    // open log file if file logging is enabled.
    if (has_flag(_config.channels, log_channel::file)) {
        open_log_file();
    }

    // start the worker thread.
    _running.store(true, std::memory_order_release);
    _worker_thread = thread_registry::spawn_thread("VLOG", [this]() {
        worker_thread_func();
    });

    // write session banner.
    write_session_banner();

    return true;
}

auto log_system::shutdown() -> void {
    if (!_initialized.load(std::memory_order_relaxed))
        return;  // not initialized.

    // write session end banner.
    write_session_end_banner();

    // signal worker thread to stop.
    _running.store(false, std::memory_order_release);
    _semaphore.release();

    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }

    close_log_file();

    _initialized.store(false, std::memory_order_release);
}

log_system::~log_system() {
    // if the worker thread is still running, we need to stop it.
    _running.store(false, std::memory_order_release);
    _semaphore.release();

    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}

// todo: this fully formats the message on caller's thread. think about passing
// todo: raw data & format on worker thread for less overhead.
auto log_system::message(log_level lvl, const char* module, const char* msg)
    -> void {
    if (!_initialized.load(std::memory_order_relaxed))
        return;

#if !defined(VENT_DEBUG)
    // skip trace/debug messages in release builds.
    if (lvl <= log_level::debug)
        return;
#endif

    // create log entry.
    log_entry entry;
    entry.lvl               = lvl;
    entry.has_source_loc    = false;
    entry.line              = 0;
    entry.file[0]           = '\0';
    entry.function[0]       = '\0';
    std::string thread_name = get_current_thread_name();
    safe_strncpy(
        entry.thread_name, sizeof(entry.thread_name), thread_name.c_str());

    // capture timestamp now (when the log was called, not when it's written).
    get_timestamp(entry.timestamp, sizeof(entry.timestamp));

    // copy strings (safely, with truncation).
#if defined(_MSC_VER)
    strncpy_s(entry.module,
              sizeof(entry.module),
              module ? module : "unknown",
              _TRUNCATE);
    strncpy_s(entry.message, sizeof(entry.message), msg ? msg : "", _TRUNCATE);
#else
    safe_strncpy(
        entry.module, sizeof(entry.module), module ? module : "unknown");
    safe_strncpy(entry.message, sizeof(entry.message), msg ? msg : "");
#endif

    // push to queue (may drop if queue is full - that's ok for logging).
    if (_queue.push(entry)) {
        // signal worker thread.
        _semaphore.release();

        // for critical messages, block until the message is written.
        // the app may crash right after, so we need to ensure it's flushed.
        if (lvl == log_level::critical) {
            u64 target =
                _critical_pushed.fetch_add(1, std::memory_order_relaxed) + 1;
            wait_for_critical(target);
        }
    }
}

auto log_system::message_full(log_level   lvl,
                              const char* module,
                              const char* msg,
                              const char* file,
                              const char* function,
                              u32         line) -> void {

    if (!_initialized.load(std::memory_order_relaxed))
        return;

#if !defined(VENT_DEBUG)
    // skip trace/debug messages in release builds.
    if (lvl <= log_level::debug)
        return;
#endif

    // create log entry.
    log_entry entry;
    entry.lvl               = lvl;
    entry.has_source_loc    = true;
    entry.line              = line;
    std::string thread_name = get_current_thread_name();
    safe_strncpy(
        entry.thread_name, sizeof(entry.thread_name), thread_name.c_str());

    // copy strings (safely, with truncation).
#if defined(_MSC_VER)
    strncpy_s(entry.module,
              sizeof(entry.module),
              module ? module : "unknown",
              _TRUNCATE);
    strncpy_s(entry.message, sizeof(entry.message), msg ? msg : "", _TRUNCATE);
    strncpy_s(
        entry.file, sizeof(entry.file), file ? file : "unknown", _TRUNCATE);
    strncpy_s(entry.function,
              sizeof(entry.function),
              function ? function : "unknown",
              _TRUNCATE);
#else
    safe_strncpy(
        entry.module, sizeof(entry.module), module ? module : "unknown");
    safe_strncpy(entry.message, sizeof(entry.message), msg ? msg : "");
    safe_strncpy(entry.file, sizeof(entry.file), file ? file : "unknown");
    safe_strncpy(entry.function,
                 sizeof(entry.function),
                 function ? function : "unknown");
#endif

    // capture timestamp now (when the log was called, not when it's written).
    get_timestamp(entry.timestamp, sizeof(entry.timestamp));

    // push to queue (may drop if queue is full - that's ok for logging).
    if (_queue.push(entry)) {
        // signal worker thread.
        _semaphore.release();

        // for critical messages, block until the message is written.
        // the app may crash right after, so we need to ensure it's flushed.
        if (lvl == log_level::critical) {
            u64 target =
                _critical_pushed.fetch_add(1, std::memory_order_relaxed) + 1;
            wait_for_critical(target);
        }
    }
}

auto log_system::enable_console_colors() -> void {
#if defined(VENT_WINDOWS)
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_handle == INVALID_HANDLE_VALUE)
        return;

    DWORD mode = 0;
    if (!GetConsoleMode(stdout_handle, &mode))
        return;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(stdout_handle, mode))
        _colors_enabled = true;
#else
    // assume ANSI colors work on Unix-like systems.
    _colors_enabled = true;
#endif
}

auto log_system::get_current_thread_name() -> std::string {
    return thread_registry::get_name();
}
// get current date as YYYY_MM_DD string (for file names).
auto log_system::get_current_date_string() -> std::string {
    auto now      = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time {};
#if defined(VENT_WINDOWS)
    localtime_s(&local_time, &now_time);
#else
    localtime_r(&now_time, &local_time);
#endif

    return std::format("{:04}_{:02}_{:02}",
                       local_time.tm_year + 1900,
                       local_time.tm_mon + 1,
                       local_time.tm_mday);
}

// todo: this runs on caller's thread. think about just passing raw data here,
// todo: not formatting.
auto log_system::get_timestamp(char* buffer, u64 buffer_size) -> void {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::tm tm_buf {};
#if defined(VENT_WINDOWS)
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    snprintf(buffer,
             buffer_size,
             "%02d:%02d:%02d.%03d",
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec,
             static_cast<int>(ms.count()));
}

auto log_system::get_session_timestamp_string() -> std::string {
    auto now      = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    auto now_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

    std::tm local_time {};
#if defined(VENT_WINDOWS)
    localtime_s(&local_time, &now_time);
#else
    localtime_r(&now_time, &local_time);
#endif

    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                       local_time.tm_year + 1900,
                       local_time.tm_mon + 1,
                       local_time.tm_mday,
                       local_time.tm_hour,
                       local_time.tm_min,
                       local_time.tm_sec,
                       static_cast<int>(now_ms.count()));
}

auto log_system::build_log_file_path(const std::string& date, int index) const
    -> std::string {
    namespace fs = std::filesystem;
    if (index == 0) {
        return (fs::path(_config.log_dir) /
                std::format("{}_{}.log", _config.app_name, date))
            .string();
    } else {
        return (fs::path(_config.log_dir) /
                std::format("{}_{}_{}.log", _config.app_name, date, index))
            .string();
    }
}

auto log_system::open_log_file() -> void {
    if (!has_flag(_config.channels, log_channel::file))
        return;

    namespace fs = std::filesystem;

    // create log directory if needed.
    fs::path log_path(_config.log_dir);
    if (!fs::exists(log_path))
        fs::create_directories(log_path);

    _current_log_date   = get_current_date_string();
    _current_file_index = 0;

    // find correct file index to use.
    // todo: make max index configurable?
    for (u32 i = 0; i <= 999; ++i) {
        std::string path = build_log_file_path(_current_log_date, i);
        fs::path    fpath(path);

        if (!fs::exists(fpath)) {
            _current_file_index = i;
            break;
        }

        auto size = fs::file_size(fpath);
        if (size < _config.max_file_size) {
            _current_file_index = i;
            _current_file_size  = static_cast<u64>(size);
            break;
        }

        _current_file_index = i + 1;
    }

    // open log file for appending.
    _log_file_path =
        build_log_file_path(_current_log_date, _current_file_index);
    _log_file.open(_log_file_path, std::ios::app);

    if (_log_file.is_open()) {
        _current_file_size = static_cast<u64>(_log_file.tellp());
    }
}

auto log_system::close_log_file() -> void {
    if (!_log_file.is_open())
        return;
    _log_file.flush();
    _log_file.close();
}

auto log_system::rotate_log_file_if_needed() -> void {
    if (!has_flag(_config.channels, log_channel::file))
        return;

    std::string current_date = get_current_date_string();

    // date has changed - close current file and open new one.
    if (current_date != _current_log_date) {
        close_log_file();
        open_log_file();
        return;
    }

    // file size limit reached - rotate to new file.
    if (_current_file_size >= _config.max_file_size) {
        close_log_file();
        _current_file_index++;
        _current_file_size = 0;

        _log_file_path =
            build_log_file_path(_current_log_date, _current_file_index);
        _log_file.open(_log_file_path, std::ios::app);

        if (_log_file.is_open()) {
            _current_file_size = static_cast<u64>(_log_file.tellp());
        }
    }
}

auto log_system::write_session_banner() -> void {
    std::string timestamp = get_session_timestamp_string();

    // console banner.
    if (has_flag(_config.channels, log_channel::console)) {
        if (_colors_enabled) {
            std::print("{}", level_to_color(log_level::info));
        }
        std::println("========================================");
        std::println("  session start: {}", timestamp);
        std::println("========================================");
        if (_colors_enabled) {
            std::print("{}", COLOR_RESET);
        }
    }

    // file banner.
    if (has_flag(_config.channels, log_channel::file) && _log_file.is_open()) {
        _log_file << "========================================\n";
        _log_file << std::format("  session start: {}\n", timestamp);
        _log_file << "========================================\n";
        _log_file.flush();
        _current_file_size = static_cast<u64>(_log_file.tellp());
    }
}

auto log_system::write_session_end_banner() -> void {
    std::string timestamp = get_session_timestamp_string();

    // console banner.
    if (has_flag(_config.channels, log_channel::console)) {
        if (_colors_enabled) {
            std::print("{}", level_to_color(log_level::info));
        }
        std::println("========================================");
        std::println("  session end:   {}", timestamp);
        std::println("========================================");
        if (_colors_enabled) {
            std::print("{}", COLOR_RESET);
        }
    }

    // file banner.
    if (has_flag(_config.channels, log_channel::file) && _log_file.is_open()) {
        _log_file << "========================================\n";
        _log_file << std::format("  session end:   {}\n", timestamp);
        _log_file << "========================================\n";
        _log_file.flush();
        _current_file_size = static_cast<u64>(_log_file.tellp());
    }
}

auto log_system::write_entry_short(const log_entry& entry) -> void {
    std::string formatted = std::format("[{}] {} {:>4} | [{}] {}",
                                        level_to_string(entry.lvl),
                                        entry.timestamp,
                                        entry.thread_name,
                                        entry.module,
                                        entry.message);

    // console output (colored).
    if (has_flag(_config.channels, log_channel::console)) {
        std::ostream& output =
            (entry.lvl >= log_level::error) ? std::cerr : std::cout;
        if (_colors_enabled) {
            output << level_to_color(entry.lvl) << formatted << COLOR_RESET
                   << '\n';
        } else {
            output << formatted << '\n';
        }
        output.flush();
    }

    // file output (no colors).
    if (has_flag(_config.channels, log_channel::file) && _log_file.is_open()) {
        _log_file << formatted << '\n';
        _current_file_size = static_cast<u64>(_log_file.tellp());
        _log_file.flush();
    }
}

auto log_system::write_entry_full(const log_entry& entry) -> void {
    // header line.
    std::string header = std::format("[{}] {} {:>4} | [{}] {}:{}\n"
                                     "        fun : {}\n"
                                     "        msg : {}",
                                     level_to_string(entry.lvl),
                                     entry.timestamp,
                                     entry.thread_name,
                                     entry.module,
                                     entry.file,
                                     entry.line,
                                     entry.function,
                                     entry.message);

    // console output (colored).
    if (has_flag(_config.channels, log_channel::console)) {
        std::ostream& output =
            (entry.lvl >= log_level::error) ? std::cerr : std::cout;
        if (_colors_enabled) {
            output << level_to_color(entry.lvl) << header << COLOR_RESET
                   << '\n';
        } else {
            output << header << '\n';
        }
        output.flush();
    }

    // file output (no colors).
    if (has_flag(_config.channels, log_channel::file) && _log_file.is_open()) {
        _log_file << header << '\n';
        _current_file_size = static_cast<u64>(_log_file.tellp());
        _log_file.flush();
    }
}

auto log_system::write_entry(const log_entry& entry) -> void {
    // check for file rotation.
    rotate_log_file_if_needed();

    // choose format based on whether source location was captured.
    if (entry.has_source_loc) {
        write_entry_full(entry);
    } else {
        write_entry_short(entry);
    }

    // signal critical message completion so caller can unblock.
    if (entry.lvl == log_level::critical) {
        _critical_processed.fetch_add(1, std::memory_order_release);
    }
}

auto log_system::wait_for_critical(u64 target_count) -> void {
    // spin-wait until the worker thread has processed this critical message.
    // use a simple spin with yield to avoid burning CPU while waiting.
    // timeout after 1 second to prevent infinite hang if something goes wrong.
    constexpr u32 MAX_ITERATIONS = 10000;  // ~1 second with 100us sleeps.
    u32           iterations     = 0;

    while (_critical_processed.load(std::memory_order_acquire) < target_count) {
        if (++iterations > MAX_ITERATIONS) {
            // timeout - log may be stuck, don't block forever.
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

auto log_system::worker_thread_func() -> void {

    while (_running.load(std::memory_order_relaxed)) {
        // wait for signal (or timeout to check _running).
        if (_semaphore.try_acquire_for(std::chrono::milliseconds(100))) {
            // process all available entries.
            while (auto maybe = _queue.try_pop()) {
                write_entry(*maybe);
            }
        }
    }

    // flush remaining entries on shutdown.
    while (auto maybe = _queue.try_pop())
        write_entry(*maybe);
}

}  // namespace vent

// register the log system for automatic discovery.
VENT_REGISTER_SYSTEM(vent::log_system, vent::ic_log, vent::i_log)