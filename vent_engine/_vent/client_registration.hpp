#pragma once
//
// vent public sdk.
// client base class and registration.
// ——————————————————————
//
// provides the client_base class and VENT_REGISTER_CLIENT macro for client
// applications. clients inherit from client_base and use the macro to register
// for automatic discovery by the engine.
//
// usage:
//   class my_game : public vent::client_base {
//       auto on_initialize() -> bool override { ... }
//       auto on_update(f64 delta) -> void override { ... }
//       auto on_shutdown() -> void override { ... }
//       auto dependencies() const -> std::span<const std::string_view> override
//       {
//           static constexpr std::string_view deps[] =
//           {"vent.platform_system"}; return deps;
//       }
//   };
//   VENT_REGISTER_CLIENT(my_game)

#include <_vent/vent_sdk.hpp>
#include <_vent/system/system_base.hpp>
#include <_vent/interfaces/ir_client.hpp>
#include <_vent/interfaces/ir_dependencies.hpp>
#include <_vent/interfaces/ir_runnable.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace vent {

/// @brief base class for client applications. inherits from system_base
/// (lifecycle) and implements ir_client, ir_runnable, and ir_dependencies roles.
/// clients must override dependencies() to declare what systems they need.
class client_base
    : public system_base
    , public ir_client
    , public ir_runnable
    , public ir_dependencies {
public:
    // --- client-facing api ---
    // —————————————————————————————————————————————————————————————————————————
    // these are to be overridden by the client application.

    /// @brief called when the client initializes.
    /// @return true if initialization succeeded, false to abort.
    virtual auto on_initialize() -> bool { return true; }

    /// @brief called every frame with delta time in seconds.
    /// @param delta_time time since last frame in seconds.
    auto on_update(f64 delta_time) -> void override { (void) delta_time; }

    /// @brief called when the client shuts down.
    auto on_shutdown() -> void override {}

    /// @brief default window closing policy. overridable to change behavior.
    [[nodiscard]]
    auto close_policy() const -> window_close_policy override {
        return window_close_policy::exit_on_main_close;
    }

    /// @brief declare systems this client depends on. override to specify
    /// dependencies. client will not initialize until all deps are ready.
    /// @return span of system names to wait for.
    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        return {};  // default: no dependencies.
    }

public:
    ~client_base() override = default;

    // --- system_base implementation ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief system name. always "client".
    [[nodiscard]]
    auto name() const -> std::string_view override {
        return "client";
    }

    /// @brief system initialization. forwards to on_initialize().
    [[nodiscard]]
    auto on_initialization(i32 /*stage*/) -> initialization_result override {
        return on_initialize() ? initialization_result::complete()
                               : initialization_result::failed();
    }

    // --- ir_client implementation ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief check if the client wants to keep running.
    /// @return true to continue, false to exit main loop.
    [[nodiscard]]
    auto is_running() const -> bool override {
        return _running.load(std::memory_order_acquire);
    }

    /// @brief request exit from the main loop. thread-safe.
    auto request_exit() -> void override {
        _running.store(false, std::memory_order_release);
    }

protected:
    std::atomic<bool> _running {
        true};  ///< directly controls the vent main loop.
};

/// @brief pending client factory function type..
using client_factory_fn = std::function<std::unique_ptr<client_base>()>;

/// @brief register the client factory. called by VENT_REGISTER_CLIENT macro.
/// @param factory function that creates the client instance.
/// @note exported from libvent_engine.so so client plugins can register.
VENT_API auto register_client_factory(client_factory_fn factory) -> void;

}  // namespace vent

// --- VENT_REGISTER_CLIENT ---
// —————————————————————————————————————————————————————————————————————————————
// this macro registers a client for automatic discovery by the engine.
// to be placed in global scope of the client's implementation .cpp file.
//
// the client must inherit from vent::client_base.
// only one client can be registered per application - attempting to register
// multiple clients will result in a linker error (duplicate symbol).
//
// usage:
//   class my_game : public vent::client_base { ... };
//   VENT_REGISTER_CLIENT(my_game)

// linker guard: this symbol is defined by the macro and will cause a linker
// error if two clients attempt to register (duplicate definition).
// the [[maybe_unused]] attribute suppresses warnings about the unused variable.
#define VENT_REGISTER_CLIENT(TYPE)                             \
    [[maybe_unused]]                                           \
    extern const char _vent_client_registration_guard_;        \
    const char        _vent_client_registration_guard_ = 0;    \
    namespace {                                                \
    struct VENT_CLIENT_REGISTRAR {                             \
        VENT_CLIENT_REGISTRAR() {                              \
            ::vent::register_client_factory(                   \
                []() -> std::unique_ptr<::vent::client_base> { \
                    return std::make_unique<TYPE>();           \
                });                                            \
        }                                                      \
    };                                                         \
    static VENT_CLIENT_REGISTRAR _vent_client_registrar_;      \
    }  // namespace
