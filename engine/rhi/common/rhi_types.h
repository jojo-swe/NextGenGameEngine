#pragma once

#include "engine/core/types.h"
#include <cstdint>

namespace nge::rhi {

// ─── Resource Handles ────────────────────────────────────────────────────
// Typed handles to GPU resources. Index into backend arrays.
template <typename Tag>
struct Handle {
    union {
        u32 index;
        u32 id;
    };
    Handle() : index(UINT32_MAX) {}
    constexpr u32 GetId() const { return index; }
    constexpr bool IsValid() const { return index != UINT32_MAX; }
    constexpr bool operator==(Handle o) const { return index == o.index; }
    constexpr bool operator!=(Handle o) const { return index != o.index; }
};

struct BufferTag {};
struct TextureTag {};
struct SamplerTag {};
struct PipelineTag {};
struct ShaderTag {};
struct RenderPassTag {};
struct DescriptorSetTag {};
struct AccelStructTag {};

using BufferHandle       = Handle<BufferTag>;
using TextureHandle      = Handle<TextureTag>;
using SamplerHandle      = Handle<SamplerTag>;
using PipelineHandle     = Handle<PipelineTag>;
using ShaderHandle       = Handle<ShaderTag>;
using RenderPassHandle   = Handle<RenderPassTag>;
using DescriptorSetHandle = Handle<DescriptorSetTag>;
using AccelStructHandle  = Handle<AccelStructTag>;

// ─── Enums ───────────────────────────────────────────────────────────────

enum class GraphicsAPI : u8 {
    Vulkan,
    DirectX12
};

enum class QueueType : u8 {
    Graphics,
    Compute,
    Transfer,
    Count
};

enum class Format : u32 {
    Unknown = 0,

    // 8-bit
    R8_UNORM, R8_SNORM, R8_UINT, R8_SINT,
    RG8_UNORM, RG8_SNORM, RG8_UINT, RG8_SINT,
    RGBA8_UNORM, RGBA8_SNORM, RGBA8_UINT, RGBA8_SINT,
    RGBA8_SRGB,
    BGRA8_UNORM, BGRA8_SRGB,

    // 16-bit
    R16_FLOAT, R16_UINT, R16_SINT, R16_UNORM,
    RG16_FLOAT, RG16_UINT,
    RGBA16_FLOAT, RGBA16_UINT,

    // 32-bit
    R32_FLOAT, R32_UINT, R32_SINT,
    RG32_FLOAT, RG32_UINT,
    RGB32_FLOAT,
    RGBA32_FLOAT, RGBA32_UINT,

    // Depth/stencil
    D16_UNORM,
    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,

    // Compressed
    BC1_UNORM, BC1_SRGB,
    BC3_UNORM, BC3_SRGB,
    BC4_UNORM, BC4_SNORM,
    BC5_UNORM, BC5_SNORM,
    BC6H_UFLOAT, BC6H_SFLOAT,
    BC7_UNORM, BC7_SRGB,

    Count
};

enum class TextureType : u8 {
    Tex1D,
    Tex2D,
    Tex3D,
    TexCube,
    Tex2DArray,
    TexCubeArray
};

enum class TextureUsage : u32 {
    None              = 0,
    ShaderRead        = 1 << 0,
    ShaderWrite       = 1 << 1,
    RenderTarget      = 1 << 2,
    DepthStencil      = 1 << 3,
    TransferSrc       = 1 << 4,
    TransferDst       = 1 << 5,
    InputAttachment   = 1 << 6,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline bool operator&(TextureUsage a, TextureUsage b) {
    return (static_cast<u32>(a) & static_cast<u32>(b)) != 0;
}

enum class BufferUsage : u32 {
    None            = 0,
    Vertex          = 1 << 0,
    Index           = 1 << 1,
    Uniform         = 1 << 2,
    Storage         = 1 << 3,
    Indirect        = 1 << 4,
    TransferSrc     = 1 << 5,
    TransferDst     = 1 << 6,
    AccelStructInput   = 1 << 7,
    AccelStructStorage = 1 << 8,
    ShaderBindingTable = 1 << 9,
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline bool operator&(BufferUsage a, BufferUsage b) {
    return (static_cast<u32>(a) & static_cast<u32>(b)) != 0;
}

enum class MemoryUsage : u8 {
    GPU_Only,       // Device-local, fastest for GPU. Use staging for upload.
    CPU_To_GPU,     // Host-visible, coherent. Good for uniforms, streaming.
    GPU_To_CPU,     // For readback.
};

enum class ShaderStage : u32 {
    Vertex         = 1 << 0,
    Fragment       = 1 << 1,
    Compute        = 1 << 2,
    Geometry       = 1 << 3,
    TessControl    = 1 << 4,
    TessEval       = 1 << 5,
    Mesh           = 1 << 6,
    Amplification  = 1 << 7,  // Task shader in Vulkan
    RayGeneration  = 1 << 8,
    RayMiss        = 1 << 9,
    RayClosestHit  = 1 << 10,
    RayAnyHit      = 1 << 11,
    RayIntersection = 1 << 12,
    Callable       = 1 << 13,
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<u32>(a) | static_cast<u32>(b));
}

enum class LoadOp : u8 {
    Load,
    Clear,
    DontCare,
};

enum class StoreOp : u8 {
    Store,
    DontCare,
};

enum class CompareOp : u8 {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class CullMode : u8 {
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class FrontFace : u8 {
    CounterClockwise,
    Clockwise,
};

enum class PrimitiveTopology : u8 {
    TriangleList,
    TriangleStrip,
    TriangleFan,
    LineList,
    LineStrip,
    PointList,
};

enum class BlendFactor : u8 {
    Zero, One,
    SrcColor, OneMinusSrcColor,
    DstColor, OneMinusDstColor,
    SrcAlpha, OneMinusSrcAlpha,
    DstAlpha, OneMinusDstAlpha,
};

enum class BlendOp : u8 {
    Add, Subtract, ReverseSubtract, Min, Max,
};

enum class FilterMode : u8 {
    Nearest,
    Linear,
};

enum class AddressMode : u8 {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

enum class IndexType : u8 {
    UInt16,
    UInt32,
};

enum class ResourceState : u32 {
    Undefined = 0,
    Common,
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    ShaderRead,
    ShaderWrite,
    RenderTarget,
    DepthWrite,
    DepthStencilWrite,
    DepthStencilRead,
    TransferSrc,
    TransferDst,
    Present,
    AccelStructRead,
    AccelStructWrite,
    AccelStructBuildInput,
    IndirectArgument,
};

// ─── Descriptors ─────────────────────────────────────────────────────────

struct ClearValue {
    union {
        f32 color[4];
        struct { f32 depth; u32 stencil; } depthStencil;
    };

    static ClearValue Color(f32 r, f32 g, f32 b, f32 a = 1.0f) {
        ClearValue v; v.color[0]=r; v.color[1]=g; v.color[2]=b; v.color[3]=a;
        return v;
    }
    static ClearValue DepthStencil(f32 depth = 1.0f, u32 stencil = 0) {
        ClearValue v; v.depthStencil = {depth, stencil};
        return v;
    }
};

struct Viewport {
    f32 x = 0, y = 0;
    f32 width = 0, height = 0;
    f32 minDepth = 0.0f, maxDepth = 1.0f;
};

struct Scissor {
    i32 x = 0, y = 0;
    u32 width = 0, height = 0;
};

// ─── Creation Descriptors ────────────────────────────────────────────────

struct BufferDesc {
    usize       size        = 0;
    BufferUsage usage       = BufferUsage::None;
    MemoryUsage memoryUsage = MemoryUsage::GPU_Only;
    const char* debugName   = nullptr;
};

struct TextureDesc {
    u32          width      = 1;
    u32          height     = 1;
    u32          depth      = 1;
    u32          mipLevels  = 1;
    u32          arrayLayers = 1;
    Format       format     = Format::RGBA8_UNORM;
    TextureType  type       = TextureType::Tex2D;
    TextureUsage usage      = TextureUsage::ShaderRead;
    MemoryUsage  memoryUsage = MemoryUsage::GPU_Only;
    const char*  debugName  = nullptr;
};

struct SamplerDesc {
    FilterMode  minFilter   = FilterMode::Linear;
    FilterMode  magFilter   = FilterMode::Linear;
    FilterMode  mipFilter   = FilterMode::Linear;
    AddressMode addressU    = AddressMode::Repeat;
    AddressMode addressV    = AddressMode::Repeat;
    AddressMode addressW    = AddressMode::Repeat;
    f32         mipLodBias  = 0.0f;
    f32         maxAnisotropy = 16.0f;
    bool        enableAnisotropy = true;
    CompareOp   compareOp   = CompareOp::Never;
    bool        enableCompare = false;
};

struct ShaderDesc {
    const byte* bytecode     = nullptr;
    usize       bytecodeSize = 0;
    ShaderStage stage        = ShaderStage::Vertex;
    const char* entryPoint   = "main";
    const char* debugName    = nullptr;
};

// ─── Graphics Pipeline Descriptor ────────────────────────────────────────

struct VertexAttribute {
    u32    location = 0;
    u32    binding  = 0;
    Format format   = Format::RGB32_FLOAT;
    u32    offset   = 0;
};

struct VertexBinding {
    u32   binding   = 0;
    u32   stride    = 0;
    bool  perInstance = false;
};

struct BlendAttachment {
    bool        enable      = false;
    BlendFactor srcColor    = BlendFactor::SrcAlpha;
    BlendFactor dstColor    = BlendFactor::OneMinusSrcAlpha;
    BlendOp     colorOp     = BlendOp::Add;
    BlendFactor srcAlpha    = BlendFactor::One;
    BlendFactor dstAlpha    = BlendFactor::Zero;
    BlendOp     alphaOp     = BlendOp::Add;
};

struct RenderTargetDesc {
    Format  format  = Format::RGBA8_UNORM;
    LoadOp  loadOp  = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
};

struct DepthStencilDesc {
    Format    format      = Format::D32_FLOAT;
    bool      depthTest   = true;
    bool      depthWrite  = true;
    CompareOp depthCompare = CompareOp::Less;
    LoadOp    loadOp      = LoadOp::Clear;
    StoreOp   storeOp     = StoreOp::Store;
};

struct GraphicsPipelineDesc {
    ShaderHandle          vertexShader;
    ShaderHandle          fragmentShader;
    ShaderHandle          meshShader;         // If using mesh shader pipeline
    ShaderHandle          amplificationShader; // Task shader

    VertexBinding         vertexBindings[8] = {};
    u32                   vertexBindingCount = 0;
    VertexAttribute       vertexAttributes[16] = {};
    u32                   vertexAttributeCount = 0;

    PrimitiveTopology     topology  = PrimitiveTopology::TriangleList;
    CullMode              cullMode  = CullMode::Back;
    FrontFace             frontFace = FrontFace::CounterClockwise;
    bool                  wireframe = false;

    BlendAttachment       blendAttachments[8] = {};
    u32                   blendAttachmentCount = 0;

    RenderTargetDesc      renderTargets[8] = {};
    u32                   renderTargetCount = 0;
    DepthStencilDesc      depthStencil;
    bool                  hasDepthStencil = true;

    bool                  isMeshShaderPipeline = false;

    const char*           debugName = nullptr;
};

struct ComputePipelineDesc {
    ShaderHandle computeShader;
    const char*  debugName = nullptr;
};

struct RayTracingPipelineDesc {
    ShaderHandle rayGenShader;
    ShaderHandle missShaders[4];
    u32          missShaderCount = 0;
    ShaderHandle hitGroups[4];   // Closest hit
    u32          hitGroupCount = 0;
    u32          maxRecursionDepth = 2;
    const char*  debugName = nullptr;
};

// ─── Acceleration Structure ──────────────────────────────────────────────

struct AccelStructGeometryDesc {
    BufferHandle vertexBuffer;
    u32          vertexCount    = 0;
    u32          vertexStride   = 0;
    Format       vertexFormat   = Format::RGB32_FLOAT;
    BufferHandle indexBuffer;
    u32          indexCount     = 0;
    IndexType    indexType      = IndexType::UInt32;
    bool         opaque         = true;
};

enum class AccelStructType : u8 {
    BottomLevel, // BLAS
    TopLevel,    // TLAS
};

struct AccelStructDesc {
    AccelStructType          type = AccelStructType::BottomLevel;
    AccelStructGeometryDesc  geometries[16];
    u32                      geometryCount = 0;
    const char*              debugName = nullptr;
};

// ─── Device Capabilities ─────────────────────────────────────────────────

struct DeviceCapabilities {
    bool meshShaders         = false;
    bool rayTracing          = false;
    bool descriptorIndexing  = false;
    bool dynamicRendering    = false;
    bool variableRateShading = false;
    bool int16               = false;
    bool float16             = false;
    bool int64               = false;
    u32  maxBindlessTextures = 0;
    u32  maxBindlessBuffers  = 0;
    u64  maxBufferSize       = 0;
    u32  maxTextureDimension2D = 0;
    u32  maxComputeWorkGroupSize[3] = {};
    u32  maxMeshShaderOutputVertices = 0;
    u32  maxMeshShaderOutputPrimitives = 0;
};

// ─── Feature Tier (per Phase 0.1) ────────────────────────────────────────
enum class FeatureTier : u8 {
    Tier0_Baseline = 0,  // Vulkan 1.3 / DX12 raster baseline
    Tier1_GPUDriven,     // Mesh shaders + descriptor indexing
    Tier2_RayTracing,    // Hardware ray tracing, hybrid GI
    Tier3_Neural,        // Tensor/ML acceleration
};

} // namespace nge::rhi
