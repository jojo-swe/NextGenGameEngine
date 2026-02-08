// ─── Volumetric Cloud Noise Generator ────────────────────────────────────
// Generates 3D noise textures used by the volumetric cloud renderer.
// Produces two textures:
//   1. Base shape noise: Perlin-Worley FBM (128³, RGBA)
//      R: Perlin-Worley, G: Worley x1, B: Worley x2, A: Worley x4
//   2. Detail noise: Worley FBM (32³, RGBA)
//      R: Worley x1, G: Worley x2, B: Worley x4, A: Worley x8
//
// These are baked once at load time and sampled during cloud ray marching.
//
// References:
//   - "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn" (Schneider, SIGGRAPH 2015)
//   - "Nubis: Authoring Real-Time Volumetric Cloudscapes" (Schneider, SIGGRAPH 2017)

RWTexture3D<float4> g_BaseShapeNoise  : register(u0); // 128³
RWTexture3D<float4> g_DetailNoise     : register(u1); // 32³

struct CloudNoiseConstants {
    uint3  baseResolution;    // 128, 128, 128
    uint   pad0;
    uint3  detailResolution;  // 32, 32, 32
    uint   pad1;
    float  baseFrequency;     // 4.0
    float  detailFrequency;   // 8.0
    float  persistence;       // 0.5
    float  lacunarity;        // 2.0
    uint   worleySeed;        // Random seed for Worley points
    uint   perlinSeed;
    float  pad2;
    float  pad3;
};

[[vk::push_constant]] ConstantBuffer<CloudNoiseConstants> cb;

// ─── Hash functions for noise generation ─────────────────────────────────

uint Hash(uint x) {
    x ^= x >> 16;
    x *= 0x45d9f3b;
    x ^= x >> 16;
    x *= 0x45d9f3b;
    x ^= x >> 16;
    return x;
}

uint Hash3(uint3 p) {
    return Hash(p.x ^ Hash(p.y ^ Hash(p.z)));
}

float HashFloat(uint x) {
    return float(Hash(x)) / float(0xFFFFFFFF);
}

float3 HashFloat3(uint3 p, uint seed) {
    uint h = Hash3(p + seed);
    return float3(
        HashFloat(h),
        HashFloat(h * 16807u + 1u),
        HashFloat(h * 48271u + 2u)
    );
}

// ─── Perlin Noise ────────────────────────────────────────────────────────

float3 Fade(float3 t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float GradientDot(uint hash, float3 p) {
    // 12 gradient directions
    uint h = hash & 15u;
    float u = h < 8u ? p.x : p.y;
    float v = h < 4u ? p.y : (h == 12u || h == 14u ? p.x : p.z);
    return ((h & 1u) == 0u ? u : -u) + ((h & 2u) == 0u ? v : -v);
}

float PerlinNoise3D(float3 pos, uint seed) {
    float3 floorPos = floor(pos);
    float3 fracPos = pos - floorPos;
    int3 ip = int3(floorPos);

    float3 f = Fade(fracPos);

    // Hash 8 corners
    uint h000 = Hash3(uint3(ip + int3(0, 0, 0)) + seed);
    uint h100 = Hash3(uint3(ip + int3(1, 0, 0)) + seed);
    uint h010 = Hash3(uint3(ip + int3(0, 1, 0)) + seed);
    uint h110 = Hash3(uint3(ip + int3(1, 1, 0)) + seed);
    uint h001 = Hash3(uint3(ip + int3(0, 0, 1)) + seed);
    uint h101 = Hash3(uint3(ip + int3(1, 0, 1)) + seed);
    uint h011 = Hash3(uint3(ip + int3(0, 1, 1)) + seed);
    uint h111 = Hash3(uint3(ip + int3(1, 1, 1)) + seed);

    float g000 = GradientDot(h000, fracPos - float3(0, 0, 0));
    float g100 = GradientDot(h100, fracPos - float3(1, 0, 0));
    float g010 = GradientDot(h010, fracPos - float3(0, 1, 0));
    float g110 = GradientDot(h110, fracPos - float3(1, 1, 0));
    float g001 = GradientDot(h001, fracPos - float3(0, 0, 1));
    float g101 = GradientDot(h101, fracPos - float3(1, 0, 1));
    float g011 = GradientDot(h011, fracPos - float3(0, 1, 1));
    float g111 = GradientDot(h111, fracPos - float3(1, 1, 1));

    // Trilinear interpolation
    float x00 = lerp(g000, g100, f.x);
    float x10 = lerp(g010, g110, f.x);
    float x01 = lerp(g001, g101, f.x);
    float x11 = lerp(g011, g111, f.x);

    float y0 = lerp(x00, x10, f.y);
    float y1 = lerp(x01, x11, f.y);

    return lerp(y0, y1, f.z);
}

float PerlinFBM(float3 pos, uint octaves, float frequency, float persistence, float lacunarity, uint seed) {
    float value = 0.0;
    float amplitude = 1.0;
    float maxAmplitude = 0.0;

    for (uint i = 0; i < octaves; ++i) {
        value += PerlinNoise3D(pos * frequency, seed + i * 73u) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxAmplitude;
}

// ─── Worley (Cellular) Noise ─────────────────────────────────────────────

float WorleyNoise3D(float3 pos, float frequency, uint seed) {
    float3 scaledPos = pos * frequency;
    int3 cellId = int3(floor(scaledPos));
    float3 localPos = scaledPos - float3(cellId);

    float minDist = 1e10;

    // Search 3x3x3 neighborhood
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                int3 neighbor = cellId + int3(x, y, z);
                float3 featurePoint = float3(neighbor) + HashFloat3(uint3(neighbor + 1000), seed);
                float3 diff = featurePoint - scaledPos;
                float dist = dot(diff, diff);
                minDist = min(minDist, dist);
            }
        }
    }

    return sqrt(minDist);
}

float WorleyFBM(float3 pos, uint octaves, float frequency, float persistence, float lacunarity, uint seed) {
    float value = 0.0;
    float amplitude = 1.0;
    float maxAmplitude = 0.0;

    for (uint i = 0; i < octaves; ++i) {
        value += (1.0 - WorleyNoise3D(pos, frequency, seed + i * 137u)) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxAmplitude;
}

// ─── Perlin-Worley blend (Schneider 2015) ────────────────────────────────

float PerlinWorley(float3 pos, float frequency, uint perlinSeed, uint worleySeed) {
    float perlin = PerlinFBM(pos, 4, frequency, 0.5, 2.0, perlinSeed) * 0.5 + 0.5;
    float worley = WorleyFBM(pos, 3, frequency, 0.5, 2.0, worleySeed);

    // Remap: Perlin as base, Worley adds detail
    return saturate(lerp(worley, 1.0, saturate(perlin)));
}

// ─── Base Shape Noise (128³) ─────────────────────────────────────────────

[numthreads(4, 4, 4)]
void CSBaseShape(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid >= cb.baseResolution)) return;

    float3 uvw = float3(DTid) / float3(cb.baseResolution);

    // R: Perlin-Worley (low frequency, main cloud shape)
    float perlinWorley = PerlinWorley(uvw, cb.baseFrequency, cb.perlinSeed, cb.worleySeed);

    // G: Worley x1 (medium frequency billows)
    float worley1 = WorleyFBM(uvw, 3, cb.baseFrequency * 2.0, cb.persistence, cb.lacunarity, cb.worleySeed + 100u);

    // B: Worley x2 (higher frequency detail)
    float worley2 = WorleyFBM(uvw, 3, cb.baseFrequency * 4.0, cb.persistence, cb.lacunarity, cb.worleySeed + 200u);

    // A: Worley x4 (highest frequency micro-detail)
    float worley4 = WorleyFBM(uvw, 3, cb.baseFrequency * 8.0, cb.persistence, cb.lacunarity, cb.worleySeed + 300u);

    g_BaseShapeNoise[DTid] = float4(perlinWorley, worley1, worley2, worley4);
}

// ─── Detail Noise (32³) ─────────────────────────────────────────────────

[numthreads(4, 4, 4)]
void CSDetailNoise(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid >= cb.detailResolution)) return;

    float3 uvw = float3(DTid) / float3(cb.detailResolution);

    float freq = cb.detailFrequency;

    // R-A: Increasing frequency Worley FBM for erosion detail
    float w1 = WorleyFBM(uvw, 3, freq,       cb.persistence, cb.lacunarity, cb.worleySeed + 400u);
    float w2 = WorleyFBM(uvw, 3, freq * 2.0, cb.persistence, cb.lacunarity, cb.worleySeed + 500u);
    float w4 = WorleyFBM(uvw, 3, freq * 4.0, cb.persistence, cb.lacunarity, cb.worleySeed + 600u);
    float w8 = WorleyFBM(uvw, 3, freq * 8.0, cb.persistence, cb.lacunarity, cb.worleySeed + 700u);

    g_DetailNoise[DTid] = float4(w1, w2, w4, w8);
}

// ─── Curl Noise for cloud animation ─────────────────────────────────────
// Optional: generates a 3D curl noise texture for wind distortion

RWTexture3D<float4> g_CurlNoise : register(u2); // 64³ (optional)

[numthreads(4, 4, 4)]
void CSCurlNoise(uint3 DTid : SV_DispatchThreadID) {
    float3 uvw = float3(DTid) / 64.0;
    float eps = 0.01;

    // Compute curl of a potential field (Perlin noise)
    float px0 = PerlinNoise3D((uvw + float3(eps, 0, 0)) * 4.0, cb.perlinSeed + 800u);
    float px1 = PerlinNoise3D((uvw - float3(eps, 0, 0)) * 4.0, cb.perlinSeed + 800u);
    float py0 = PerlinNoise3D((uvw + float3(0, eps, 0)) * 4.0, cb.perlinSeed + 900u);
    float py1 = PerlinNoise3D((uvw - float3(0, eps, 0)) * 4.0, cb.perlinSeed + 900u);
    float pz0 = PerlinNoise3D((uvw + float3(0, 0, eps)) * 4.0, cb.perlinSeed + 1000u);
    float pz1 = PerlinNoise3D((uvw - float3(0, 0, eps)) * 4.0, cb.perlinSeed + 1000u);

    float3 curl;
    curl.x = (py0 - py1) - (pz0 - pz1);
    curl.y = (pz0 - pz1) - (px0 - px1);
    curl.z = (px0 - px1) - (py0 - py1);
    curl /= (2.0 * eps);

    // Normalize and pack to [0,1]
    float len = length(curl);
    if (len > 0.001) curl /= len;
    curl = curl * 0.5 + 0.5;

    g_CurlNoise[DTid] = float4(curl, len);
}
