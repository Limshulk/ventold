struct VertexInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> u_buffer;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput in) {
    VertexOutput output;
    output.position = mul(u_buffer.proj, mul(u_buffer.view, mul(u_buffer.model, float4(in.position, 1.0))));
    output.color = in.color;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput in) : SV_Target
{
    float3 color = in.color;
    return float4(color, 1.0);
}