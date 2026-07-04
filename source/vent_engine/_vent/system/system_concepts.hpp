#pragma once
//
// vent public sdk.
// system concepts.
// ——————————————————————
//
// provides c++20 concepts for systems and interfaces.

#include <concepts>
#include <string_view>

namespace vent {

/// @brief concept enforcing that an interface provides a static constexpr
/// std::string_view named 'system_name'.
template <typename T>
concept c_system_interface = requires {
    { T::system_name } -> std::convertible_to<std::string_view>;
};

}  // namespace vent
