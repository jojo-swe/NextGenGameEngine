#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/entity.h"
#include "engine/core/ecs/world.h"
#include "engine/core/math/math_types.h"
#include "engine/core/math/pga.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace nge::scene {

// ─── Scene Serialization ─────────────────────────────────────────────────
// Saves/loads entity hierarchy and component data to/from a binary format.
// Supports versioned format for forward compatibility.
//
// Format:
//   [Header]    - magic, version, entity count
//   [Entities]  - per-entity: id, parent, name, component mask
//   [Components] - per-component type: serialized data blocks

// ─── Scene File Header ───────────────────────────────────────────────────

struct SceneFileHeader {
    static constexpr u32 MAGIC = 0x4E474553; // "NGES"
    static constexpr u32 VERSION = 1;

    u32 magic       = MAGIC;
    u32 version     = VERSION;
    u32 entityCount = 0;
    u32 componentTypeCount = 0;
    u64 dataSize    = 0; // Total payload size after header
};

// ─── Component Serializer Registration ───────────────────────────────────
// Each component type registers serialize/deserialize callbacks.

using ComponentTypeId = u32;

struct ComponentSerializer {
    std::string name;
    ComponentTypeId typeId;

    // Serialize: entity → byte buffer
    std::function<bool(ecs::Entity, std::vector<u8>&)> serialize;

    // Deserialize: byte buffer → entity
    std::function<bool(ecs::Entity, const u8*, usize)> deserialize;

    // Size hint (0 = variable)
    usize fixedSize = 0;
};

// ─── Serialized Entity ───────────────────────────────────────────────────

struct SerializedEntity {
    u64         id;
    u64         parentId;       // 0 = root
    std::string name;
    u64         componentMask;  // Bitfield of which components are present

    // Transform (always present)
    math::Vec3  position;
    math::Vec4  rotation;       // Quaternion
    math::Vec3  scale;
};

// ─── Scene Serializer ────────────────────────────────────────────────────

class SceneSerializer {
public:
    SceneSerializer() = default;

    // Register component serializers
    void RegisterComponent(const ComponentSerializer& serializer);

    // Serialize scene to file
    bool SaveToFile(const std::string& path, ecs::World& world);

    // Deserialize scene from file
    bool LoadFromFile(const std::string& path, ecs::World& world);

    // Serialize to memory buffer
    bool SaveToBuffer(std::vector<u8>& outBuffer, ecs::World& world);

    // Deserialize from memory buffer
    bool LoadFromBuffer(const u8* data, usize size, ecs::World& world);

    // Clear all registered serializers
    void ClearRegistrations() { m_serializers.clear(); }

    // Binary write helpers (public for serialization extensions and testing)
    static void WriteU32(std::vector<u8>& buf, u32 val);
    static void WriteU64(std::vector<u8>& buf, u64 val);
    static void WriteF32(std::vector<u8>& buf, f32 val);
    static void WriteString(std::vector<u8>& buf, const std::string& str);
    static void WriteVec3(std::vector<u8>& buf, const math::Vec3& v);
    static void WriteVec4(std::vector<u8>& buf, const math::Vec4& v);
    static void WriteBytes(std::vector<u8>& buf, const void* data, usize size);

    // Binary read helpers
    struct Reader {
        const u8* data;
        usize size;
        usize pos = 0;

        bool HasBytes(usize count) const { return pos + count <= size; }
        u32 ReadU32();
        u64 ReadU64();
        f32 ReadF32();
        std::string ReadString();
        math::Vec3 ReadVec3();
        math::Vec4 ReadVec4();
        bool ReadBytes(void* out, usize count);
    };

    std::vector<ComponentSerializer> m_serializers;
    std::unordered_map<std::string, ComponentTypeId> m_nameToType;
};

// ─── Built-in Component Serializers ──────────────────────────────────────
// Pre-register these for common engine components.

namespace serializers {

ComponentSerializer TransformSerializer();
ComponentSerializer CameraSerializer();
ComponentSerializer LightSerializer();
ComponentSerializer MeshRendererSerializer();
ComponentSerializer RigidBodySerializer();
ComponentSerializer ScriptSerializer();

} // namespace serializers

} // namespace nge::scene
