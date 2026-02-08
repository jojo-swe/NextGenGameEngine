#include "engine/rhi/common/rhi_command_signature.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

CommandSignatureBuilder& CommandSignatureBuilder::SetName(const std::string& name) {
    m_desc.name = name;
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddDraw() {
    m_desc.arguments.push_back({IndirectArgType::Draw, 0, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddDrawIndexed() {
    m_desc.arguments.push_back({IndirectArgType::DrawIndexed, 0, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddDispatch() {
    m_desc.arguments.push_back({IndirectArgType::Dispatch, 0, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddDrawMeshTasks() {
    m_desc.arguments.push_back({IndirectArgType::DrawMeshTasks, 0, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddVertexBufferView() {
    m_desc.arguments.push_back({IndirectArgType::VertexBufferView, 0, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddIndexBufferView() {
    m_desc.arguments.push_back({IndirectArgType::IndexBufferView, 0, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddConstant(u32 rootParam, u32 count) {
    m_desc.arguments.push_back({IndirectArgType::Constant, rootParam, count});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddConstantBufferView(u32 rootParam) {
    m_desc.arguments.push_back({IndirectArgType::ConstantBufferView, rootParam, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddShaderResourceView(u32 rootParam) {
    m_desc.arguments.push_back({IndirectArgType::ShaderResourceView, rootParam, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::AddUnorderedAccessView(u32 rootParam) {
    m_desc.arguments.push_back({IndirectArgType::UnorderedAccessView, rootParam, 0});
    return *this;
}

CommandSignatureBuilder& CommandSignatureBuilder::SetByteStride(u32 stride) {
    m_desc.byteStride = stride;
    return *this;
}

CommandSignatureDesc CommandSignatureBuilder::Build() const {
    auto desc = m_desc;
    if (desc.byteStride == 0) {
        desc.byteStride = GetByteStride();
    }
    return desc;
}

u32 CommandSignatureBuilder::GetByteStride() const {
    u32 stride = 0;
    for (const auto& arg : m_desc.arguments) {
        stride += GetArgSize(arg);
    }
    return stride;
}

u32 CommandSignatureBuilder::GetArgSize(const IndirectArgDesc& arg) {
    switch (arg.type) {
        case IndirectArgType::Draw:              return 16; // 4 × u32
        case IndirectArgType::DrawIndexed:       return 20; // 5 × u32
        case IndirectArgType::Dispatch:          return 12; // 3 × u32
        case IndirectArgType::DrawMeshTasks:     return 12; // 3 × u32
        case IndirectArgType::VertexBufferView:  return 16; // address(8) + size(4) + stride(4)
        case IndirectArgType::IndexBufferView:   return 16; // address(8) + size(4) + format(4)
        case IndirectArgType::Constant:          return arg.constantCount * 4;
        case IndirectArgType::ConstantBufferView:return 8;  // GPU address
        case IndirectArgType::ShaderResourceView:return 8;
        case IndirectArgType::UnorderedAccessView:return 8;
    }
    return 0;
}

CommandSignatureDesc CommandSignatureBuilder::DrawIndirect() {
    return CommandSignatureBuilder()
        .SetName("DrawIndirect")
        .AddDraw()
        .Build();
}

CommandSignatureDesc CommandSignatureBuilder::DrawIndexedIndirect() {
    return CommandSignatureBuilder()
        .SetName("DrawIndexedIndirect")
        .AddDrawIndexed()
        .Build();
}

CommandSignatureDesc CommandSignatureBuilder::DispatchIndirect() {
    return CommandSignatureBuilder()
        .SetName("DispatchIndirect")
        .AddDispatch()
        .Build();
}

CommandSignatureDesc CommandSignatureBuilder::DrawMeshTasksIndirect() {
    return CommandSignatureBuilder()
        .SetName("DrawMeshTasksIndirect")
        .AddDrawMeshTasks()
        .Build();
}

bool CommandSignatureBuilder::Validate(const CommandSignatureDesc& desc) {
    if (desc.arguments.empty()) {
        NGE_LOG_ERROR("Command signature '{}': no arguments", desc.name);
        return false;
    }

    // Must end with a draw/dispatch command
    auto lastType = desc.arguments.back().type;
    bool endsWithCommand = (lastType == IndirectArgType::Draw ||
                            lastType == IndirectArgType::DrawIndexed ||
                            lastType == IndirectArgType::Dispatch ||
                            lastType == IndirectArgType::DrawMeshTasks);
    if (!endsWithCommand) {
        NGE_LOG_ERROR("Command signature '{}': must end with draw/dispatch", desc.name);
        return false;
    }

    // Stride must be >= sum of arg sizes
    u32 minStride = 0;
    for (const auto& arg : desc.arguments) {
        minStride += GetArgSize(arg);
    }
    if (desc.byteStride < minStride) {
        NGE_LOG_ERROR("Command signature '{}': stride {} < minimum {}",
                      desc.name, desc.byteStride, minStride);
        return false;
    }

    return true;
}

} // namespace nge::rhi
