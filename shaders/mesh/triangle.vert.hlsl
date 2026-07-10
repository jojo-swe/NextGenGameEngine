// ─── Instanced Cube Vertex Shader ─────────────────────────────────────────
// Renders a grid of cubes with per-instance offset, scale, and color tint.

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    // Per-instance data (binding 1)
    float4 offsetScale : INSTANCE_OFFSET_SCALE;
    float4 colorTint    : INSTANCE_COLOR_TINT;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    float3 worldPos : WORLD_POS;
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
    // Apply per-instance offset and scale
    float3 worldPos = input.position * input.offsetScale.w + input.offsetScale.xyz;
    output.position = mul(pc.viewProj, float4(worldPos, 1.0));
    output.normal   = input.normal;
    output.color    = input.color * input.colorTint.rgb;
    output.worldPos = worldPos;
    return output;
}
