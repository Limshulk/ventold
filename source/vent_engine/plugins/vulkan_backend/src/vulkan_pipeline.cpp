//
// vulkan_backend plugin.
// vulkan graphics pipeline implementation.
// ——————————————————————

#include <vulkan_pipeline.hpp>

#include <_vent/accessors.hpp>
#include <_vent/renderer/vertex.hpp>

#include <array>

namespace vent {

vulkan_pipeline::vulkan_pipeline(
    const vk::raii::Device&              device,
    const vk::raii::DescriptorSetLayout& global_descriptor_set_layout,
    const pipeline_desc&                 desc,
    vk::Format                           swapchain_image_format,
    vk::Format                           depth_image_format,
    vk::Extent2D                         swapchain_extent) {
    log()->trace("vulkan", "creating graphics pipeline");

    // --- shader modules ---
    // https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/01_Shader_modules.html

    if (!desc.shader || desc.shader->spirv_bytecode.empty()) {
        log()->error("vulkan",
                     "invalid shader provided for pipeline creation.");
        return;
    }

    vk::raii::ShaderModule shader_module =
        create_shader_module(device, desc.shader);

    vk::PipelineShaderStageCreateInfo vert_shader_stage_info {
        .stage  = vk::ShaderStageFlagBits::eVertex,
        .module = *shader_module,
        .pName  = desc.vertex_entry.c_str()};

    vk::PipelineShaderStageCreateInfo frag_shader_stage_info {
        .stage  = vk::ShaderStageFlagBits::eFragment,
        .module = *shader_module,
        .pName  = desc.fragment_entry.c_str()};

    std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages = {
        vert_shader_stage_info, frag_shader_stage_info};

    // --- fixed-functions ---
    // https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/02_Fixed_functions.html

    // dynamic state.

    std::vector<vk::DynamicState> dynamic_state = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state_info {
        .dynamicStateCount = static_cast<uint32_t>(dynamic_state.size()),
        .pDynamicStates    = dynamic_state.data()};

    // vertex input.
    vk::VertexInputBindingDescription binding_description {
        .binding   = 0,
        .stride    = sizeof(vent::vertex),
        .inputRate = vk::VertexInputRate::eVertex};

    std::array<vk::VertexInputAttributeDescription, 3> attribute_descriptions = {
        {{.location = 0,
          .binding  = 0,
          .format   = vk::Format::eR32G32B32Sfloat,
          .offset   = offsetof(vent::vertex, position)},
         {.location = 1,
          .binding  = 0,
          .format   = vk::Format::eR32G32B32Sfloat,
          .offset   = offsetof(vent::vertex, color)},
         {.location = 2,
          .binding  = 0,
          .format   = vk::Format::eR32G32Sfloat,
          .offset   = offsetof(vent::vertex, u)}}};

    vk::PipelineVertexInputStateCreateInfo vertex_input_info {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions    = &binding_description,
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attribute_descriptions.size()),
        .pVertexAttributeDescriptions = attribute_descriptions.data()};

    // input assembly.
    vk::PipelineInputAssemblyStateCreateInfo input_assembly {
        .topology = vk::PrimitiveTopology::eTriangleList};

    // viewports and scissors.
    // we use dynamic state for viewport and scissor so we don't have to
    // recreate the pipeline on resize.
    vk::PipelineViewportStateCreateInfo viewport_state {.viewportCount = 1,
                                                        .scissorCount  = 1};

    // rasterizer.
    // backface culling. we render with our right-handed z-up world basis and a
    // projection that flips y for vulkan's clip space (see math/mat4.hpp). that
    // y-flip reverses the apparent winding of triangles on screen, so a triangle
    // authored counter-clockwise in world space presents as clockwise after
    // projection. we therefore declare clockwise as the front face and cull the
    // back. (eNone was a temporary workaround that shaded both sides and hid
    // winding bugs — culling is cheaper and surfaces such bugs early.)
    vk::PipelineRasterizationStateCreateInfo rasterizer {
        .depthClampEnable        = false,
        .rasterizerDiscardEnable = false,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eClockwise,
        .depthBiasEnable         = false,
        .lineWidth               = 1.0f};

    // multisampling.
    vk::PipelineMultisampleStateCreateInfo multisampling {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable  = false};

    // color blending.
    vk::PipelineColorBlendAttachmentState color_blend_attachment {
        .blendEnable = false,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    vk::PipelineColorBlendStateCreateInfo color_blending {
        .logicOpEnable   = false,
        .logicOp         = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment};

    // push constants.
    vk::PushConstantRange push_constant_range {
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset     = 0,
        .size       = sizeof(math::mat4)};

    // pipeline layout.
    vk::PipelineLayoutCreateInfo pipeline_layout_info {
        .setLayoutCount         = 1,
        .pSetLayouts            = &*global_descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_constant_range};

    try {
        _pipeline_layout =
            vk::raii::PipelineLayout(device, pipeline_layout_info);
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create pipeline layout: {}", err.what());
        return;
    }

    // depth and stencil state.
    vk::PipelineDepthStencilStateCreateInfo depth_stencil {
        .depthTestEnable       = true,
        .depthWriteEnable      = true,
        .depthCompareOp        = vk::CompareOp::eLess,
        .depthBoundsTestEnable = false,
        .stencilTestEnable     = false,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f};

    // --- pipeline creation ---

    vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                       vk::PipelineRenderingCreateInfo>
        pipeline_create_info_chain = {
            {.stageCount          = static_cast<uint32_t>(shader_stages.size()),
             .pStages             = shader_stages.data(),
             .pVertexInputState   = &vertex_input_info,
             .pInputAssemblyState = &input_assembly,
             .pViewportState      = &viewport_state,
             .pRasterizationState = &rasterizer,
             .pMultisampleState   = &multisampling,
             .pDepthStencilState  = &depth_stencil,
             .pColorBlendState    = &color_blending,
             .pDynamicState       = &dynamic_state_info,
             .layout              = *_pipeline_layout,
             .renderPass          = nullptr},
            {.colorAttachmentCount    = 1,
             .pColorAttachmentFormats = &swapchain_image_format,
             .depthAttachmentFormat   = depth_image_format}};

    try {
        _pipeline = vk::raii::Pipeline(
            device,
            nullptr,
            pipeline_create_info_chain.get<vk::GraphicsPipelineCreateInfo>());
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create graphics pipeline: {}", err.what());
    }

    log()->trace("vulkan", "graphics pipeline created successfully.");
}

auto vulkan_pipeline::create_shader_module(const vk::raii::Device& device,
                                           const shader_asset*     shader) const
    -> vk::raii::ShaderModule {
    vk::ShaderModuleCreateInfo create_info {
        .codeSize = shader->spirv_bytecode.size() * sizeof(u32),
        .pCode    = shader->spirv_bytecode.data()};
    return vk::raii::ShaderModule(device, create_info);
}

}  // namespace vent
