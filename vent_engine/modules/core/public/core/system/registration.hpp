#pragma once
//
// core module.
// system registration initializer.
// ——————————————————————
//
// provides the static-init-time system registration mechanism
// all systems will use VENT_REGISTER_SYSTEM to register themselves before the
// engine entry point is called.
//
// note: this requires all libraries (static & shared) to be linked with
// --whole-archive. otherwise, static initializers are stripped out by the
// linker as they are never referenced.

#include <_vent/system/system_base.hpp>

#include <core/system/interface_map.hpp>

#include <functional>
#include <memory>

namespace vent {

/// @brief pending registry entry. this is a system that is not yet created,
/// therefore it can only store a function pointer to factory and interface_map
/// creation.
struct pending_entry {
    /// @brief factory function to create the system instance. implemented as a
    /// lambda in the VENT_REGISTER_SYSTEM macro.
    std::function<std::unique_ptr<class ::vent::system_base>()> factory;

    /// @brief interface registration function. this function will populate the
    /// interface map with all interfaces this system will provide.
    std::function<void(class ::vent::system_base*, interface_map&)>
        map_interfaces;

    /// @brief source plugin name. set at registration time via thread-local
    /// context. empty string for systems not from plugins.
    std::string source_plugin;
};

/// @brief thread-safe registration of a pending system entry.
/// @param entry the pending system entry to register.
/// @note exported from libvent_engine.so so plugins can register systems.
VENT_API auto register_pending_system(pending_entry&& entry) -> void;

template <typename SYSTEM, typename... INTERFACES>
auto register_interfaces(SYSTEM* sys, interface_map& map) -> void {
    // warning: fold expression. props to the guy who came up with this c++
    // clusterfuck. props to anyone who can write this without lookups.

    (map.emplace(std::type_index(typeid(INTERFACES)),
                 static_cast<INTERFACES*>(sys)),
     ...);
}

}  // namespace vent

// --- VENT_REGISTER_SYSTEM ---
// —————————————————————————————————————————————————————————————————————————————
// this macro registers a system for automatic discovery and initialization
// during engine startup.
// to be placed in global scope of the system's implementation .cpp file.
// this is c++ peak: 164 symbols and 383 letters and it ALL happens before we
// can even debug anything. wtf. (fuck this language)
//
// parameters:
//   TYPE the system class name.
//   ARGS all interfaces (their class names) this system exposes.
//
// usage:
//   // in my_system.cpp:
//   class my_system final : public ic_my_system {...};
//   VENT_REGISTER_SYSTEM(vent::my_system, vent::ic_my_system)
//
// requirements for TYPE:
//   - must inherit from vent::system_base.
//   - must override: name(), on_initialization(i32), on_shutdown().
//   - optionally implement role interfaces: ir_bootstrap, ir_dependencies.

#define VENT_REGISTER_SYSTEM(TYPE, ...)                                      \
    namespace {                                                              \
    struct VENT_REGISTRAR_TYPE {                                             \
        VENT_REGISTRAR_TYPE() {                                              \
            ::vent::register_pending_system(                                 \
                {.factory =                                                  \
                     []() -> std::unique_ptr<class ::vent::system_base> {    \
                     /* note: this does actually create the system */        \
                     return std::make_unique<TYPE>();                        \
                 },                                                          \
                 .map_interfaces =                                           \
                     []([[maybe_unused]] class ::vent::system_base* sys,     \
                        [[maybe_unused]]                                     \
                        ::vent::interface_map& m) {                          \
                         __VA_OPT__(                                         \
                             ::vent::register_interfaces<TYPE, __VA_ARGS__>( \
                                 static_cast<TYPE*>(sys), m);)               \
                     },                                                      \
                 .source_plugin = {}});                                      \
        }                                                                    \
    };                                                                       \
    static VENT_REGISTRAR_TYPE _vent_registrar_instance_;                    \
    }  // namespace
