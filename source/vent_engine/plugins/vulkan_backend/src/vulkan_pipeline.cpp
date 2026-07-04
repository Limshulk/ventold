//
// vulkan_backend plugin.
// vulkan graphics pipeline implementation.
// ——————————————————————

#include <vulkan_pipeline.hpp>
#include <_vent/accessors.hpp>
#include <array>

namespace vent {

vulkan_pipeline::vulkan_pipeline(const vk::raii::Device& device, const pipeline_desc& desc, vk::Format swapchain_image_format, vk::Extent2D swapchain_extent) {
    
    // --- Shader modules ---
    if (!desc.shader || desc.shader->spirv_bytecode.empty()) {
        log()->error("vulkan", "invalid shader provided for pipeline creation.");
        return;
    }

    vk::raii::ShaderModule shader_module = create_shader_module(device, desc.shader);

    vk::PipelineShaderStageCreateInfo vert_shader_stage_info {
        .stage  = vk::ShaderStageFlagBits::eVertex,
        .module = *shader_module,
        .pName  = desc.vertex_entry.c_str()
    };

    vk::PipelineShaderStageCreateInfo frag_shader_stage_info {
        .stage  = vk::ShaderStageFlagBits::eFragment,
        .module = *shader_module,
        .pName  = desc.fragment_entry.c_str()
    };

    std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages = {
        vert_shader_stage_info, frag_shader_stage_info
    };

    // --- Fixed-function state ---

    // 1. Vertex Input
    vk::PipelineVertexInputStateCreateInfo vertex_input_info {
        .vertexBindingDescriptionCount   = 0,
        .pVertexBindingDescriptions      = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions    = nullptr
    };

    // 2. Input Assembly
    vk::PipelineInputAssemblyStateCreateInfo input_assembly {
        .topology               = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = false
    };

    // 3. Viewports and Scissors
    // We use dynamic state for viewport and scissor so we don't have to recreate the pipeline on resize.
    vk::PipelineViewportStateCreateInfo viewport_state {
        .viewportCount = 1,
        .scissorCount  = 1
    };

    // 4. Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer {
        .depthClampEnable        = false,
        .rasterizerDiscardEnable = false,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eClockwise,
        .depthBiasEnable         = false,
        .lineWidth               = 1.0f
    };

    // 5. Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling {
        .rasterizationSamples  = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable   = false
    };

    // 6. Color blending
    vk::PipelineColorBlendAttachmentState color_blend_attachment {
        .blendEnable    = false,
        .colorWriteMask = vk::ColorComponentFlagBits::eR |
                          vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB |
                          vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo color_blending {
        .logicOpEnable   = false,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment
    };

    // 7. Dynamic State
    std::array<vk::DynamicState, 2> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state_info {
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates    = dynamic_states.data()
    };

    // --- Pipeline layout ---
    vk::PipelineLayoutCreateInfo pipeline_layout_info {
        .setLayoutCount         = 0,
        .pushConstantRangeCount = 0
    };

    try {
        _pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_info);
    } catch (const vk::SystemError& err) {
        log()->error("vulkan", "failed to create pipeline layout: {}", err.what());
        return;
    }

    // --- Pipeline creation (Dynamic Rendering) ---
    // Instead of using a Render Pass, we configure the pipeline for dynamic rendering.
    vk::PipelineRenderingCreateInfoKHR pipeline_rendering_create_info {
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_image_format
    };

    vk::GraphicsPipelineCreateInfo pipeline_info {
        .pNext               = &pipeline_rendering_create_info, // Use dynamic rendering!
        .stageCount          = static_cast<uint32_t>(shader_stages.size()),
        .pStages             = shader_stages.data(),
        .pVertexInputState   = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pColorBlendState    = &color_blending,
        .pDynamicState       = &dynamic_state_info,
        .layout              = *_pipeline_layout,
        .renderPass          = nullptr, // No render pass needed
        .subpass             = 0,
        .basePipelineHandle  = nullptr
    };

    try {
        _pipeline = vk::raii::Pipeline(device, nullptr, pipeline_info);
    } catch (const vk::SystemError& err) {
        log()->error("vulkan", "failed to create graphics pipeline: {}", err.what());
    }
}

auto vulkan_pipeline::create_shader_module(const vk::raii::Device& device, const shader_asset* shader) const -> vk::raii::ShaderModule {
    vk::ShaderModuleCreateInfo create_info {
        .codeSize = shader->spirv_bytecode.size() * sizeof(u32),
        .pCode    = shader->spirv_bytecode.data()
    };
    return vk::raii::ShaderModule(device, create_info);
}

} // namespace vent
