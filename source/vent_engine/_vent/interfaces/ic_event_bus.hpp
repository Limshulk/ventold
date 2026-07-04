#pragma once
//
// vent public sdk.
// client-faced event_bus interface.
// ——————————————————————
//
// lightweight publish/subscribe event bus for decoupled system communication.
// used for system ready notifications, resource loading events, window
// creation, and hot-reload signaling.
//
// key features:
//   - string-based event names for flexibility.
//   - parallel dispatch via job system.
//   - callback-based subscriptions with unique ids.
//
// standard event naming convention (examples!):
//   - system.ready.<name>     - system became ready (e.g.,
//   "system.ready.vent.renderer").
//   - system.failed.<name>    - system failed to init.
//   - system.skipped.<name>   - system skipped (should_load returned false).
//   - system.shutdown.<name>  - system is shutting down.
//   - window.created          - window created by client (data: ic_window*).
//   - window.resized          - window size changed.
//   - window.close_requested  - user requested window close (X button, etc).
//   - resources.loaded.<type> - resource finished loading.
//   - plugin.loaded.<name>    - plugin loaded from disk.
//   - plugin.reloaded.<name>  - plugin hot-reloaded.

#include <_vent/vent_sdk.hpp>

#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>

namespace vent {

/// @brief callback function signature for event handlers.
/// @param event the event name that was published.
/// @param data opaque pointer to event-specific data (type depends on event).
using event_callback = std::function<void(std::string_view event, void* data)>;

/// @brief unique identifier for an event subscription.
/// use this to unsubscribe from events later.
using subscription_id = u64;

/// @brief invalid subscription id (returned on failure).
constexpr subscription_id INVALID_SUBSCRIPTION = 0;

/// @brief client-facing event bus interface.
/// provides publish/subscribe functionality for decoupled communication.
class ic_event_bus {
public:
    static constexpr std::string_view system_name = "vent.system.event_bus";

    virtual ~ic_event_bus() = default;

    // --- subscription management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief subscribe to an event. the callback will be invoked whenever the
    ///        event is published.
    /// @param event event name to subscribe to (e.g.,
    /// "system.ready.vent.renderer").
    /// @param callback function to call when event fires.
    /// @return subscription id for later unsubscribe, or INVALID_SUBSCRIPTION
    /// on failure.
    [[nodiscard]]
    virtual auto subscribe(std::string_view event,
                           event_callback   callback) -> subscription_id = 0;

    /// @brief unsubscribe from an event. the callback will no longer be invoked
    ///        for this event.
    /// @param id subscription id returned by subscribe().
    virtual auto unsubscribe(subscription_id id) -> void = 0;

    // --- event publishing ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief publish an event asynchronously via job system.
    /// callbacks are dispatched in parallel, returns immediately.
    /// @param event event name to publish.
    /// @param data opaque pointer to event-specific data (can be nullptr).
    /// @warning caller must ensure data outlives all callback invocations.
    ///          for stack-allocated data, use the template overload instead.
    virtual auto publish(std::string_view event,
                         void*            data = nullptr) -> void = 0;

    /// @brief publish an event asynchronously with automatic data copying.
    /// creates a heap copy of data that is automatically freed when all
    /// callbacks complete. safe for stack-allocated event data.
    /// @tparam T event data type (must be trivially copyable).
    /// @param event event name to publish.
    /// @param data event data to copy and publish.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    auto publish(std::string_view event, const T& data) -> void {
        // note: shared_ptr ensures lifetime until all job lambdas complete.
        auto shared = std::make_shared<T>(data);
        publish_copied(
            event, shared.get(), std::static_pointer_cast<void>(shared));
    }

    /// @brief publish an event and wait for all callbacks to complete.
    /// dispatches callbacks in parallel via job system, then blocks until done.
    /// safe for stack-allocated data since we wait for completion.
    /// @param event event name to publish.
    /// @param data opaque pointer to event-specific data (can be nullptr).
    virtual auto publish_wait(std::string_view event,
                              void*            data = nullptr) -> void = 0;

protected:
    /// @brief internal: publish with copied data managed by shared_ptr.
    /// the lifetime_holder is captured in each async callback lambda to keep
    /// the data alive until all callbacks complete.
    /// @param event event name to publish.
    /// @param data raw pointer to the copied data.
    /// @param lifetime_holder shared_ptr that owns the data.
    virtual auto publish_copied(std::string_view      event,
                                void*                 data,
                                std::shared_ptr<void> lifetime_holder)
        -> void = 0;
};

}  // namespace vent
