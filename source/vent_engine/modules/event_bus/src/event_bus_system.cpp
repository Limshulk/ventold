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
                                 event_callback   callback) -> subscription_id {
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
                      .valid    = true});

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
    std::vector<event_callback> callbacks;
    std::string                 event_str(event);

    log()->trace("event_bus", "dispatching event '{}' (payload: {})", event_str, data);

    // take a snapshot of all current subscribers.
    {
        std::shared_lock lock(_mutex);

        auto it = _subscriptions.find(event_str);
        if (it == _subscriptions.end()) {
            log()->debug("event_bus", "no subscribers for event '{}'", event_str);
            return;
        }

        // collect valid callbacks.
        for (const auto& sub : it->second) {
            if (sub.valid) {
                callbacks.push_back(sub.callback);
            }
        }
    }

    if (callbacks.empty()) {
        return;
    }

    // clean up invalid subscriptions periodically.
    _publish_count_after_cleanup++;
    if (_publish_count_after_cleanup >= _cleanup_interval) {
        cleanup_invalid_subscriptions();
    }

    // fire all callbacks in parallel via job system (if available).
    // lifetime_holder is captured to keep the data alive until all callbacks
    // complete.
    log()->trace("event_bus", "dispatching event '{}' to {} subscribers.", event_str, callbacks.size());
    for (const auto& callback : callbacks) {
        job()->fire([callback, event_str, data, lifetime_holder]() {
            callback(event_str, data);
        });
    }
}

auto event_bus_system::dispatch_wait(std::string_view event, void* data)
    -> void {
    std::vector<event_callback> callbacks;
    std::string                 event_str(event);

    log()->trace("event_bus", "dispatch_wait event '{}' (payload: {})", event_str, data);

    // take a snapshot of all current subscribers.
    {
        std::shared_lock lock(_mutex);

        auto it = _subscriptions.find(event_str);
        if (it == _subscriptions.end()) {
            log()->debug("event_bus", "no subscribers for event '{}'", event_str);
            return;
        }

        // collect valid callbacks.
        for (const auto& sub : it->second) {
            if (sub.valid) {
                callbacks.push_back(sub.callback);
            }
        }
    }

    if (callbacks.empty()) {
        return;
    }

    // clean up invalid subscriptions periodically.
    _publish_count_after_cleanup++;
    if (_publish_count_after_cleanup >= _cleanup_interval) {
        cleanup_invalid_subscriptions();
    }

    // fire all callbacks and wait for them to complete.
    // parallel dispatch via job system with wait.
    log()->trace("event_bus", "dispatch_wait: firing {} callbacks for event '{}'", callbacks.size(), event_str);
    std::vector<task> tasks;
    tasks.reserve(callbacks.size());

    for (const auto& callback : callbacks) {
        tasks.push_back(job()->submit([callback, event_str, data]() {
            callback(event_str, data);
        }));
    }

    log()->trace("event_bus", "dispatch_wait: waiting for {} tasks to complete...", tasks.size());
    // wait for all tasks to complete.
    for (auto& t : tasks) {
        t.wait();
    }
}

auto event_bus_system::cleanup_invalid_subscriptions() -> void {
    std::unique_lock lock(_mutex);

    // iterate all event subscriptions and remove invalid ones.
    for (auto& [event_name, subs] : _subscriptions) {
        std::erase_if(subs, [](const subscription& sub) {
            return !sub.valid;
        });
    }

    // remove empty event entries.
    for (auto it = _subscriptions.begin(); it != _subscriptions.end();) {
        if (it->second.empty()) {
            it = _subscriptions.erase(it);
        } else {
            ++it;
        }
    }

    // reset cleanup counter.
    _publish_count_after_cleanup = 0;
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::event_bus_system,
                     vent::ic_event_bus,
                     vent::i_event_bus)