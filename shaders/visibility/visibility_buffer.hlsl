// ─── Visibility Buffer Generation ─────────────────────────────────────────
// Pass 1 of the GPU-driven pipeline.
// Rasterizes meshlet/triangle IDs to an R32G32_UINT render target.
//
// Output:
//   R: meshletId (20 bits) | triangleId (12 bits)
//   G: materialId (16 bits) | instanceId (16 bits)

#include "../common/math.hlsl"

struct VisBufferOutput {
    uint2 visibility : SV_Target0;
};

// Mesh shader path: meshlet data comes from task/mesh shader
struct MSOutput {
    float4 position   : SV_Position;
    uint   meshletId  : MESHLET_ID;
    uint   triangleId : TRIANGLE_ID;
    uint   materialId : MATERIAL_ID;
    uint   instanceId : INSTANCE_ID;
};

VisBufferOutput main(MSOutput input) {
    VisBufferOutput output;

    // Pack meshlet + triangle into R channel
    output.visibility.x = (input.meshletId << 12) | (input.triangleId & 0xFFF);

    // Pack material + instance into G channel
    output.visibility.y = (input.materialId << 16) | (input.instanceId & 0xFFFF);

    return output;
}
