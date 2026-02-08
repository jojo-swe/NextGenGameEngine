// ─── Debug Line Rendering Shader ──────────────────────────────────────────
// Simple vertex-colored line rendering for debug visualization.
// Used by DebugRenderer for gizmos, physics wireframes, nav mesh, etc.

struct DebugConstants {
    float4x4 viewProj;
};

[[vk::push_constant]] ConstantBuffer<DebugConstants> pc;

struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOutput {
    float4 clipPos : SV_Position;
    float4 color   : COLOR;
};

// ─── Vertex Shader ───────────────────────────────────────────────────────

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.clipPos = mul(pc.viewProj, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

float4 PSMain(VSOutput input) : SV_Target0 {
    return input.color;
}

// ─── Depth-tested variant ────────────────────────────────────────────────
// Same shaders, but the pipeline state has depth test enabled.
// The no-depth variant uses the same shaders with depth test disabled in PSO.
