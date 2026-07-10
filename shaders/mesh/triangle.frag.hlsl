// ─── Lit Cube Fragment Shader ─────────────────────────────────────────────

struct PSInput {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    float3 worldPos : WORLD_POS;
};

float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normal);

    // Directional light (sun) coming from upper-left-front
    float3 L = normalize(float3(-0.5, 1.0, 0.3));
    float NdotL = max(dot(N, L), 0.0);

    // Ambient + diffuse
    float3 ambient = input.color * 0.25;
    float3 diffuse = input.color * NdotL * 0.75;

    // Simple rim lighting for visual interest
    float3 V = normalize(float3(0.0, 2.0, 5.0) - input.worldPos);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.3;

    float3 finalColor = ambient + diffuse + float3(rim, rim, rim);
    return float4(finalColor, 1.0);
}
