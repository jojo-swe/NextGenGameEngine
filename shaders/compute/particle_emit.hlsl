// ─── GPU Particle Emission Compute Shader ────────────────────────────────
// Spawns new particles from the dead list into the alive list.
// Each thread handles one particle spawn with randomized initial state.

#include "../common/math.hlsl"

struct EmitConstants {
    float3 emitterPos;
    float  pad0;
    float3 emitterDir;
    float  shapeRadius;
    float  shapeAngle;       // Cone half-angle (radians)
    float  speedMin;
    float  speedMax;
    float  lifetimeMin;
    float  lifetimeMax;
    float  sizeMin;
    float  sizeMax;
    float  rotMin;
    float  rotMax;
    float  angVelMin;
    float  angVelMax;
    uint   emitCount;
    uint   maxParticles;
    uint   shapeType;        // 0=point, 1=sphere, 2=hemisphere, 3=cone, 4=box
    uint   randomSeed;
    uint   pad1;
};

[[vk::push_constant]] ConstantBuffer<EmitConstants> pc;

struct Particle {
    float3 position;
    float  age;
    float3 velocity;
    float  lifetime;
    float4 color;
    float  size;
    float  rotation;
    float  angularVelocity;
    uint   alive;
};

RWStructuredBuffer<Particle> g_Particles : register(u0, space17);
RWByteAddressBuffer          g_DeadList  : register(u1, space17); // Stack of free indices
RWByteAddressBuffer          g_AliveList : register(u2, space17);
RWByteAddressBuffer          g_Counters  : register(u3, space17); // [0]=alive, [1]=dead, [2]=emit

// ─── Random Number Generation (PCG hash) ─────────────────────────────────

uint PCGHash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(uint seed) {
    return float(PCGHash(seed)) / 4294967295.0;
}

float RandomRange(uint seed, float minVal, float maxVal) {
    return minVal + RandomFloat(seed) * (maxVal - minVal);
}

float3 RandomDirection(uint seed) {
    float z = RandomFloat(seed) * 2.0 - 1.0;
    float phi = RandomFloat(seed + 1) * 2.0 * PI;
    float r = sqrt(1.0 - z * z);
    return float3(r * cos(phi), r * sin(phi), z);
}

float3 RandomInSphere(uint seed) {
    float3 dir = RandomDirection(seed);
    float r = pow(RandomFloat(seed + 3), 1.0 / 3.0);
    return dir * r;
}

float3 RandomOnCone(uint seed, float3 axis, float halfAngle) {
    // Generate random direction within a cone around axis
    float cosAngle = cos(halfAngle);
    float z = RandomRange(seed, cosAngle, 1.0);
    float phi = RandomFloat(seed + 1) * 2.0 * PI;
    float sinTheta = sqrt(1.0 - z * z);

    float3 local = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Rotate local to align with axis
    float3 up = abs(axis.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, axis));
    float3 forward = cross(axis, right);

    return right * local.x + forward * local.y + axis * local.z;
}

// ─── Main Emission Kernel ────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.emitCount) return;

    // Pop from dead list (atomic decrement dead count)
    uint deadCount;
    g_Counters.InterlockedAdd(4, -1, deadCount); // [1] = dead count
    if (deadCount <= 0) {
        g_Counters.InterlockedAdd(4, 1); // Restore if underflow
        return;
    }

    uint particleIdx = g_DeadList.Load((deadCount - 1) * 4);
    if (particleIdx >= pc.maxParticles) return;

    // Generate unique seed per particle
    uint seed = PCGHash(DTid.x + pc.randomSeed * 1000003u);

    // Compute spawn position based on emitter shape
    float3 spawnPos = pc.emitterPos;
    float3 spawnDir = pc.emitterDir;

    if (pc.shapeType == 1) {
        // Sphere
        spawnPos += RandomInSphere(seed + 10) * pc.shapeRadius;
        spawnDir = normalize(spawnPos - pc.emitterPos);
    } else if (pc.shapeType == 2) {
        // Hemisphere
        float3 offset = RandomInSphere(seed + 10) * pc.shapeRadius;
        if (dot(offset, pc.emitterDir) < 0) offset = -offset;
        spawnPos += offset;
        spawnDir = normalize(offset);
    } else if (pc.shapeType == 3) {
        // Cone
        spawnDir = RandomOnCone(seed + 10, pc.emitterDir, pc.shapeAngle);
    } else if (pc.shapeType == 4) {
        // Box (TODO: use shapeSize)
        spawnPos += float3(
            RandomRange(seed + 10, -pc.shapeRadius, pc.shapeRadius),
            RandomRange(seed + 11, -pc.shapeRadius, pc.shapeRadius),
            RandomRange(seed + 12, -pc.shapeRadius, pc.shapeRadius));
    }

    // Initialize particle
    Particle p;
    p.position = spawnPos;
    p.age = 0;
    p.velocity = spawnDir * RandomRange(seed + 20, pc.speedMin, pc.speedMax);
    p.lifetime = RandomRange(seed + 30, pc.lifetimeMin, pc.lifetimeMax);
    p.color = float4(1, 1, 1, 1); // Will be modulated by color-over-life
    p.size = RandomRange(seed + 40, pc.sizeMin, pc.sizeMax);
    p.rotation = RandomRange(seed + 50, pc.rotMin, pc.rotMax);
    p.angularVelocity = RandomRange(seed + 60, pc.angVelMin, pc.angVelMax);
    p.alive = 1;

    g_Particles[particleIdx] = p;

    // Push to alive list (atomic increment alive count)
    uint aliveIdx;
    g_Counters.InterlockedAdd(0, 1, aliveIdx);
    g_AliveList.Store(aliveIdx * 4, particleIdx);
}
