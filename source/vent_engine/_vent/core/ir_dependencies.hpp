#pragma once
//
// vent public sdk.
// role interface: dependencies.
// ——————————————————————
//
// role interface for systems that depend on other systems.
// the system registry uses this to determine initialization order.
//
// usage:
//   class my_system : public system_base, public ir_dependencies {
//       auto dependencies() const -> std::span<const std::string_view> override
//       {
//           static constexpr std::string_view deps[] = {"vent.other_system"};
//           return deps;
//       }
//   };

#include <span>
#include <string_view>

namespace vent {

/// @brief role interface for systems with dependencies. systems implementing
/// this interface declare which other systems must initialize before them.
class ir_dependencies {
public:
    virtual ~ir_dependencies() = default;

    /// @brief get the list of system names this system depends on.
    /// @return span of dependency system names.
    [[nodiscard]]
    virtual auto dependencies() const -> std::span<const std::string_view> = 0;
};

}  // namespace vent
