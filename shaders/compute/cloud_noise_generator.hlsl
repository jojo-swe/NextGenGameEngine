// ─── Procedural Cloud Noise Generator ────────────────────────────────────
// GPU compute shader for generating 3D noise textures used in
// volumetric cloud rendering. Produces tileable Worley, Perlin,
// and Perlin-Worley hybrid volumes.
//
// Output textures:
//   - Base shape noise (128^3): RGBA = Perlin-Worley, Worley x3 octaves
//   - Detail noise (32^3): RGBA = Worley at 3 frequencies + Perlin-Worley
//   - Curl noise (128^2): RG = 2D curl offset for weathering animation
//
// References:
//   - "Real-Time Volumetric Cloudscapes" (Schneider, SIGGRAPH 2015)
//   - "Nubis: Authoring Real-Time Volumetric Cloudscapes" (Schneider, 2017)
//   - "The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn" (Guerrilla)

// ─── Output ──────────────────────────────────────────────────────────────

RWTexture3D<float4> g_BaseShapeNoise  : register(u0); // 128^3 RGBA
RWTexture3D<float4> g_DetailNoise     : register(u1); // 32^3 RGBA
RWTexture2D<float2> g_CurlNoise       : register(u2); // 128^2 RG

struct CloudNoiseConstants {
    uint  baseResolution;       // 128
    uint  detailResolution;     // 32
    uint  curlResolution;       // 128
    uint  passType;             // 0=base, 1=detail, 2=curl
    float worleyJitter;         // Cell point jitter (default 1.0)
    float perlinFrequency;      // Base Perlin frequency (default 4.0)
    float curlStrength;         // Curl displacement strength (default 1.0)
    float seed;                 // Random seed for variation
};

[[vk::push_constant]] ConstantBuffer<CloudNoiseConstants> cb;

// ─── Hash Functions ──────────────────────────────────────────────────────

float3 Hash3(float3 p) {
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(p + cb.seed) * 43758.5453);
}

float Hash1(float3 p) {
    return frac(sin(dot(p, float3(127.1, 311.7, 74.7)) + cb.seed) * 43758.5453);
}

float2 Hash2(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p + cb.seed) * 43758.5453);
}

// ─── Worley Noise (F1) ──────────────────────────────────────────────────
// Returns distance to closest feature point in 3D grid. Tileable.

float WorleyNoise(float3 uv, float frequency) {
    float3 p = uv * frequency;
    float3 ip = floor(p);
    float3 fp = frac(p);

    float minDist = 1.0;

    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float3 neighbor = float3(x, y, z);
                // Wrap for tileability
                float3 cellId = fmod(ip + neighbor + frequency, frequency);
                float3 point = Hash3(cellId) * cb.worleyJitter;
                float3 diff = neighbor + point - fp;
                float dist = dot(diff, diff);
                minDist = min(minDist, dist);
            }
        }
    }

    return 1.0 - sqrt(minDist); // Invert: 1 at feature points, 0 far away
}

// ─── Perlin Noise (3D Tileable) ──────────────────────────────────────────

float3 PerlinGrad(float3 p) {
    return normalize(Hash3(p) * 2.0 - 1.0);
}

float PerlinNoise(float3 uv, float frequency) {
    float3 p = uv * frequency;
    float3 ip = floor(p);
    float3 fp = frac(p);

    // Quintic interpolation
    float3 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);

    // 8 corner gradients (tileable wrap)
    float n000 = dot(PerlinGrad(fmod(ip + float3(0,0,0), frequency)), fp - float3(0,0,0));
    float n100 = dot(PerlinGrad(fmod(ip + float3(1,0,0), frequency)), fp - float3(1,0,0));
    float n010 = dot(PerlinGrad(fmod(ip + float3(0,1,0), frequency)), fp - float3(0,1,0));
    float n110 = dot(PerlinGrad(fmod(ip + float3(1,1,0), frequency)), fp - float3(1,1,0));
    float n001 = dot(PerlinGrad(fmod(ip + float3(0,0,1), frequency)), fp - float3(0,0,1));
    float n101 = dot(PerlinGrad(fmod(ip + float3(1,0,1), frequency)), fp - float3(1,0,1));
    float n011 = dot(PerlinGrad(fmod(ip + float3(0,1,1), frequency)), fp - float3(0,1,1));
    float n111 = dot(PerlinGrad(fmod(ip + float3(1,1,1), frequency)), fp - float3(1,1,1));

    // Trilinear blend
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);

    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);

    return lerp(nxy0, nxy1, u.z) * 0.5 + 0.5; // Remap to [0, 1]
}

// ─── FBM (Fractal Brownian Motion) ───────────────────────────────────────

float PerlinFBM(float3 uv, float baseFreq, uint octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = baseFreq;

    for (uint i = 0; i < octaves; ++i) {
        value += PerlinNoise(uv, frequency) * amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

float WorleyFBM(float3 uv, float baseFreq, uint octaves) {
    float value = 0.0;
    float amplitude = 0.625; // Weighted toward lower octaves
    float frequency = baseFreq;

    for (uint i = 0; i < octaves; ++i) {
        value += WorleyNoise(uv, frequency) * amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// ─── Perlin-Worley Hybrid ────────────────────────────────────────────────
// Remap Perlin using Worley for cloud-like shapes (Schneider 2015).

float PerlinWorley(float3 uv, float perlinFreq, float worleyFreq) {
    float perlin = PerlinFBM(uv, perlinFreq, 4);
    float worley = WorleyFBM(uv, worleyFreq, 3);

    // Remap: use Perlin as base, modulated by Worley
    return saturate(lerp(worley, 1.0, saturate(perlin * 2.0)));
}

// ─── 2D Curl Noise ───────────────────────────────────────────────────────
// Divergence-free 2D vector field for animating cloud weathering.

float2 CurlNoise2D(float2 uv, float frequency) {
    float eps = 0.001;

    // Sample scalar noise field at offset positions
    float n0 = Hash1(float3(uv * frequency, 0));
    float nx = Hash1(float3((uv + float2(eps, 0)) * frequency, 0));
    float ny = Hash1(float3((uv + float2(0, eps)) * frequency, 0));

    // Curl = (dN/dy, -dN/dx)
    float dNdx = (nx - n0) / eps;
    float dNdy = (ny - n0) / eps;

    return float2(dNdy, -dNdx) * cb.curlStrength;
}

// ─── Pass 0: Base Shape Noise (128^3) ────────────────────────────────────
// RGBA channels:
//   R: Perlin-Worley (primary cloud shape)
//   G: Worley at frequency 1x (low freq detail)
//   B: Worley at frequency 2x (mid freq detail)
//   A: Worley at frequency 4x (high freq detail)

[numthreads(4, 4, 4)]
void CSBaseShapeNoise(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.baseResolution;
    if (any(DTid >= uint3(res, res, res))) return;

    float3 uv = (float3(DTid) + 0.5) / float(res);

    float pw = PerlinWorley(uv, cb.perlinFrequency, cb.perlinFrequency * 2.0);
    float w1 = WorleyFBM(uv, cb.perlinFrequency * 2.0, 3);
    float w2 = WorleyFBM(uv, cb.perlinFrequency * 4.0, 3);
    float w3 = WorleyFBM(uv, cb.perlinFrequency * 8.0, 3);

    g_BaseShapeNoise[DTid] = float4(pw, w1, w2, w3);
}

// ─── Pass 1: Detail Noise (32^3) ────────────────────────────────────────
// RGBA channels:
//   R: Worley at frequency 8x
//   G: Worley at frequency 16x
//   B: Worley at frequency 32x
//   A: Perlin-Worley detail

[numthreads(4, 4, 4)]
void CSDetailNoise(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.detailResolution;
    if (any(DTid >= uint3(res, res, res))) return;

    float3 uv = (float3(DTid) + 0.5) / float(res);

    float w1 = WorleyFBM(uv, 8.0, 3);
    float w2 = WorleyFBM(uv, 16.0, 3);
    float w3 = WorleyFBM(uv, 32.0, 3);
    float pw = PerlinWorley(uv, 8.0, 16.0);

    g_DetailNoise[DTid] = float4(w1, w2, w3, pw);
}

// ─── Pass 2: Curl Noise (128^2) ─────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSCurlNoise(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.curlResolution;
    if (any(DTid.xy >= uint2(res, res))) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float(res);

    // Multi-octave curl for complex flow
    float2 curl = CurlNoise2D(uv, 4.0);
    curl += CurlNoise2D(uv, 8.0) * 0.5;
    curl += CurlNoise2D(uv, 16.0) * 0.25;

    g_CurlNoise[DTid.xy] = curl;
}
