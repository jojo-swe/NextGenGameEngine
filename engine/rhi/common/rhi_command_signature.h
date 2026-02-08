#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>

namespace nge::rhi {

// ─── GPU Command Signature Builder ───────────────────────────────────────
// Defines the layout of indirect command buffers for ExecuteIndirect-style
// GPU-driven rendering. Each signature specifies the sequence of arguments
// the GPU reads when processing indirect commands.
//
// Vulkan equivalent: implicit from the indirect draw/dispatch call type.
// D3D12 equivalent: ID3D12CommandSignature with argument descs.
//
// This abstraction allows the engine to validate and describe indirect
// command layouts regardless of the backend.

enum class IndirectArgType : u8 {
    Draw,                   // DrawIndirect (vertexCount, instanceCount, firstVertex, firstInstance)
    DrawIndexed,            // DrawIndexedIndirect (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
    Dispatch,               // DispatchIndirect (groupCountX, groupCountY, groupCountZ)
    DrawMeshTasks,          // DrawMeshTasksIndirect (groupCountX, groupCountY, groupCountZ)
    VertexBufferView,       // Bind vertex buffer (bufferAddress, sizeInBytes, strideInBytes)
    IndexBufferView,        // Bind index buffer (bufferAddress, sizeInBytes, format)
    Constant,               // Inline root/push constant (u32 values)
    ConstantBufferView,     // Bind constant buffer (bufferAddress)
    ShaderResourceView,     // Bind SRV (bufferAddress)
    UnorderedAccessView,    // Bind UAV (bufferAddress)
};

struct IndirectArgDesc {
    IndirectArgType type;
    u32             rootParameterIndex = 0;  // For constant/CBV/SRV/UAV args
    u32             constantCount = 0;       // For Constant type: number of u32s
};

struct CommandSignatureDesc {
    std::string                name;
    std::vector<IndirectArgDesc> arguments;
    u32                        byteStride = 0;  // 0 = auto-calculate
};

class CommandSignatureBuilder {
public:
    CommandSignatureBuilder& SetName(const std::string& name);

    // Add arguments in order
    CommandSignatureBuilder& AddDraw();
    CommandSignatureBuilder& AddDrawIndexed();
    CommandSignatureBuilder& AddDispatch();
    CommandSignatureBuilder& AddDrawMeshTasks();
    CommandSignatureBuilder& AddVertexBufferView();
    CommandSignatureBuilder& AddIndexBufferView();
    CommandSignatureBuilder& AddConstant(u32 rootParam, u32 count);
    CommandSignatureBuilder& AddConstantBufferView(u32 rootParam);
    CommandSignatureBuilder& AddShaderResourceView(u32 rootParam);
    CommandSignatureBuilder& AddUnorderedAccessView(u32 rootParam);

    // Override byte stride (default = sum of argument sizes)
    CommandSignatureBuilder& SetByteStride(u32 stride);

    // Build the descriptor
    CommandSignatureDesc Build() const;

    // Get the calculated byte stride
    u32 GetByteStride() const;

    // Presets
    static CommandSignatureDesc DrawIndirect();
    static CommandSignatureDesc DrawIndexedIndirect();
    static CommandSignatureDesc DispatchIndirect();
    static CommandSignatureDesc DrawMeshTasksIndirect();

    // Validation
    static bool Validate(const CommandSignatureDesc& desc);

private:
    static u32 GetArgSize(const IndirectArgDesc& arg);

    CommandSignatureDesc m_desc;
};

} // namespace nge::rhi
