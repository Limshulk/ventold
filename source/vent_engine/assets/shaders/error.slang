// vent engine asset.
// error shader: solid magenta — the classic "something is wrong but visible".
// ——————————————————————
//
// used as the fallback pipeline when the default shader is missing or broken.
// a wrong-but-visible frame is debuggable; a black screen is not. matches the
// default shader's pipeline layout (set 0 camera ubo, push-constant model) so
// it can be swapped in without touching binding code; it simply ignores the
// texture set.

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

struct PushConstants {
    float4x4 model;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> push;

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput in) {
    VertexOutput output;
    output.position = mul(u_frame.proj, mul(u_frame.view, mul(push.model, float4(in.position, 1.0))));
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput in) : SV_Target
{
    return float4(1.0, 0.0, 1.0, 1.0);
}
