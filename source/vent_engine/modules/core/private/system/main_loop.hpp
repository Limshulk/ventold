#pragma once
//
// core module.
// main loop.
// ——————————————————————
//
// main game loop logic. handles frame timing, runnable system updates, and
// termination conditions.

#include <_vent/vent_sdk.hpp>
#include <_vent/core/ir_runnable.hpp>
#include <_vent/core/ir_client.hpp>
#include <_vent/platform/ic_platform.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace vent {

/// @brief main game loop handler. manages the frame loop, calling
/// ir_runnable::on_update() for all registered runnables and checking
/// termination conditions.
///
/// termination conditions:
/// - client's is_running() returns false.
/// - main window's should_close() returns true.
/// - request_stop() is called from any thread.
///
/// thread safety:
/// - add_runnable()/remove_runnable() can be called from any thread.
/// - runnables are only updated from the main loop thread.
/// - new runnables are added to a pending list and integrated at frame start.
/// - removed runnables are flagged and removed at frame start.

class main_loop {
public:
    main_loop()  = default;
    ~main_loop() = default;

    VENT_NO_COPY_MOVE(main_loop);

private:
    // --- systems ---

    /// @brief client controlling loop lifecycle.
    ir_client* _client = nullptr;

    /// @brief platform for window close detection.
    ic_platform* _platform = nullptr;

    /// @brief active runnables, updated each frame. only accessed by loop
    /// thread.
    std::vector<ir_runnable*> _runnables;

    /// @brief pending runnables to add at next frame start.
    std::vector<ir_runnable*> _pending_add;

    /// @brief runnables to remove at next frame start.
    std::vector<ir_runnable*> _pending_remove;

    // --- state ---

    /// @brief true while run() is executing.
    std::atomic<bool> _running {false};

    /// @brief true when stop has been requested.
    std::atomic<bool> _stop_requested {false};

    /// @brief last frame's delta time.
    f64 _delta_time = 0.0;

    /// @brief total frame count.
    u64 _frame_count = 0;

    /// @brief last frame timestamp for delta calculation.
    std::chrono::steady_clock::time_point _last_time;

    // --- thread safety ---

    /// @brief mutex protecting pending lists.
    std::mutex _pending_mutex;

private:
    /// @brief integrate pending adds and removes into the active list. called
    /// at the start of each frame.
    auto sync_runnables() -> void;

public:
    // --- configuration ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set the client that controls loop lifecycle.
    /// @param client pointer to the client implementing ir_client.
    auto set_client(ir_client* client) -> void;

    /// @brief set the platform interface for window close detection.
    /// @param platform pointer to the platform interface.
    auto set_platform(ic_platform* platform) -> void;

    /// @brief add a runnable to be updated each frame. can be called from any
    /// thread at any time.
    /// @param runnable pointer to the runnable.
    auto add_runnable(ir_runnable* runnable) -> void;

    /// @brief remove a runnable from the update list. can be called from any
    /// thread at any time.
    /// @param runnable pointer to the runnable to remove.
    auto remove_runnable(ir_runnable* runnable) -> void;

    /// @brief clear all runnables. only call when loop is not running.
    auto clear_runnables() -> void;

    /// @brief set all runnables at once (replaces existing). only call when
    /// loop is not running.
    /// @param runnables vector of runnable pointers.
    auto set_runnables(std::vector<ir_runnable*> runnables) -> void;

    // --- loop control ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief run the main loop. blocks until termination condition is met.
    /// @return exit code (0 = success, non-zero = error or client exit code).
    auto run() -> int;

    /// @brief check if the loop is currently running.
    /// @return true if run() is executing.
    [[nodiscard]]
    auto is_running() const -> bool;

    /// @brief request the loop to stop.
    auto request_stop() -> void;

    // --- frame timing ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get the delta time of the last frame.
    /// @return delta time in seconds.
    [[nodiscard]]
    auto delta_time() const -> f64;

    /// @brief get the total number of frames since loop start.
    /// @return frame count.
    [[nodiscard]]
    auto frame_count() const -> u64;
};

}  // namespace vent
