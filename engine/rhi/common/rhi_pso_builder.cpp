#include "engine/rhi/common/rhi_pso_builder.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// ─── GraphicsPSOBuilder ──────────────────────────────────────────────────

GraphicsPSOBuilder& GraphicsPSOBuilder::SetVertexShader(const std::string& path) { m_desc.vertexShader = path; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetFragmentShader(const std::string& path) { m_desc.fragmentShader = path; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetMeshShader(const std::string& path) { m_desc.meshShader = path; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetTaskShader(const std::string& path) { m_desc.taskShader = path; return *this; }

GraphicsPSOBuilder& GraphicsPSOBuilder::AddVertexAttribute(u32 location, Format format, u32 offset, u32 binding) {
    m_desc.vertexAttributes.push_back({location, format, offset, binding});
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::AddVertexBinding(u32 binding, u32 stride, bool perInstance) {
    m_desc.vertexBindings.push_back({binding, stride, perInstance});
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::SetTopology(PrimitiveTopology topology) { m_desc.topology = topology; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetCullMode(CullMode mode) { m_desc.cullMode = mode; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetFrontFace(FrontFace face) { m_desc.frontFace = face; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetPolygonMode(PolygonMode mode) { m_desc.polygonMode = mode; return *this; }

GraphicsPSOBuilder& GraphicsPSOBuilder::SetDepthBias(f32 constant, f32 slope, f32 clamp) {
    m_desc.depthBiasConstant = constant;
    m_desc.depthBiasSlope = slope;
    m_desc.depthBiasClamp = clamp;
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::EnableDepthTest(bool enable) { m_desc.depthTestEnable = enable; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::EnableDepthWrite(bool enable) { m_desc.depthWriteEnable = enable; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetDepthCompare(CompareFunc func) { m_desc.depthCompareFunc = func; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::EnableStencilTest(bool enable) { m_desc.stencilTestEnable = enable; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetStencilFront(const StencilState& state) { m_desc.stencilFront = state; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetStencilBack(const StencilState& state) { m_desc.stencilBack = state; return *this; }

GraphicsPSOBuilder& GraphicsPSOBuilder::SetSampleCount(u32 count) { m_desc.sampleCount = count; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::EnableSampleShading(f32 minFraction) {
    m_desc.sampleShading = true;
    m_desc.minSampleShading = minFraction;
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::AddBlendAttachment(const BlendAttachment& attachment) {
    m_desc.blendAttachments.push_back(attachment);
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::AddAlphaBlendAttachment() {
    BlendAttachment att;
    att.blendEnable = true;
    att.srcColor = BlendFactor::SrcAlpha;
    att.dstColor = BlendFactor::OneMinusSrcAlpha;
    att.colorOp = BlendOp::Add;
    att.srcAlpha = BlendFactor::One;
    att.dstAlpha = BlendFactor::OneMinusSrcAlpha;
    att.alphaOp = BlendOp::Add;
    m_desc.blendAttachments.push_back(att);
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::AddOpaqueAttachment() {
    BlendAttachment att;
    att.blendEnable = false;
    att.writeMask = 0xF;
    m_desc.blendAttachments.push_back(att);
    return *this;
}

GraphicsPSOBuilder& GraphicsPSOBuilder::AddColorFormat(Format format) { m_desc.colorFormats.push_back(format); return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetDepthFormat(Format format) { m_desc.depthFormat = format; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetLayout(u64 layoutHandle) { m_desc.layoutHandle = layoutHandle; return *this; }
GraphicsPSOBuilder& GraphicsPSOBuilder::SetName(const std::string& name) { m_desc.debugName = name; return *this; }

PSOValidationResult GraphicsPSOBuilder::Validate() const {
    PSOValidationResult result;

    // Must have either vertex+fragment or mesh shader
    bool hasVS = !m_desc.vertexShader.empty();
    bool hasFS = !m_desc.fragmentShader.empty();
    bool hasMS = !m_desc.meshShader.empty();

    if (!hasVS && !hasMS) {
        result.errors.push_back("Must specify either vertex shader or mesh shader");
        result.valid = false;
    }

    if (hasVS && hasMS) {
        result.errors.push_back("Cannot use both vertex shader and mesh shader");
        result.valid = false;
    }

    if (hasMS && !m_desc.vertexAttributes.empty()) {
        result.warnings.push_back("Vertex attributes ignored when using mesh shader");
    }

    if (hasVS && !hasFS && !m_desc.rasterizerDiscard) {
        result.errors.push_back("Fragment shader required unless rasterizer discard is enabled");
        result.valid = false;
    }

    // Validate vertex attributes reference valid bindings
    for (const auto& attr : m_desc.vertexAttributes) {
        bool bindingFound = false;
        for (const auto& binding : m_desc.vertexBindings) {
            if (binding.binding == attr.binding) { bindingFound = true; break; }
        }
        if (!bindingFound) {
            result.errors.push_back("Vertex attribute at location " + std::to_string(attr.location) +
                " references non-existent binding " + std::to_string(attr.binding));
            result.valid = false;
        }
    }

    // Color format count should match blend attachment count
    if (!m_desc.colorFormats.empty() && !m_desc.blendAttachments.empty() &&
        m_desc.colorFormats.size() != m_desc.blendAttachments.size()) {
        result.errors.push_back("Color format count (" + std::to_string(m_desc.colorFormats.size()) +
            ") does not match blend attachment count (" + std::to_string(m_desc.blendAttachments.size()) + ")");
        result.valid = false;
    }

    // Depth format required if depth test enabled
    if (m_desc.depthTestEnable && m_desc.depthFormat == Format::Undefined) {
        result.warnings.push_back("Depth test enabled but no depth format specified");
    }

    // Sample count must be power of 2
    if (m_desc.sampleCount != 1 && m_desc.sampleCount != 2 &&
        m_desc.sampleCount != 4 && m_desc.sampleCount != 8 &&
        m_desc.sampleCount != 16 && m_desc.sampleCount != 32 &&
        m_desc.sampleCount != 64) {
        result.errors.push_back("Sample count must be a power of 2, got " + std::to_string(m_desc.sampleCount));
        result.valid = false;
    }

    if (m_desc.layoutHandle == 0) {
        result.warnings.push_back("No pipeline layout specified");
    }

    return result;
}

GraphicsPSODesc GraphicsPSOBuilder::Build() const {
    auto validation = Validate();
    if (!validation.valid) {
        for (const auto& err : validation.errors) {
            NGE_LOG_ERROR("PSO '{}': {}", m_desc.debugName, err);
        }
    }
    for (const auto& warn : validation.warnings) {
        NGE_LOG_WARN("PSO '{}': {}", m_desc.debugName, warn);
    }
    return m_desc;
}

GraphicsPSOBuilder GraphicsPSOBuilder::Opaque() {
    GraphicsPSOBuilder b;
    b.SetCullMode(CullMode::Back)
     .SetFrontFace(FrontFace::CounterClockwise)
     .EnableDepthTest(true)
     .EnableDepthWrite(true)
     .SetDepthCompare(CompareFunc::Less)
     .SetName("Opaque");
    return b;
}

GraphicsPSOBuilder GraphicsPSOBuilder::Transparent() {
    GraphicsPSOBuilder b;
    b.SetCullMode(CullMode::None)
     .EnableDepthTest(true)
     .EnableDepthWrite(false)
     .SetDepthCompare(CompareFunc::Less)
     .SetName("Transparent");
    return b;
}

GraphicsPSOBuilder GraphicsPSOBuilder::ShadowMap() {
    GraphicsPSOBuilder b;
    b.SetCullMode(CullMode::Front) // Front-face culling for shadow maps
     .EnableDepthTest(true)
     .EnableDepthWrite(true)
     .SetDepthCompare(CompareFunc::Less)
     .SetDepthBias(1.25f, 1.75f, 0.0f)
     .SetName("ShadowMap");
    return b;
}

GraphicsPSOBuilder GraphicsPSOBuilder::Wireframe() {
    GraphicsPSOBuilder b;
    b.SetPolygonMode(PolygonMode::Line)
     .SetCullMode(CullMode::None)
     .EnableDepthTest(true)
     .EnableDepthWrite(false)
     .SetName("Wireframe");
    return b;
}

GraphicsPSOBuilder GraphicsPSOBuilder::FullscreenTriangle() {
    GraphicsPSOBuilder b;
    b.SetCullMode(CullMode::None)
     .EnableDepthTest(false)
     .EnableDepthWrite(false)
     .SetTopology(PrimitiveTopology::TriangleList)
     .SetName("FullscreenTriangle");
    return b;
}

// ─── ComputePSOBuilder ───────────────────────────────────────────────────

ComputePSOBuilder& ComputePSOBuilder::SetShader(const std::string& path) { m_desc.shader = path; return *this; }
ComputePSOBuilder& ComputePSOBuilder::SetEntryPoint(const std::string& entry) { m_desc.entryPoint = entry; return *this; }
ComputePSOBuilder& ComputePSOBuilder::SetLayout(u64 layoutHandle) { m_desc.layoutHandle = layoutHandle; return *this; }
ComputePSOBuilder& ComputePSOBuilder::SetName(const std::string& name) { m_desc.debugName = name; return *this; }

PSOValidationResult ComputePSOBuilder::Validate() const {
    PSOValidationResult result;
    if (m_desc.shader.empty()) {
        result.errors.push_back("Compute shader path is empty");
        result.valid = false;
    }
    if (m_desc.entryPoint.empty()) {
        result.errors.push_back("Compute entry point is empty");
        result.valid = false;
    }
    if (m_desc.layoutHandle == 0) {
        result.warnings.push_back("No pipeline layout specified");
    }
    return result;
}

ComputePSODesc ComputePSOBuilder::Build() const {
    auto validation = Validate();
    if (!validation.valid) {
        for (const auto& err : validation.errors) {
            NGE_LOG_ERROR("Compute PSO '{}': {}", m_desc.debugName, err);
        }
    }
    return m_desc;
}

} // namespace nge::rhi
