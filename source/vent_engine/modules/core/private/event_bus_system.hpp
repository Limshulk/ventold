#pragma once
//
// core module.
// event_bus implementation.
// ——————————————————————
//
// core-based implementation of the event_bus. provides publish-/subscribe-able
// events that are described by string names.
// publish() dispatches callbacks to the job system for parallel execution. while
// execution, each subscriber sees a snapshot of the latests subscriber list.

#include <core/interfaces/i_event_bus.hpp>
#include <core/interfaces/ir_bootstrap.hpp>

#include <_vent/system/system_base.hpp>

#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace vent {

class event_bus_system final
    : public system_base
    , public i_event_bus
    , public ir_bootstrap {
public:
    event_bus_system() = default;
    ~event_bus_system() override;

    VENT_NO_COPY_MOVE(event_bus_system);

public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————
    // event_bus is a bootstrap system.

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_event_bus::system_name;
    }

    [[nodiscard]]
    auto on_initialization(i32 stage = 0)
        -> system_initialization_result override {
        return initialize() ? system_initialization_result::complete()
                            : system_initialization_result::failed();
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    // --- internal types ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief internal subscription struct stored for each subscrption
    /// registrated.
    struct subscription {
        subscription_id id;        ///< unique subscription id.
        std::string     event;     ///< event name (copy, not to be changed).
        event_callback  callback;  ///< callback function when event publishes.
        bool            valid;  ///< false if unsubscribed (for lazy cleanup).
    };

    // --- member variables ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief map of all subscriptions. event name -> list of subscriptions.
    std::unordered_map<std::string, std::vector<subscription>> _subscriptions;

    std::unordered_map<subscription_id, std::string>
        _subscription_index;  ///< subscription id -> event name.

    /// @brief next subscription id to be assigned.
    std::atomic<subscription_id> _next_subscription_id {1};

    /// @brief mutex for subscription management.
    mutable std::shared_mutex _mutex;

    /// @brief true if system is ready to be used.
    bool _initialized = false;

    /// @brief count of published events since last cleanup. used to trigger
    /// periodic cleanup of invalid subscriptions.
    u32 _publish_count_after_cleanup = 0;  // todo: atomic?

    /// @brief interval of publish calls to trigger cleanup.
    u32 _cleanup_interval = 1000;  // todo: make this configurable.

    // --- internal methods ---
    // —————————————————————————————————————————————————————————————————————————

    // --- lifecycle ---

    /// @brief initialize the event bus system.
    /// @return true on success, false on failure.
    auto initialize() -> bool;

    /// @brief shutdown the event bus system.
    auto shutdown() -> void;



public:
    // --- ic_event_bus ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto subscribe(std::string_view event, event_callback callback)
        -> subscription_id override;

    auto unsubscribe(subscription_id id) -> void override;

    auto publish(std::string_view event, void* data = nullptr) -> void override;

    auto publish_wait(std::string_view event, void* data = nullptr)
        -> void override;

private:
    // --- ic_event_bus ---
    // —————————————————————————————————————————————————————————————————————————

    auto publish_copied(std::string_view      event,
                        void*                 data,
                        std::shared_ptr<void> lifetime_holder) -> void override;

    // --- internal dispatch ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief dispatch event to all subscribers in parallel. exits after firing.
    /// @param event event name to dispatch.
    /// @param data opaque pointer to event-specific data.
    /// @param lifetime_holder optional shared_ptr captured in job lambdas to
    /// keep data alive until all callbacks are complete.
    auto dispatch(std::string_view      event,
                  void*                 data,
                  std::shared_ptr<void> lifetime_holder = nullptr) -> void;

    /// @brief dispatch event to all subscribers in parallel and wait for
    /// completion.
    /// @param event event name to dispatch.
    /// @param data opaque pointer to event-specific data.
    auto dispatch_wait(std::string_view event, void* data) -> void;

    /// @brief cleanup all invalid subscriptions from the subscription lists.
    auto cleanup_invalid_subscriptions() -> void;
};

}  // namespace vent
