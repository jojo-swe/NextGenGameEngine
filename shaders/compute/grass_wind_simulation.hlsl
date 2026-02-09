// ─── Grass Wind Simulation Shader ────────────────────────────────────────
// GPU compute shader for physically-based grass/foliage wind animation.
// Outputs per-instance transform offsets consumed by the grass renderer.
//
// Features:
//   - Procedural wind field (directional + gust + turbulence layers)
//   - Per-blade stiffness and phase variation
//   - Interactive displacement (player/character pushback)
//   - Recovery spring dynamics (bend → return)
//   - LOD-aware: fewer simulation updates at distance
//
// References:
//   - "Rendering Grass in Real-Time" (Jahrmann & Wimmer, I3D 2013)
//   - "Ghost of Tsushima Grass Rendering" (Sucker Punch, SIGGRAPH 2021)
//   - "Horizon Forbidden West Vegetation" (Guerrilla, GDC 2022)

struct GrassInstance {
    float3 rootPosition;    // World-space root
    float  height;          // Blade height
    float  stiffness;       // 0=floppy, 1=rigid (default 0.5)
    float  phase;           // Random phase offset
    float  pad0;
    float  pad1;
};

struct GrassWindOutput {
    float3 tipOffset;       // World-space tip displacement
    float  bendAngle;       // Bend angle in radians
    float2 windUV;          // Wind field sample position (for debug viz)
    float  recoveryFactor;  // Current spring recovery (0=fully bent, 1=upright)
    float  pad0;
};

StructuredBuffer<GrassInstance>   g_Instances : register(t0);
RWStructuredBuffer<GrassWindOutput> g_Output  : register(u0);

struct WindConstants {
    float3 windDirection;       // Primary wind direction (world space)
    float  windSpeed;           // Base wind speed (default 3.0 m/s)
    float3 gustDirection;       // Gust direction offset
    float  gustFrequency;       // Gust oscillation frequency (default 0.5 Hz)
    float3 playerPosition;      // Player world position for interaction
    float  playerRadius;        // Interaction radius (default 1.5m)
    float  playerPushStrength;  // Push force (default 2.0)
    float  time;
    float  deltaTime;
    float  turbulenceScale;     // Noise scale for turbulence (default 0.1)
    float  turbulenceStrength;  // Turbulence intensity (default 0.3)
    float  recoverySpeed;       // Spring recovery rate (default 3.0)
    float  maxBendAngle;        // Max bend in radians (default PI/3)
    uint   instanceCount;
    uint   lodLevel;            // 0=full, 1=half, 2=quarter update rate
    float  pad0;
    float  pad1;
};

[[vk::push_constant]] ConstantBuffer<WindConstants> cb;

// ─── Wind Field Sampling ─────────────────────────────────────────────────

float Hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float ValueNoise(float2 p) {
    float2 ip = floor(p);
    float2 fp = frac(p);
    fp = fp * fp * (3.0 - 2.0 * fp); // Hermite interpolation

    float a = Hash(ip);
    float b = Hash(ip + float2(1, 0));
    float c = Hash(ip + float2(0, 1));
    float d = Hash(ip + float2(1, 1));

    return lerp(lerp(a, b, fp.x), lerp(c, d, fp.x), fp.y);
}

float3 SampleWindField(float3 worldPos, float time) {
    // Base directional wind with spatial variation
    float2 windUV = worldPos.xz * cb.turbulenceScale;
    float spatial = ValueNoise(windUV + time * 0.3);

    float3 baseWind = cb.windDirection * cb.windSpeed;
    baseWind *= (0.7 + 0.3 * spatial); // Spatial variation

    // Gust layer: slower, larger-scale oscillation
    float gustPhase = sin(time * cb.gustFrequency * 6.2831) * 0.5 + 0.5;
    float gustSpatial = ValueNoise(windUV * 0.3 + time * 0.1);
    float3 gust = cb.gustDirection * gustPhase * gustSpatial * cb.windSpeed * 0.5;

    // Turbulence: high-frequency noise
    float turb1 = ValueNoise(windUV * 3.0 + time * 1.5) - 0.5;
    float turb2 = ValueNoise(windUV * 3.0 + float2(17.3, 31.7) + time * 1.7) - 0.5;
    float3 turbulence = float3(turb1, 0, turb2) * cb.turbulenceStrength;

    return baseWind + gust + turbulence;
}

// ─── Player Interaction ──────────────────────────────────────────────────

float3 PlayerPush(float3 rootPos) {
    float3 toGrass = rootPos - cb.playerPosition;
    toGrass.y = 0; // Horizontal only
    float dist = length(toGrass);

    if (dist > cb.playerRadius || dist < 0.001) return float3(0, 0, 0);

    float pushFactor = 1.0 - saturate(dist / cb.playerRadius);
    pushFactor = pushFactor * pushFactor; // Quadratic falloff

    float3 pushDir = normalize(toGrass);
    return pushDir * pushFactor * cb.playerPushStrength;
}

// ─── Spring Dynamics ─────────────────────────────────────────────────────

float SpringRecovery(float currentBend, float targetBend, float stiffness, float dt) {
    // Critically damped spring toward target
    float springForce = (targetBend - currentBend) * cb.recoverySpeed * stiffness;
    return currentBend + springForce * dt;
}

// ─── Main Compute ────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint idx = DTid.x;
    if (idx >= cb.instanceCount) return;

    // LOD-based update skip
    if (cb.lodLevel > 0) {
        uint updateMask = (1u << cb.lodLevel) - 1u;
        uint frameSlot = uint(cb.time * 60.0) & updateMask;
        if ((idx & updateMask) != frameSlot) return;
    }

    GrassInstance inst = g_Instances[idx];

    // Sample wind at blade root
    float phaseTime = cb.time + inst.phase * 6.2831;
    float3 wind = SampleWindField(inst.rootPosition, phaseTime);

    // Player interaction
    float3 push = PlayerPush(inst.rootPosition);
    wind += push;

    // Compute bend direction and magnitude
    float windMagnitude = length(wind);
    float3 windDir = windMagnitude > 0.001 ? wind / windMagnitude : float3(0, 0, 0);

    // Target bend angle based on wind force and stiffness
    float targetBend = saturate(windMagnitude / (cb.windSpeed * 2.0)) * cb.maxBendAngle;
    targetBend *= (1.0 - inst.stiffness * 0.7); // Stiffer blades bend less

    // Read previous state for spring recovery
    GrassWindOutput prev = g_Output[idx];
    float currentBend = prev.bendAngle;

    // Spring dynamics
    float newBend = SpringRecovery(currentBend, targetBend, inst.stiffness, cb.deltaTime);
    newBend = clamp(newBend, 0, cb.maxBendAngle);

    // Compute tip offset from bend
    // Bend is an arc: tip moves forward and down
    float sinBend = sin(newBend);
    float cosBend = cos(newBend);

    float3 tipOffset = windDir * sinBend * inst.height;
    tipOffset.y = -(1.0 - cosBend) * inst.height; // Droop

    // Add micro-flutter for visual interest
    float flutter = sin(phaseTime * 8.0 + inst.phase * 100.0) * 0.02 * (1.0 - inst.stiffness);
    tipOffset += float3(flutter, 0, flutter * 0.7);

    // Recovery factor (for shader: controls color desaturation when bent)
    float recovery = 1.0 - saturate(newBend / cb.maxBendAngle);

    // Write output
    GrassWindOutput output;
    output.tipOffset = tipOffset;
    output.bendAngle = newBend;
    output.windUV = inst.rootPosition.xz * cb.turbulenceScale;
    output.recoveryFactor = recovery;
    output.pad0 = 0;

    g_Output[idx] = output;
}
