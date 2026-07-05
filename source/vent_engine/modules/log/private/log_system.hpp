#pragma once
//
// log module.
// log_system implementation.
// ——————————————————————
//
// async logging system with lock-free queue for logging to console and files.
// supports file rotation after specified file size.
//
// note: this system uses a worker thread independent of the job system to avoid
// overhead and latency in the job system itself.

#include <log/interfaces/i_log.hpp>

#include <core/containers/mpsc_queue.hpp>
#include <core/interfaces/ir_bootstrap.hpp>

#include <_vent/system/system_base.hpp>

#include <array>
#include <fstream>
#include <semaphore>
#include <thread>

namespace vent {

// --- log entry ---
// ——————————————————————————————————————————————————————————————————————————————

// todo: add another entry for executing timestamp to get the execution delay in
// ms todo: between log requesting (engine thread) and log writing (worker
// thread).
struct log_entry {
    log_level lvl;             ///< log level.
    bool      has_source_loc;  ///< true if source location info is available.

    // todo: use fixed-size arrays with limits from limit::log::*
    char module[/*limit::log::module_name_size*/ 32];   ///< module name.
    char message[/*limit::log::message_size*/ 4096];    ///< message text.
    char timestamp[/*limit::log::timestamp_size*/ 32];  ///< pre-formatted
                                                        ///< timestamp
                                                        ///< (HH:MM:SS.mmm).

    char file[/*limit::log::file_path_size*/ 256];  ///< source file name.
    char function[/*limit::log::function_name_size*/ 128];  ///< function name.

    char thread_name[16];  ///< friendly thread name (e.g., "MAIN", "W:01").
    u32  line;             ///< line number.
};

// --- log config ---
// ——————————————————————————————————————————————————————————————————————————————

struct log_config {
    const char* app_name =
        "vent_application";  ///< application name used in log file name.
    const char* log_dir   = "logs";            ///< directory for log files.
    log_channel channels  = log_channel::all;  ///< enabled log channels.
    log_level   min_level = log_level::trace;  ///< minimum log level to output.
    u64 max_file_size = 1 * 1024 * 1024;  ///< maximum log file size in bytes.
};

// --- log system ---
// ——————————————————————————————————————————————————————————————————————————————

class log_system final
    : public system_base
    , public i_log
    , public ir_bootstrap {
public:
    log_system() = default;
    ~log_system() override;

    VENT_NO_COPY_MOVE(log_system);

public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————
    // log_system is a bootstrap system.

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_log::system_name;
    }

    [[nodiscard]]
    auto bootstrap_priority() const -> i32 override {
        return -100;  // extreme high priority.
    }

    [[nodiscard]]
    auto on_initialization() -> system_initialization_status override {
        return initialize() ? system_initialization_status::success
                            : system_initialization_status::failed;
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    // --- constants ---
    // —————————————————————————————————————————————————————————————————————————

    // log level names for channel output.
    static constexpr std::array<std::string_view, 6> LEVEL_NAMES {"T",
                                                                  "D",
                                                                  "I",
                                                                  "W",
                                                                  "E",
                                                                  "F"};

    // magic log level colors for console output.
    static constexpr std::array<std::string_view, 6> LEVEL_COLORS {
        "\033[90m",  // TRACE    - bright black (gray).
        "\033[36m",  // DEBUG    - cyan.
        "\033[32m",  // INFO     - green.
        "\033[33m",  // WARN     - yellow.
        "\033[31m",  // ERROR    - red.
        "\033[35m"   // CRITICAL - magenta.
    };

    // default color reset code.
    static constexpr std::string_view COLOR_RESET = "\033[0m";

    // --- member variables ---
    // —————————————————————————————————————————————————————————————————————————

    bool _colors_enabled = false;  ///< true if console colors are working.

    log_config _config {};  ///< current log configuration.

    // todo: make this inti limit::log::*
    mpsc_queue<log_entry> _queue {
        /*limit::log::queue_capacity*/ 4096};  ///< log entry queue.
    std::thread       _worker_thread;          ///< worker thread.
    std::atomic<bool> _initialized {false};    ///< true if initialized.
    std::atomic<bool> _running {false};        ///< controls worker thread loop.

    std::counting_semaphore<> _semaphore {
        0};  ///< semaphore to signal new log entries.

    // critical message synchronization.
    // used to block caller until critical messages are written (app may crash).
    std::atomic<u64> _critical_pushed {
        0};  ///< count of critical messages pushed.
    std::atomic<u64> _critical_processed {
        0};  ///< count of critical messages written.

    std::ofstream _log_file;               ///< current log file.
    std::string   _log_file_path;          ///< current log file path.
    std::string   _current_log_date;       ///< date string of current log file.
    u64           _current_file_size = 0;  ///< current log file size.
    u64 _current_file_index = 0;  ///< current log file index (for rotation).

    // --- ic_log ---
    // —————————————————————————————————————————————————————————————————————————

    auto message(log_level lvl, const char* module, const char* message)
        -> void override;

    auto message_full(log_level   lvl,
                      const char* module,
                      const char* message,
                      const char* file,
                      const char* function,
                      u32         line) -> void override;

    // --- internal helpers ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief convert log level to string.
    /// @param lvl log level.
    /// @return string representation of the log level.
    static constexpr auto level_to_string(log_level lvl) -> const char* {
        auto idx = static_cast<u64>(lvl);
        return idx < LEVEL_NAMES.size() ? LEVEL_NAMES[idx].data() : "?????";
    }

    /// @brief convert log level to ANSI color code.
    /// @param lvl log level.
    /// @return ANSI color code string.
    static constexpr auto level_to_color(log_level lvl) -> std::string_view {
        auto idx = static_cast<u64>(lvl);
        return idx < LEVEL_COLORS.size() ? LEVEL_COLORS[idx] : "";
    }

    /// @brief enable virtual terminal processing for console to support ANSI
    /// colors on microslop.
    auto enable_console_colors() -> void;

    /// @brief get friendly name for current thread.
    /// @return current thread name from thread_registry.
    auto get_current_thread_name() -> std::string;

    /// @brief get current date as string (YYYY-MM-DD).
    /// @return date string in YYYY-MM-DD format.
    static auto get_current_date_string() -> std::string;

    /// @brief get current timestamp as string (HH:MM:SS.mmm).
    /// @param buffer destination buffer.
    /// @param buffer_size size of the destination buffer.
    static auto get_timestamp(char* buffer, u64 buffer_size) -> void;

    /// @brief get session timestamp string for log file naming.
    /// @return session timestamp string.
    static auto get_session_timestamp_string() -> std::string;

    /// @brief block until a specific critical message has been processed.
    /// @param target_count the critical message count to wait for.
    auto wait_for_critical(u64 target_count) -> void;

    // --- file management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief build log file path for given date and index.
    /// @param date date string (YYYY_MM_DD).
    /// @param index log file index (0 = first file).
    /// @return constructed log file path.
    auto build_log_file_path(const std::string& date, int index) const
        -> std::string;

    /// @brief open log file for writing. creates directories as needed. finds
    /// any available files and appends to existing file if under size limit.
    /// creates new file if needed.
    auto open_log_file() -> void;

    /// @brief close the current log file if open.
    auto close_log_file() -> void;

    /// @brief called before every log. checks if log file needs rotation (date
    /// change or size limit). if so, rotates the log file.
    auto rotate_log_file_if_needed() -> void;

    // --- write log messages ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief write session banner to console and log file.
    auto write_session_banner() -> void;

    /// @brief write session end banner to console and log file.
    auto write_session_end_banner() -> void;

    /// @brief format and write a short (1-line) log entry to console and file.
    /// format: [LEVEL] HH:MM:SS.mmm | [module] message.
    /// @param entry log entry to write.
    auto write_entry_short(const log_entry& entry) -> void;

    /// @brief format and write a full (3-line) log entry to console and file.
    /// format:
    ///   [LEVEL] HH:MM:SS.mmm | [module] file:line | [thread: id]
    ///           fun : function_name
    ///           msg : message
    /// @param entry log entry to write.
    auto write_entry_full(const log_entry& entry) -> void;

    /// @brief write a log entry (chooses short or full format).
    /// @param entry log entry to write.
    auto write_entry(const log_entry& entry) -> void;

    // --- threading ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief worker thread function that processes log entries from the queue.
    /// blocking.
    auto worker_thread_func() -> void;

    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize the log system.
    /// @return true on success, false on failure.
    auto initialize() -> bool;

    /// @brief shutdown the log system.
    auto shutdown() -> void;
};

}  // namespace vent