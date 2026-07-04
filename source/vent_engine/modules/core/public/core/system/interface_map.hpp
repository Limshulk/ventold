#pragma once
//
// core module.
// interface map type.
// ——————————————————————
//
// type alias for mapping interface types to pointers.
// used during system registration to handle multiple inheritance.

#include <typeindex>
#include <unordered_map>

namespace vent {

/// @brief maps interface type to correctly-cast pointer. used to handle
/// multiple inheritance pointer adjustment at registration time.
using interface_map = std::unordered_map<std::type_index, void*>;

}  // namespace vent
