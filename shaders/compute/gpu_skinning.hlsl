// ─── GPU Skinning Compute Shader ─────────────────────────────────────────
// Transforms vertices by bone matrices on the GPU.
// One thread per vertex. Outputs skinned position + normal + tangent
// into a separate vertex buffer for rendering.
//
// Supports up to 4 bone influences per vertex.
// Skinning matrices are precomputed on CPU: world * invBindPose.

#include "../common/math.hlsl"

struct SkinningConstants {
    uint vertexCount;
    uint boneCount;
    uint meshOffset;    // Offset into global vertex buffer
    uint outputOffset;  // Offset into output skinned buffer
};

[[vk::push_constant]] ConstantBuffer<SkinningConstants> pc;

// ─── Input Structures ────────────────────────────────────────────────────

struct SkinnedVertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texcoord;
    uint4  boneIndices;  // Up to 4 bone influences
    float4 boneWeights;
};

struct OutputVertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texcoord;
};

// ─── Buffers ─────────────────────────────────────────────────────────────

StructuredBuffer<SkinnedVertex>  g_InputVertices   : register(t0, space12);
StructuredBuffer<float4x4>       g_BoneMatrices    : register(t1, space12);
RWStructuredBuffer<OutputVertex> g_OutputVertices   : register(u0, space12);

// ─── Dual Quaternion Skinning (optional, higher quality) ─────────────────

struct DualQuat {
    float4 real;
    float4 dual;
};

StructuredBuffer<DualQuat> g_BoneDualQuats : register(t2, space12);

DualQuat DQNormalize(DualQuat dq) {
    float len = length(dq.real);
    DualQuat result;
    result.real = dq.real / len;
    result.dual = dq.dual / len;
    return result;
}

float3 DQTransformPoint(DualQuat dq, float3 p) {
    // Rotate
    float3 r = dq.real.xyz;
    float rw = dq.real.w;
    float3 t = 2.0 * (dq.dual.w * r - dq.dual.xyz * rw + cross(r, dq.dual.xyz));
    float3 rotated = p + 2.0 * cross(r, cross(r, p) + rw * p);
    return rotated + t;
}

float3 DQTransformNormal(DualQuat dq, float3 n) {
    float3 r = dq.real.xyz;
    float rw = dq.real.w;
    return n + 2.0 * cross(r, cross(r, n) + rw * n);
}

// ─── Linear Blend Skinning ───────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSLinearBlend(uint3 DTid : SV_DispatchThreadID) {
    uint vertexIdx = DTid.x;
    if (vertexIdx >= pc.vertexCount) return;

    SkinnedVertex v = g_InputVertices[pc.meshOffset + vertexIdx];

    // Accumulate weighted bone transforms
    float4x4 skinMatrix = (float4x4)0;

    [unroll]
    for (uint i = 0; i < 4; ++i) {
        float weight = v.boneWeights[i];
        if (weight <= 0.0) continue;

        uint boneIdx = v.boneIndices[i];
        if (boneIdx >= pc.boneCount) continue;

        skinMatrix += g_BoneMatrices[boneIdx] * weight;
    }

    // Transform position
    float4 skinnedPos = mul(skinMatrix, float4(v.position, 1.0));

    // Transform normal (using upper-left 3×3, assumes uniform scale)
    float3x3 normalMatrix = (float3x3)skinMatrix;
    float3 skinnedNormal = normalize(mul(normalMatrix, v.normal));

    // Transform tangent
    float3 skinnedTangent = normalize(mul(normalMatrix, v.tangent.xyz));

    // Write output
    OutputVertex out;
    out.position = skinnedPos.xyz;
    out.normal   = skinnedNormal;
    out.tangent  = float4(skinnedTangent, v.tangent.w); // Preserve bitangent sign
    out.texcoord = v.texcoord;

    g_OutputVertices[pc.outputOffset + vertexIdx] = out;
}

// ─── Dual Quaternion Skinning ────────────────────────────────────────────
// Higher quality than LBS — no volume loss artifacts on twisted joints.

[numthreads(64, 1, 1)]
void CSDualQuat(uint3 DTid : SV_DispatchThreadID) {
    uint vertexIdx = DTid.x;
    if (vertexIdx >= pc.vertexCount) return;

    SkinnedVertex v = g_InputVertices[pc.meshOffset + vertexIdx];

    // Accumulate weighted dual quaternions
    DualQuat blendedDQ;
    blendedDQ.real = float4(0, 0, 0, 0);
    blendedDQ.dual = float4(0, 0, 0, 0);

    // Ensure consistent hemisphere (antipodality fix)
    float4 firstReal = float4(0, 0, 0, 1);
    bool firstSet = false;

    [unroll]
    for (uint i = 0; i < 4; ++i) {
        float weight = v.boneWeights[i];
        if (weight <= 0.0) continue;

        uint boneIdx = v.boneIndices[i];
        if (boneIdx >= pc.boneCount) continue;

        DualQuat boneDQ = g_BoneDualQuats[boneIdx];

        // Antipodality: flip if dot product with first bone's quaternion is negative
        if (!firstSet) {
            firstReal = boneDQ.real;
            firstSet = true;
        } else {
            if (dot(boneDQ.real, firstReal) < 0.0) {
                boneDQ.real = -boneDQ.real;
                boneDQ.dual = -boneDQ.dual;
            }
        }

        blendedDQ.real += boneDQ.real * weight;
        blendedDQ.dual += boneDQ.dual * weight;
    }

    blendedDQ = DQNormalize(blendedDQ);

    // Transform
    float3 skinnedPos = DQTransformPoint(blendedDQ, v.position);
    float3 skinnedNormal = normalize(DQTransformNormal(blendedDQ, v.normal));
    float3 skinnedTangent = normalize(DQTransformNormal(blendedDQ, v.tangent.xyz));

    OutputVertex out;
    out.position = skinnedPos;
    out.normal   = skinnedNormal;
    out.tangent  = float4(skinnedTangent, v.tangent.w);
    out.texcoord = v.texcoord;

    g_OutputVertices[pc.outputOffset + vertexIdx] = out;
}
