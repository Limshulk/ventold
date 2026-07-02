#pragma once
//
// renderer module.
// render backend interface.
// ——————————————————————
//
// details.

#include <string_view>

namespace vent {

class i_render_backend {
public:
    virtual ~i_render_backend() = default;

    // --- backend info ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a friendly name for the backend api that implements this
    /// interface.
    /// @return a string view containing the backend api name.
    virtual auto get_api_name() const -> std::string_view = 0;
};

}  // namespace vent