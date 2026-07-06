// vent engine asset.
// default shader: camera ubo + per-draw texture, vertex-color modulated.
// ——————————————————————
//
// descriptor sets are organized by update frequency (must match the layouts
// in the vulkan backend):
//   set 0, binding 0: per-frame camera matrices (per-window ubo ring).
//   set 1, binding 0: per-draw combined image sampler (one set per texture).

struct VertexInput {
    float3 position : POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD;
};

struct UniformBuffer {
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> u_frame;
[[vk::binding(0, 1)]] Sampler2D tex_sampler;

struct PushConstants {
    float4x4 model;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> push;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color;
    float2 uv;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput in) {
    VertexOutput output;
    output.position = mul(u_frame.proj, mul(u_frame.view, mul(push.model, float4(in.position, 1.0))));
    output.color = in.color;
    output.uv = in.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput in) : SV_Target
{
    float4 tex_color = tex_sampler.Sample(in.uv);
    return float4(in.color * tex_color.rgb, tex_color.a);
}
