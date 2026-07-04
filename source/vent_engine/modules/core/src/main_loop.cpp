// core module.
// main loop implementation.
// ——————————————————————
//
// implements the main game loop.

#include <system/main_loop.hpp>
#include <_vent/accessors.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

namespace vent {

// --- internal ---
// —————————————————————————————————————————————————————————————————————————————

auto main_loop::sync_runnables() -> void {
    std::lock_guard lock(_pending_mutex);

    // add pending runnables.
    for (auto* r : _pending_add) {
        // avoid duplicates.
        if (std::find(_runnables.begin(), _runnables.end(), r) ==
            _runnables.end()) {
            _runnables.push_back(r);
        }
    }
    _pending_add.clear();

    // remove flagged runnables.
    for (auto* r : _pending_remove) {
        std::erase(_runnables, r);
    }
    _pending_remove.clear();
}

// --- configuration ---
// —————————————————————————————————————————————————————————————————————————————

auto main_loop::set_client(ir_client* client) -> void {
    _client = client;
}

auto main_loop::set_platform(ic_platform* platform) -> void {
    _platform = platform;
}

auto main_loop::add_runnable(ir_runnable* runnable) -> void {
    if (!runnable)
        return;

    std::lock_guard lock(_pending_mutex);
    _pending_add.push_back(runnable);
}

auto main_loop::remove_runnable(ir_runnable* runnable) -> void {
    if (!runnable)
        return;

    std::lock_guard lock(_pending_mutex);

    // also remove from pending_add in case it was just added.
    std::erase(_pending_add, runnable);

    // flag for removal from active list.
    _pending_remove.push_back(runnable);
}

auto main_loop::clear_runnables() -> void {
    _runnables.clear();
    std::lock_guard lock(_pending_mutex);
    _pending_add.clear();
    _pending_remove.clear();
}

auto main_loop::set_runnables(std::vector<ir_runnable*> runnables) -> void {
    _runnables = std::move(runnables);
    std::lock_guard lock(_pending_mutex);
    _pending_add.clear();
    _pending_remove.clear();
}

// --- loop control ---
// —————————————————————————————————————————————————————————————————————————————

// this call is blocking until the application exits.
// note that ui and windows are not handled here, but in P:UI thread.

auto main_loop::run() -> int {
    if (!_client) {
        log()->error("main_loop", "no client set. cannot run main loop.");
        return 1;
    }

    log()->info("MAIN LOOP", "ENTERING MAIN LOOP");

    _running.store(true, std::memory_order_release);
    _stop_requested.store(false, std::memory_order_release);
    _frame_count = 0;
    _last_time   = std::chrono::steady_clock::now();

    while (_client->is_running() &&
           !_stop_requested.load(std::memory_order_acquire)) {
        // sync pending runnable changes.
        sync_runnables();

        // calculate frame delta.
        auto now    = std::chrono::steady_clock::now();
        _delta_time = std::chrono::duration<f64>(now - _last_time).count();
        _last_time  = now;

        // update all runnables.
        for (auto* runnable : _runnables) {
            runnable->on_update(_delta_time);
        }

        if (_platform && _platform->get_window_count() == 0) {
            log()->trace("main_loop", "no windows remain, exiting main loop");
            break;
        }

        _frame_count++;
    }

    _running.store(false, std::memory_order_release);
    log()->info("main_loop", "main loop ended");
    return 0;
}

auto main_loop::is_running() const -> bool {
    return _running.load(std::memory_order_acquire);
}

auto main_loop::request_stop() -> void {
    _stop_requested.store(true, std::memory_order_release);
}

// --- frame timing ---
// —————————————————————————————————————————————————————————————————————————————

auto main_loop::delta_time() const -> f64 {
    return _delta_time;
}

auto main_loop::frame_count() const -> u64 {
    return _frame_count;
}

}  // namespace vent
