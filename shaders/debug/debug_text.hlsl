// ─── Debug Text Rendering Shader ──────────────────────────────────────────
// Renders screen-space text quads using a bitmap font atlas.
// Each character is a textured quad with per-vertex color.

struct TextConstants {
    float2 screenSize;
    float2 pad;
};

[[vk::push_constant]] ConstantBuffer<TextConstants> pc;

Texture2D<float>  g_FontAtlas  : register(t0, space21);
SamplerState      g_PointClamp : register(s0, space21);

struct VSInput {
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR;
};

struct VSOutput {
    float4 clipPos  : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR;
};

// ─── Vertex Shader ───────────────────────────────────────────────────────

VSOutput VSMain(VSInput input) {
    VSOutput output;
    // Position is already in NDC [-1,1]
    output.clipPos = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

float4 PSMain(VSOutput input) : SV_Target0 {
    float alpha = g_FontAtlas.Sample(g_PointClamp, input.texcoord);
    if (alpha < 0.5) discard;
    return float4(input.color.rgb, input.color.a * alpha);
}

// ─── Background variant (renders solid color behind text) ────────────────

float4 PSMainBG(VSOutput input) : SV_Target0 {
    // For background quads, ignore atlas and use color directly
    return input.color;
}
