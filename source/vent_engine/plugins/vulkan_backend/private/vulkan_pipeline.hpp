#pragma once
//
// vulkan_backend plugin.
// vulkan graphics pipeline.
// ——————————————————————
//
// encapsulates the vulkan graphics pipeline following the vulkan tutorial.

#include <_vent/asset/shader.hpp>

#include <renderer/interfaces/i_pipeline.hpp>

#include <vulkan/vulkan_raii.hpp>

namespace vent {

class vulkan_pipeline final : public i_pipeline {
public:
    vulkan_pipeline(const vk::raii::Device& device,
                    const pipeline_desc&    desc,
                    vk::Format              swapchain_image_format,
                    vk::Extent2D            swapchain_extent);
    ~vulkan_pipeline() override = default;

    auto get_pipeline() const -> const vk::raii::Pipeline& { return _pipeline; }
    auto get_pipeline_layout() const -> const vk::raii::PipelineLayout& {
        return _pipeline_layout;
    }

private:
    vk::raii::PipelineLayout _pipeline_layout = nullptr;
    vk::raii::Pipeline       _pipeline        = nullptr;

    [[nodiscard]]
    auto create_shader_module(const vk::raii::Device& device,
                              const shader_asset*     shader) const
        -> vk::raii::ShaderModule;
};

}  // namespace vent
