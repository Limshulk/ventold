#pragma once
//
// vent public sdk
// render command definitions.
// ——————————————————————
//
// defines the sorting key, render packets, and the thread-local command list
// abstraction used by the frontend to batch rendering commands.

#include <_vent/vent_sdk.hpp>
#include <_vent/renderer/vertex.hpp>

#include <algorithm>
#include <span>
#include <vector>

namespace vent {

// 64-bit sort key.
// layout:
// [ 8 bits: depth / layer ]
// [ 16 bits: pipeline handle ]
// [ 16 bits: material / texture handle ]
// [ 24 bits: distance to camera ]
using sort_key = u64;

/// @brief encapsulates all data needed for a single draw call.
struct render_packet {
    sort_key
        key;  ///< 0x00-0x08 (8b): 64-bit sort key for depth/pipeline sorting.
    mesh_handle mesh;  ///< 0x08-0x10 (8b): opaque handle to the mesh to draw.
    texture_handle texture; ///< 0x10-0x18 (8b): handle to the texture to bind.
    pipeline_handle pipeline; ///< 0x18-0x20 (8b): handle to the graphics pipeline.
    math::mat4 transform; ///< 0x20-0x60 (64b): the model-to-world transform.
};

// a thread-local list of rendering commands.
// buffers commands lock-free and can be sorted efficiently before backend
// translation.
class command_list {
public:
    command_list()  = default;
    ~command_list() = default;

    VENT_NO_COPY_MOVE(command_list)

    VENT_INLINE auto draw_mesh(pipeline_handle pipeline, texture_handle texture, mesh_handle mesh, const math::mat4& transform, sort_key key = 0) -> void {
        _packets.push_back({key, mesh, texture, pipeline, transform});
    }

    /// @brief clear the list for the next frame.
    VENT_INLINE auto clear() -> void { _packets.clear(); }

    /// @brief sort the internal packets based on their sort key.
    VENT_INLINE auto sort() -> void {
        std::ranges::sort(_packets,
                          [](const render_packet& a, const render_packet& b) {
                              return a.key < b.key;
                          });
    }

    /// @brief get the underlying packets.
    [[nodiscard]]
    VENT_INLINE auto get_packets() const -> std::span<const render_packet> {
        return _packets;
    }

private:
    std::vector<render_packet> _packets;
};

}  // namespace vent
