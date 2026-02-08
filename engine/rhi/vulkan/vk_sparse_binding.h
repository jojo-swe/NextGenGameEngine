#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Sparse Binding Manager ──────────────────────────────────────────
// Manages Vulkan sparse residency for virtual textures. Maps virtual
// texture pages to physical memory tiles on demand.
//
// Sparse images allow individual mip tails and tiles to be bound/unbound
// from physical memory independently, enabling:
//   - Virtual texture streaming (only resident pages consume VRAM)
//   - Partial mip residency (fine-grained mip level control)
//   - Memory aliasing for tiles with same content

struct SparseTile {
    u32 x, y, z;         // Tile coordinates within the mip level
    u32 mipLevel;
    u32 arrayLayer;
};

struct SparseTileHash {
    size_t operator()(const SparseTile& t) const {
        size_t h = std::hash<u32>{}(t.x);
        h ^= std::hash<u32>{}(t.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(t.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(t.mipLevel) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(t.arrayLayer) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

inline bool operator==(const SparseTile& a, const SparseTile& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z &&
           a.mipLevel == b.mipLevel && a.arrayLayer == b.arrayLayer;
}

struct PhysicalPage {
    u64  memoryHandle = 0;     // VkDeviceMemory
    u64  offset = 0;           // Offset within memory allocation
    u64  size = 0;
    bool bound = false;
};

struct SparseImageInfo {
    TextureHandle texture;
    u32           tileWidth = 0;    // Tile size in texels (e.g., 64 or 128)
    u32           tileHeight = 0;
    u32           tileDepth = 1;
    u32           tilesX = 0;       // Number of tiles in X
    u32           tilesY = 0;
    u32           mipLevels = 0;
    u32           arrayLayers = 1;
    u64           tileSizeBytes = 0; // Bytes per tile
    u64           mipTailOffset = 0;
    u32           mipTailFirstLod = 0;
};

struct SparseBindingConfig {
    u64 physicalPagePoolSize = 256 * 1024 * 1024; // 256 MB physical page pool
    u32 maxSparseImages = 64;
    u32 maxPendingBinds = 512;
};

struct SparseBindingStats {
    u32 totalTiles;
    u32 residentTiles;
    u32 pendingBinds;
    u32 pendingUnbinds;
    u64 physicalMemoryUsed;
    u64 physicalMemoryBudget;
    f32 residencyPercent;
};

class SparseBindingManager {
public:
    bool Init(IDevice* device, const SparseBindingConfig& config = {});
    void Shutdown();

    // Register a sparse image for management
    u32 RegisterSparseImage(TextureHandle texture, const SparseImageInfo& info);

    // Bind a tile to physical memory (makes it resident)
    bool BindTile(u32 imageIndex, const SparseTile& tile);

    // Unbind a tile (makes it non-resident, frees physical page)
    bool UnbindTile(u32 imageIndex, const SparseTile& tile);

    // Bind the mip tail (all mips below the sparse threshold)
    bool BindMipTail(u32 imageIndex);

    // Submit pending bind/unbind operations to the sparse binding queue
    void Flush();

    // Check if a tile is resident
    bool IsTileResident(u32 imageIndex, const SparseTile& tile) const;

    // Query
    const SparseImageInfo& GetImageInfo(u32 imageIndex) const;
    SparseBindingStats GetStats() const;

private:
    struct ImageState {
        SparseImageInfo info;
        std::unordered_map<SparseTile, PhysicalPage, SparseTileHash> boundTiles;
        bool mipTailBound = false;
    };

    struct PendingBind {
        u32        imageIndex;
        SparseTile tile;
        bool       bind; // true = bind, false = unbind
    };

    PhysicalPage AllocatePhysicalPage(u64 size);
    void FreePhysicalPage(const PhysicalPage& page);

    IDevice* m_device = nullptr;
    SparseBindingConfig m_config;

    std::vector<ImageState> m_images;
    std::vector<PendingBind> m_pendingBinds;

    // Physical page pool
    u64 m_poolHandle = 0;        // VkDeviceMemory for the page pool
    u64 m_poolCapacity = 0;
    u64 m_poolUsed = 0;
    std::vector<u64> m_freePages; // Free page offsets

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
