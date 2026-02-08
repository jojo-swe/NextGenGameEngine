#include "engine/rhi/common/rhi_pso_hash.h"
#include <cstring>

namespace nge::rhi {

void PSOHasher::Builder::FeedByte(u8 byte) {
    m_hash ^= static_cast<u64>(byte);
    m_hash *= 0x100000001b3ULL; // FNV-1a prime
}

PSOHasher::Builder& PSOHasher::Builder::Feed(u8 val) { FeedByte(val); return *this; }

PSOHasher::Builder& PSOHasher::Builder::Feed(u16 val) {
    FeedByte(static_cast<u8>(val & 0xFF));
    FeedByte(static_cast<u8>((val >> 8) & 0xFF));
    return *this;
}

PSOHasher::Builder& PSOHasher::Builder::Feed(u32 val) {
    for (int i = 0; i < 4; ++i) FeedByte(static_cast<u8>((val >> (i * 8)) & 0xFF));
    return *this;
}

PSOHasher::Builder& PSOHasher::Builder::Feed(u64 val) {
    for (int i = 0; i < 8; ++i) FeedByte(static_cast<u8>((val >> (i * 8)) & 0xFF));
    return *this;
}

PSOHasher::Builder& PSOHasher::Builder::Feed(f32 val) {
    u32 bits;
    std::memcpy(&bits, &val, sizeof(bits));
    return Feed(bits);
}

PSOHasher::Builder& PSOHasher::Builder::Feed(bool val) {
    FeedByte(val ? 1 : 0);
    return *this;
}

PSOHasher::Builder& PSOHasher::Builder::Feed(const std::string& val) {
    for (char c : val) FeedByte(static_cast<u8>(c));
    FeedByte(0); // Null terminator to separate strings
    return *this;
}

PSOHasher::Builder& PSOHasher::Builder::Feed(const void* data, u32 bytes) {
    const u8* ptr = static_cast<const u8*>(data);
    for (u32 i = 0; i < bytes; ++i) FeedByte(ptr[i]);
    return *this;
}

PSOHash PSOHasher::Builder::Finalize() const {
    return PSOHash{m_hash};
}

PSOHash PSOHasher::Hash(const GraphicsPSODesc& desc) {
    Builder b;
    HashShaders(b, desc);
    HashVertexInput(b, desc);
    HashRasterState(b, desc);
    HashDepthStencilState(b, desc);
    HashBlendState(b, desc);
    HashRenderTargets(b, desc);

    b.Feed(desc.sampleCount);
    b.Feed(desc.sampleShading);
    b.Feed(desc.minSampleShading);
    b.Feed(desc.layoutHandle);

    return b.Finalize();
}

PSOHash PSOHasher::Hash(const ComputePSODesc& desc) {
    Builder b;
    b.Feed(desc.shader);
    b.Feed(desc.entryPoint);
    b.Feed(desc.layoutHandle);
    return b.Finalize();
}

void PSOHasher::HashShaders(Builder& b, const GraphicsPSODesc& desc) {
    b.Feed(desc.vertexShader);
    b.Feed(desc.fragmentShader);
    b.Feed(desc.geometryShader);
    b.Feed(desc.tessControlShader);
    b.Feed(desc.tessEvalShader);
    b.Feed(desc.meshShader);
    b.Feed(desc.taskShader);
}

void PSOHasher::HashVertexInput(Builder& b, const GraphicsPSODesc& desc) {
    b.Feed(static_cast<u32>(desc.vertexAttributes.size()));
    for (const auto& attr : desc.vertexAttributes) {
        b.Feed(attr.location);
        b.Feed(static_cast<u32>(attr.format));
        b.Feed(attr.offset);
        b.Feed(attr.binding);
    }
    b.Feed(static_cast<u32>(desc.vertexBindings.size()));
    for (const auto& binding : desc.vertexBindings) {
        b.Feed(binding.binding);
        b.Feed(binding.stride);
        b.Feed(binding.perInstance);
    }
    b.Feed(static_cast<u8>(desc.topology));
    b.Feed(desc.primitiveRestart);
}

void PSOHasher::HashRasterState(Builder& b, const GraphicsPSODesc& desc) {
    b.Feed(static_cast<u8>(desc.polygonMode));
    b.Feed(static_cast<u8>(desc.cullMode));
    b.Feed(static_cast<u8>(desc.frontFace));
    b.Feed(desc.depthClamp);
    b.Feed(desc.rasterizerDiscard);
    b.Feed(desc.depthBiasConstant);
    b.Feed(desc.depthBiasSlope);
    b.Feed(desc.depthBiasClamp);
    b.Feed(desc.lineWidth);
}

void PSOHasher::HashDepthStencilState(Builder& b, const GraphicsPSODesc& desc) {
    b.Feed(desc.depthTestEnable);
    b.Feed(desc.depthWriteEnable);
    b.Feed(static_cast<u8>(desc.depthCompareFunc));
    b.Feed(desc.stencilTestEnable);

    auto hashStencil = [&b](const StencilState& s) {
        b.Feed(static_cast<u8>(s.failOp));
        b.Feed(static_cast<u8>(s.passOp));
        b.Feed(static_cast<u8>(s.depthFailOp));
        b.Feed(static_cast<u8>(s.compareFunc));
        b.Feed(s.compareMask);
        b.Feed(s.writeMask);
        b.Feed(s.reference);
    };
    hashStencil(desc.stencilFront);
    hashStencil(desc.stencilBack);
}

void PSOHasher::HashBlendState(Builder& b, const GraphicsPSODesc& desc) {
    b.Feed(static_cast<u32>(desc.blendAttachments.size()));
    for (const auto& att : desc.blendAttachments) {
        b.Feed(att.blendEnable);
        b.Feed(static_cast<u8>(att.srcColor));
        b.Feed(static_cast<u8>(att.dstColor));
        b.Feed(static_cast<u8>(att.colorOp));
        b.Feed(static_cast<u8>(att.srcAlpha));
        b.Feed(static_cast<u8>(att.dstAlpha));
        b.Feed(static_cast<u8>(att.alphaOp));
        b.Feed(att.writeMask);
    }
    b.Feed(desc.blendConstants, 4);
}

void PSOHasher::HashRenderTargets(Builder& b, const GraphicsPSODesc& desc) {
    b.Feed(static_cast<u32>(desc.colorFormats.size()));
    for (auto fmt : desc.colorFormats) {
        b.Feed(static_cast<u32>(fmt));
    }
    b.Feed(static_cast<u32>(desc.depthFormat));
}

} // namespace nge::rhi
