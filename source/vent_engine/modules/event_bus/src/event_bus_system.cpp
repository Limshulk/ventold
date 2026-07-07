// core module.
// event_bus implementation.
// ——————————————————————
//
// implementation of the event_bus class. provides thread-safe, multitasking
// event publishing and handling.

#include <event_bus_system.hpp>

#include <core/system/registration.hpp>

#include <_vent/accessors.hpp>

#include <mutex>

namespace vent {

// --- lifecycle ---
// —————————————————————————————————————————————————————————————————————————————

event_bus_system::~event_bus_system() {
    if (_initialized) {
        // todo: in a clean shutdown, we should never even get here. perhaps log
        // todo: a warning? but we should assume no log is not available here.

        shutdown();
    }
}

auto event_bus_system::initialize() -> bool {
    if (_initialized)
        return true;

    _initialized = true;

    // for now, we don't have anything to do here.
    return true;
}

auto event_bus_system::shutdown() -> void {
    log()->trace("event_bus", "event_bus_system::shutdown() called");
    std::unique_lock lock(_mutex);

    _subscriptions.clear();
    _subscription_index.clear();
    _initialized = false;
}

auto event_bus_system::subscribe(std::string_view event,
                                 event_callback   callback,
                                 event_delivery   delivery) -> subscription_id {
    if (!_initialized)
        return INVALID_SUBSCRIPTION;

    std::unique_lock lock(_mutex);

    // create new subscription.
    auto id = _next_subscription_id.fetch_add(1, std::memory_order_relaxed);

    std::string event_str(event);
    log()->trace("event_bus", "subscribing to event: '{}'", event_str);

    _subscriptions[event_str].push_back(
        subscription {.id       = id,
                      .event    = event_str,
                      .callback = std::move(callback),
                      .delivery = delivery});

    _subscription_index[id] = event_str;

    log()->trace("event_bus", "subscribed to event '{}' with id {}", event_str, id);
    return id;
}

auto event_bus_system::unsubscribe(subscription_id id) -> void {
    if (id == INVALID_SUBSCRIPTION || !_initialized)
        return;

    std::unique_lock lock(_mutex);

    log()->trace("event_bus", "unsubscribing id {}", id);

    // find event to this subscription id.
    auto it = _subscription_index.find(id);
    if (it == _subscription_index.end()) {
        log()->debug("event_bus", "unsubscribe failed: id {} not found.", id);
        return;
    }

    // remove subscription from the global map.
    auto& all_subs = _subscriptions[it->second];
    std::erase_if(all_subs, [id](const subscription& sub) {
        return sub.id == id;
    });

    log()->trace("event_bus", "unsubscribed id {} from event '{}'", id, it->second);
    _subscription_index.erase(it);
}

auto event_bus_system::publish(std::string_view event, void* data) -> void {
    dispatch(event, data);
}

auto event_bus_system::publish_wait(std::string_view event, void* data)
    -> void {
    dispatch_wait(event, data);
}

auto event_bus_system::publish_copied(std::string_view      event,
                                      void*                 data,
                                      std::shared_ptr<void> lifetime_holder)
    -> void {
    dispatch(event, data, lifetime_holder);
}

auto event_bus_system::dispatch(std::string_view      event,
                                void*                 data,
                                std::shared_ptr<void> lifetime_holder) -> void {
    // snapshot (callback, delivery) pairs so we can route each subscriber to its
    // chosen thread without holding the lock during dispatch.
    struct pending {
        event_callback callback;
        event_delivery delivery;
    };
    std::vector<pending> callbacks;
    std::string          event_str(event);

    log()->trace("event_bus", "dispatching event '{}' (payload: {})", event_str, data);

    // take a snapshot of all current subscribers.
    {
        std::shared_lock lock(_mutex);

        auto it = _subscriptions.find(event_str);
        if (it == _subscriptions.end()) {
            log()->debug("event_bus", "no subscribers for event '{}'", event_str);
            return;
        }

        // snapshot the callbacks. unsubscribe() hard-erases entries, so every
        // subscription still present here is live — no validity filtering.
        for (const auto& sub : it->second) {
            callbacks.push_back({sub.callback, sub.delivery});
        }
    }

    if (callbacks.empty()) {
        return;
    }

    // route each callback per its delivery policy. lifetime_holder is captured
    // into every deferred lambda so the data outlives all callbacks.
    log()->trace("event_bus", "dispatching event '{}' to {} subscribers.", event_str, callbacks.size());
    for (const auto& p : callbacks) {
        switch (p.delivery) {
            case event_delivery::immediate:
                // synchronous, on the publisher's thread, before we return.
                p.callback(event_str, data);
                break;
            case event_delivery::main:
                // deferred onto the main thread's frame-start drain.
                job()->fire(
                    [cb = p.callback, event_str, data, lifetime_holder]() {
                        cb(event_str, data);
                    },
                    job_priority::normal,
                    job_affinity::main);
                break;
            case event_delivery::parallel:
            default:
                // on a job worker, possibly concurrently (the default).
                job()->fire(
                    [cb = p.callback, event_str, data, lifetime_holder]() {
                        cb(event_str, data);
                    });
                break;
        }
    }
}

auto event_bus_system::dispatch_wait(std::string_view event, void* data)
    -> void {
    struct pending {
        event_callback callback;
        event_delivery delivery;
    };
    std::vector<pending> callbacks;
    std::string          event_str(event);

    log()->trace("event_bus", "dispatch_wait event '{}' (payload: {})", event_str, data);

    // take a snapshot of all current subscribers.
    {
        std::shared_lock lock(_mutex);

        auto it = _subscriptions.find(event_str);
        if (it == _subscriptions.end()) {
            log()->debug("event_bus", "no subscribers for event '{}'", event_str);
            return;
        }

        // snapshot the callbacks (see dispatch(): all present subscriptions are
        // live because unsubscribe() hard-erases).
        for (const auto& sub : it->second) {
            callbacks.push_back({sub.callback, sub.delivery});
        }
    }

    if (callbacks.empty()) {
        return;
    }

    log()->trace("event_bus", "dispatch_wait: firing {} callbacks for event '{}'", callbacks.size(), event_str);
    std::vector<task> tasks;
    tasks.reserve(callbacks.size());

    for (const auto& p : callbacks) {
        switch (p.delivery) {
            case event_delivery::immediate:
                // runs synchronously here — trivially "awaited".
                p.callback(event_str, data);
                break;
            case event_delivery::main:
                // main-delivered callbacks are NOT awaited: blocking on the frame
                // drain from this (non-main) thread would deadlock (see the
                // contract in ic_event_bus.hpp). fire-and-forget onto main; it
                // runs at the next frame boundary. the caller must ensure `data`
                // outlives the frame for such subscribers.
                job()->fire([cb = p.callback, event_str, data]() {
                                cb(event_str, data);
                            },
                            job_priority::normal,
                            job_affinity::main);
                break;
            case event_delivery::parallel:
            default:
                tasks.push_back(
                    job()->submit([cb = p.callback, event_str, data]() {
                        cb(event_str, data);
                    }));
                break;
        }
    }

    log()->trace("event_bus", "dispatch_wait: waiting for {} tasks to complete...", tasks.size());
    // wait only for the parallel tasks (immediate already ran, main is deferred).
    for (auto& t : tasks) {
        t.wait();
    }
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::event_bus_system,
                     vent::ic_event_bus,
                     vent::i_event_bus)