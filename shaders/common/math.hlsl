// ─── Common Math Utilities ───────────────────────────────────────────────

#ifndef NGE_MATH_HLSL
#define NGE_MATH_HLSL

static const float PI        = 3.14159265358979323846;
static const float TWO_PI    = 6.28318530717958647692;
static const float HALF_PI   = 1.57079632679489661923;
static const float INV_PI    = 0.31830988618379067154;
static const float EPSILON   = 1e-6;
static const float FLT_MAX   = 3.402823466e+38;

// ─── Encoding / Decoding ─────────────────────────────────────────────────

// Octahedral normal encoding (unit vector → 2 floats in [-1,1])
float2 OctEncode(float3 n) {
    float t = abs(n.x) + abs(n.y) + abs(n.z);
    float2 o = n.xy / t;
    if (n.z < 0.0) {
        o = (1.0 - abs(o.yx)) * (o.xy >= 0.0 ? 1.0 : -1.0);
    }
    return o;
}

float3 OctDecode(float2 o) {
    float3 n = float3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

// Pack two floats in [0,1] into a single uint
uint PackUnorm2x16(float2 v) {
    uint x = (uint)(clamp(v.x, 0.0, 1.0) * 65535.0 + 0.5);
    uint y = (uint)(clamp(v.y, 0.0, 1.0) * 65535.0 + 0.5);
    return x | (y << 16);
}

float2 UnpackUnorm2x16(uint p) {
    float x = (float)(p & 0xFFFF) / 65535.0;
    float y = (float)(p >> 16) / 65535.0;
    return float2(x, y);
}

// ─── Barycentric Interpolation ───────────────────────────────────────────

float3 BarycentricInterpolate(float3 a, float3 b, float3 c, float3 bary) {
    return a * bary.x + b * bary.y + c * bary.z;
}

float2 BarycentricInterpolate2(float2 a, float2 b, float2 c, float3 bary) {
    return a * bary.x + b * bary.y + c * bary.z;
}

// ─── Depth / Position Reconstruction ─────────────────────────────────────

float LinearizeDepth(float depth, float nearZ, float farZ) {
    return nearZ * farZ / (farZ + depth * (nearZ - farZ));
}

float3 ReconstructWorldPos(float2 uv, float depth, float4x4 invViewProj) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y; // Vulkan NDC
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

// ─── Random / Sampling ───────────────────────────────────────────────────

// PCG hash for shader random numbers
uint PCGHash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(inout uint seed) {
    seed = PCGHash(seed);
    return (float)seed / 4294967295.0;
}

float2 RandomFloat2(inout uint seed) {
    return float2(RandomFloat(seed), RandomFloat(seed));
}

// Cosine-weighted hemisphere sampling
float3 SampleCosineHemisphere(float2 xi) {
    float r = sqrt(xi.x);
    float theta = TWO_PI * xi.y;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - xi.x));
    return float3(x, y, z);
}

// Uniform hemisphere sampling
float3 SampleUniformHemisphere(float2 xi) {
    float z = xi.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = TWO_PI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

// Build TBN from normal (for transforming hemisphere samples)
float3x3 BuildTBN(float3 N) {
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    return float3x3(T, B, N);
}

// ─── Halton Sequence (for temporal jitter) ───────────────────────────────

float Halton(uint index, uint base) {
    float result = 0.0;
    float f = 1.0 / (float)base;
    uint i = index;
    while (i > 0) {
        result += f * (float)(i % base);
        i = i / base;
        f /= (float)base;
    }
    return result;
}

float2 HaltonJitter(uint frameIndex) {
    return float2(Halton(frameIndex, 2), Halton(frameIndex, 3)) - 0.5;
}

#endif // NGE_MATH_HLSL
