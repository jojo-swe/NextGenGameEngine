// ─── Basic Triangle Vertex Shader ─────────────────────────────────────────
// First triangle test — hardcoded fullscreen triangle for smoke testing.

struct VSInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR;
};

struct PushConstants {
    float4x4 viewProj;
    float4   cameraPos;
    uint     frameIndex;
    uint     screenWidth;
    uint     screenHeight;
    uint     pad0;
    float    time;
    float    deltaTime;
    uint     pad1;
    uint     pad2;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.viewProj, float4(input.position, 1.0));
    output.color    = input.color;
    return output;
}
