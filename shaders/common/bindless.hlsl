// ─── Bindless Resource Access ─────────────────────────────────────────────
// Global descriptor arrays shared across all shaders.
// Resources are referenced by index (pushed via push constants or storage buffers).

// Binding 0: All sampled textures
Texture2D g_Textures[]   : register(t0, space0);

// Binding 1: All storage buffers (accessed as ByteAddressBuffer for flexibility)
ByteAddressBuffer g_Buffers[] : register(t0, space1);

// Binding 2: All samplers
SamplerState g_Samplers[] : register(s0, space2);

// ─── Push Constants ──────────────────────────────────────────────────────
// 128 bytes of push constants available to every shader.
struct PushConstants {
    float4x4 viewProj;       // 64 bytes
    float4   cameraPos;      // 16 bytes
    uint     frameIndex;     // 4 bytes
    uint     screenWidth;    // 4 bytes
    uint     screenHeight;   // 4 bytes
    uint     pad0;           // 4 bytes
    float    time;           // 4 bytes
    float    deltaTime;      // 4 bytes
    uint     pad1;           // 4 bytes
    uint     pad2;           // 4 bytes
};                           // Total: 112 bytes

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// ─── Utility: Sample a bindless texture ──────────────────────────────────
float4 SampleBindless(uint textureIndex, uint samplerIndex, float2 uv) {
    return g_Textures[textureIndex].Sample(g_Samplers[samplerIndex], uv);
}

float4 SampleBindlessLevel(uint textureIndex, uint samplerIndex, float2 uv, float lod) {
    return g_Textures[textureIndex].SampleLevel(g_Samplers[samplerIndex], uv, lod);
}
