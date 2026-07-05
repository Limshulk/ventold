#pragma once
//
// vulkan_backend plugin.
// vulkan graphics pipeline.
// ——————————————————————
//
// encapsulates the vulkan graphics pipeline following the vulkan tutorial.

#include <_vent/asset/shader.hpp>

#include <_vent/renderer/pipeline_desc.hpp>

#include <vulkan/vulkan_raii.hpp>

namespace vent {

class vulkan_pipeline final {
public:
    vulkan_pipeline(
        const vk::raii::Device&              device,
        const vk::raii::DescriptorSetLayout& global_descriptor_set_layout,
        const pipeline_desc&                 desc,
        vk::Format                           swapchain_image_format,
        vk::Format                           depth_image_format,
        vk::Extent2D                         swapchain_extent);
    ~vulkan_pipeline() = default;

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
