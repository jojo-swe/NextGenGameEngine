// ─── Deferred Screen-Space Decals ─────────────────────────────────────────
// Projects decal textures onto scene geometry in screen space.
// Reads depth buffer to reconstruct world position, then projects into
// decal's OBB (oriented bounding box) to compute UVs.
//
// Modifies albedo, normal, and roughness of the underlying surface.
// Supports blending modes: alpha, additive, multiply.

#include "../common/math.hlsl"

struct DecalConstants {
    float4x4 invViewProj;
    float4x4 decalWorldToLocal;  // Transforms world pos into decal's local [0,1]^3 space
    float4x4 decalLocalToWorld;
    float4   decalColor;         // RGBA tint
    float4   decalParams;        // x = normalStrength, y = roughnessOverride, z = metallicOverride, w = opacity
    uint2    screenSize;
    uint     blendMode;          // 0 = alpha, 1 = additive, 2 = multiply
    float    angleFade;          // Fade based on surface-to-decal angle (cosine threshold)
};

[[vk::push_constant]] ConstantBuffer<DecalConstants> pc;

Texture2D<float>    g_DepthBuffer     : register(t0, space16);
Texture2D<float4>   g_GBuffer_Normal  : register(t1, space16); // World normal

Texture2D<float4>   g_DecalAlbedo     : register(t2, space16);
Texture2D<float4>   g_DecalNormal     : register(t3, space16);

RWTexture2D<float4> g_OutputAlbedo    : register(u0, space16);
RWTexture2D<float4> g_OutputNormal    : register(u1, space16);
RWTexture2D<float2> g_OutputRoughMetal : register(u2, space16);

SamplerState g_LinearClamp : register(s0, space16);

// ─── Main ────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(pc.screenSize);

    // Reconstruct world position from depth
    float depth = g_DepthBuffer[DTid.xy];
    if (depth <= 0.0) return;

    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos4 = mul(pc.invViewProj, clipPos);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // Project into decal local space
    float4 localPos4 = mul(pc.decalWorldToLocal, float4(worldPos, 1.0));
    float3 localPos = localPos4.xyz;

    // Check if inside decal OBB [0,1]^3
    if (any(localPos < 0.0) || any(localPos > 1.0)) return;

    // Decal UV from XZ plane (Y is depth into surface)
    float2 decalUV = localPos.xz;

    // Angle-based fade: reject pixels whose normal faces away from decal
    float3 worldNormal = normalize(g_GBuffer_Normal[DTid.xy].xyz * 2.0 - 1.0);
    float3 decalForward = normalize(pc.decalLocalToWorld[1].xyz); // Y axis = decal projection dir
    float angleCos = abs(dot(worldNormal, decalForward));
    if (angleCos < pc.angleFade) return;
    float angleFade = saturate((angleCos - pc.angleFade) / (1.0 - pc.angleFade));

    // Depth fade: soften at edges of OBB depth
    float depthFade = 1.0 - abs(localPos.y * 2.0 - 1.0);
    depthFade = saturate(depthFade * 4.0); // Sharp-ish falloff

    // Sample decal textures
    float4 decalColor = g_DecalAlbedo.SampleLevel(g_LinearClamp, decalUV, 0) * pc.decalColor;
    float4 decalNormal = g_DecalNormal.SampleLevel(g_LinearClamp, decalUV, 0);

    float alpha = decalColor.a * pc.decalParams.w * angleFade * depthFade;
    if (alpha < 0.01) return;

    // Read current G-buffer values
    float4 currentAlbedo = g_OutputAlbedo[DTid.xy];
    float4 currentNormal = g_OutputNormal[DTid.xy];
    float2 currentRM = g_OutputRoughMetal[DTid.xy];

    // Blend albedo
    float3 blendedAlbedo;
    if (pc.blendMode == 0) {
        // Alpha blend
        blendedAlbedo = lerp(currentAlbedo.rgb, decalColor.rgb, alpha);
    } else if (pc.blendMode == 1) {
        // Additive
        blendedAlbedo = currentAlbedo.rgb + decalColor.rgb * alpha;
    } else {
        // Multiply
        blendedAlbedo = lerp(currentAlbedo.rgb, currentAlbedo.rgb * decalColor.rgb, alpha);
    }
    g_OutputAlbedo[DTid.xy] = float4(blendedAlbedo, currentAlbedo.a);

    // Blend normal
    if (pc.decalParams.x > 0.0) {
        float3 decalN = decalNormal.xyz * 2.0 - 1.0;
        // Transform decal normal from local tangent space to world space
        float3 decalT = normalize(pc.decalLocalToWorld[0].xyz);
        float3 decalB = normalize(pc.decalLocalToWorld[2].xyz);
        float3 decalWorldN = normalize(decalT * decalN.x + decalForward * decalN.z + decalB * decalN.y);

        float normalAlpha = alpha * pc.decalParams.x;
        float3 blendedN = normalize(lerp(currentNormal.xyz * 2.0 - 1.0, decalWorldN, normalAlpha));
        g_OutputNormal[DTid.xy] = float4(blendedN * 0.5 + 0.5, currentNormal.w);
    }

    // Override roughness/metallic if specified
    if (pc.decalParams.y >= 0.0) {
        float2 blendedRM = lerp(currentRM, float2(pc.decalParams.y, pc.decalParams.z), alpha * 0.5);
        g_OutputRoughMetal[DTid.xy] = blendedRM;
    }
}
