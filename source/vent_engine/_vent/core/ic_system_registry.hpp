#pragma once
//
// vent public sdk.
// client-faced system_registry interface.
// ——————————————————————
//
// provides type-based access to all registered system interfaces.
//
// usage:
//   auto* window = vent::system()->get<ic_window>(); // will assert if not
//   found. bool has_deps =
//   vent::system()->has_role<ir_dependencies>("vent.my_system");

// todo: implement proper VENT_ASSERT.
#include <_vent/system/system_concepts.hpp>

#include <cassert>
#include <string_view>
#include <typeinfo>

namespace vent {

class ic_system_registry {
protected:
public:
    virtual ~ic_system_registry() = default;

    /// @brief get a system interface by type. asserts if not found.
    /// @tparam T type of the interface.
    /// @return pointer to the interface. never nullptr.
    template <c_system_interface T>
    [[nodiscard]]
    auto get() -> T* {
        auto* ptr = static_cast<T*>(get_interface_ptr(typeid(T)));
        assert(ptr != nullptr &&
               "system interface not registered");  // todo: proper assert.
        return ptr;
    }

    /// @brief get a system interface by type. returns nullptr if not found or
    /// not ready.
    /// @tparam T type of the interface.
    /// @return pointer to the interface. nullptr if not found or not ready.
    template <c_system_interface T>
    [[nodiscard]]
    auto get_if_ready() -> T* {
        return static_cast<T*>(get_interface_ptr_if_ready(typeid(T)));
    }

    /// @brief check if a system implements a specific role interface.
    /// @tparam Role role interface type (e.g., ir_dependencies, ir_runnable).
    /// @param system_name name of the system to query.
    /// @return true if the system exists and implements the role.
    template <typename Role>
    [[nodiscard]]
    auto has_role(std::string_view system_name) -> bool {
        return has_role_impl(system_name, typeid(Role));
    }

    /// @brief get a system's role interface if it implements it.
    /// @tparam Role role interface type (e.g., ir_dependencies, ir_runnable).
    /// @param system_name name of the system to query.
    /// @return pointer to the role interface, or nullptr if not found.
    template <typename Role>
    [[nodiscard]]
    auto get_role(std::string_view system_name) -> Role* {
        return static_cast<Role*>(get_role_impl(system_name, typeid(Role)));
    }

protected:
    /// @brief get interface pointer by type. returns nullptr if not found.
    /// @param interface_type type of interface to look for.
    /// @return interface ptr or nullptr if not found / not registered.
    [[nodiscard]]
    virtual auto get_interface_ptr(std::type_info const& interface_type)
        -> void* = 0;

    /// @brief get interface pointer by type, if the system is ready.
    /// @param interface_type type of interface to look for.
    /// @return interface ptr or nullptr if not found / not registered or not
    /// ready.
    [[nodiscard]]
    virtual auto get_interface_ptr_if_ready(std::type_info const& interface_type)
        -> void* = 0;

    /// @brief check if a system implements a role.
    /// @param system_name name of the system.
    /// @param role_type type of the role interface.
    /// @return true if the system implements the role.
    [[nodiscard]]
    virtual auto has_role_impl(std::string_view      system_name,
                               std::type_info const& role_type) -> bool = 0;

    /// @brief get a system's role interface.
    /// @param system_name name of the system.
    /// @param role_type type of the role interface.
    /// @return pointer to the role, or nullptr if not found.
    [[nodiscard]]
    virtual auto get_role_impl(std::string_view      system_name,
                               std::type_info const& role_type) -> void* = 0;
};

}  // namespace vent
