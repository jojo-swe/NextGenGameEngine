#include "engine/scene/serialization/scene_serializer.h"
#include "engine/scene/transform/transform.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <fstream>
#include <cstring>

namespace nge::scene {

// ─── Binary Write Helpers ────────────────────────────────────────────────

void SceneSerializer::WriteU32(std::vector<u8>& buf, u32 val) {
    buf.insert(buf.end(), reinterpret_cast<const u8*>(&val),
               reinterpret_cast<const u8*>(&val) + sizeof(u32));
}

void SceneSerializer::WriteU64(std::vector<u8>& buf, u64 val) {
    buf.insert(buf.end(), reinterpret_cast<const u8*>(&val),
               reinterpret_cast<const u8*>(&val) + sizeof(u64));
}

void SceneSerializer::WriteF32(std::vector<u8>& buf, f32 val) {
    buf.insert(buf.end(), reinterpret_cast<const u8*>(&val),
               reinterpret_cast<const u8*>(&val) + sizeof(f32));
}

void SceneSerializer::WriteString(std::vector<u8>& buf, const std::string& str) {
    WriteU32(buf, static_cast<u32>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

void SceneSerializer::WriteVec3(std::vector<u8>& buf, const math::Vec3& v) {
    WriteF32(buf, v.x); WriteF32(buf, v.y); WriteF32(buf, v.z);
}

void SceneSerializer::WriteVec4(std::vector<u8>& buf, const math::Vec4& v) {
    WriteF32(buf, v.x); WriteF32(buf, v.y); WriteF32(buf, v.z); WriteF32(buf, v.w);
}

void SceneSerializer::WriteBytes(std::vector<u8>& buf, const void* data, usize size) {
    const u8* p = static_cast<const u8*>(data);
    buf.insert(buf.end(), p, p + size);
}

// ─── Binary Read Helpers ─────────────────────────────────────────────────

u32 SceneSerializer::Reader::ReadU32() {
    u32 val = 0;
    if (HasBytes(sizeof(u32))) {
        std::memcpy(&val, data + pos, sizeof(u32));
        pos += sizeof(u32);
    }
    return val;
}

u64 SceneSerializer::Reader::ReadU64() {
    u64 val = 0;
    if (HasBytes(sizeof(u64))) {
        std::memcpy(&val, data + pos, sizeof(u64));
        pos += sizeof(u64);
    }
    return val;
}

f32 SceneSerializer::Reader::ReadF32() {
    f32 val = 0;
    if (HasBytes(sizeof(f32))) {
        std::memcpy(&val, data + pos, sizeof(f32));
        pos += sizeof(f32);
    }
    return val;
}

std::string SceneSerializer::Reader::ReadString() {
    u32 len = ReadU32();
    if (!HasBytes(len)) return {};
    std::string str(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return str;
}

math::Vec3 SceneSerializer::Reader::ReadVec3() {
    return {ReadF32(), ReadF32(), ReadF32()};
}

math::Vec4 SceneSerializer::Reader::ReadVec4() {
    return {ReadF32(), ReadF32(), ReadF32(), ReadF32()};
}

bool SceneSerializer::Reader::ReadBytes(void* out, usize count) {
    if (!HasBytes(count)) return false;
    std::memcpy(out, data + pos, count);
    pos += count;
    return true;
}

// ─── Registration ────────────────────────────────────────────────────────

void SceneSerializer::RegisterComponent(const ComponentSerializer& serializer) {
    m_nameToType[serializer.name] = serializer.typeId;
    m_serializers.push_back(serializer);
    NGE_LOG_DEBUG("Registered component serializer: '{}' (type {})",
                  serializer.name, serializer.typeId);
}

// ─── Save ────────────────────────────────────────────────────────────────

bool SceneSerializer::SaveToFile(const std::string& path, ecs::World& world) {
    std::vector<u8> buffer;
    if (!SaveToBuffer(buffer, world)) return false;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Failed to open file for writing: {}", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    NGE_LOG_INFO("Scene saved to '{}' ({} bytes)", path, buffer.size());
    return true;
}

bool SceneSerializer::SaveToBuffer(std::vector<u8>& outBuffer, ecs::World& world) {
    outBuffer.clear();

    // Collect all entities
    auto entities = world.GetAllEntities();
    u32 entityCount = static_cast<u32>(entities.size());

    // Reserve header space (will be filled in later)
    usize headerPos = outBuffer.size();
    outBuffer.resize(headerPos + sizeof(SceneFileHeader));

    // Write entities
    WriteU32(outBuffer, entityCount);

    for (auto entity : entities) {
        // Entity ID
        WriteU64(outBuffer, entity.id);

        // Parent (0 = root)
        u64 parentId = 0;
        // TODO: query parent from scene hierarchy
        WriteU64(outBuffer, parentId);

        // Name
        std::string name = world.GetEntityName(entity);
        WriteString(outBuffer, name);

        // Transform (always serialized)
        auto* transform = world.GetComponent<scene::Transform>(entity);
        if (transform) {
            WriteVec3(outBuffer, transform->GetLocalPosition());
            WriteVec4(outBuffer, {0, 0, 0, 1}); // PGA motor doesn't store quat directly
            WriteVec3(outBuffer, {1, 1, 1});     // PGA motor doesn't store scale directly
        } else {
            WriteVec3(outBuffer, {0, 0, 0});
            WriteVec4(outBuffer, {0, 0, 0, 1});
            WriteVec3(outBuffer, {1, 1, 1});
        }

        // Component data
        u32 componentCount = 0;
        usize componentCountPos = outBuffer.size();
        WriteU32(outBuffer, 0); // Placeholder

        for (const auto& serializer : m_serializers) {
            std::vector<u8> componentData;
            if (serializer.serialize && serializer.serialize(entity, componentData)) {
                WriteU32(outBuffer, serializer.typeId);
                WriteU32(outBuffer, static_cast<u32>(componentData.size()));
                WriteBytes(outBuffer, componentData.data(), componentData.size());
                componentCount++;
            }
        }

        // Patch component count
        std::memcpy(outBuffer.data() + componentCountPos, &componentCount, sizeof(u32));
    }

    // Patch header
    SceneFileHeader header;
    header.entityCount = entityCount;
    header.componentTypeCount = static_cast<u32>(m_serializers.size());
    header.dataSize = outBuffer.size() - headerPos - sizeof(SceneFileHeader);
    std::memcpy(outBuffer.data() + headerPos, &header, sizeof(SceneFileHeader));

    return true;
}

// ─── Load ────────────────────────────────────────────────────────────────

bool SceneSerializer::LoadFromFile(const std::string& path, ecs::World& world) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Failed to open file for reading: {}", path);
        return false;
    }

    auto fileSize = file.tellg();
    file.seekg(0);

    std::vector<u8> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    bool result = LoadFromBuffer(buffer.data(), buffer.size(), world);
    if (result) {
        NGE_LOG_INFO("Scene loaded from '{}' ({} bytes)", path, fileSize);
    }
    return result;
}

bool SceneSerializer::LoadFromBuffer(const u8* data, usize size, ecs::World& world) {
    Reader reader{data, size, 0};

    // Read header
    SceneFileHeader header;
    if (!reader.ReadBytes(&header, sizeof(SceneFileHeader))) {
        NGE_LOG_ERROR("Scene file too small for header");
        return false;
    }

    if (header.magic != SceneFileHeader::MAGIC) {
        NGE_LOG_ERROR("Invalid scene file magic: 0x{:08X}", header.magic);
        return false;
    }

    if (header.version != SceneFileHeader::VERSION) {
        NGE_LOG_WARN("Scene file version mismatch: {} (expected {})",
                     header.version, SceneFileHeader::VERSION);
        // Could attempt migration for older versions
    }

    u32 entityCount = reader.ReadU32();

    // ID remapping (old ID → new entity)
    std::unordered_map<u64, ecs::Entity> idMap;

    for (u32 i = 0; i < entityCount; ++i) {
        u64 oldId = reader.ReadU64();
        u64 parentId = reader.ReadU64();
        std::string name = reader.ReadString();

        // Create entity
        ecs::Entity entity = world.CreateEntity();
        world.SetEntityName(entity, name);
        idMap[oldId] = entity;

        // Transform
        math::Vec3 position = reader.ReadVec3();
        math::Vec4 rotation = reader.ReadVec4();
        math::Vec3 scale = reader.ReadVec3();
        (void)rotation; (void)scale; // PGA motors don't use quat/scale directly
        auto& t = world.AddComponent<scene::Transform>(entity);
        t.SetPosition(position);

        // Parent reparenting (deferred — parent may not exist yet)
        (void)parentId;

        // Components
        u32 componentCount = reader.ReadU32();
        for (u32 c = 0; c < componentCount; ++c) {
            u32 typeId = reader.ReadU32();
            u32 dataSize = reader.ReadU32();

            if (!reader.HasBytes(dataSize)) {
                NGE_LOG_ERROR("Scene file truncated at entity {} component {}", i, c);
                return false;
            }

            // Find serializer for this type
            bool handled = false;
            for (const auto& serializer : m_serializers) {
                if (serializer.typeId == typeId && serializer.deserialize) {
                    serializer.deserialize(entity, reader.data + reader.pos, dataSize);
                    handled = true;
                    break;
                }
            }

            if (!handled) {
                NGE_LOG_WARN("Unknown component type {} in scene file, skipping {} bytes",
                             typeId, dataSize);
            }

            reader.pos += dataSize;
        }
    }

    // Second pass: reparent entities
    // TODO: iterate entities and set parents using idMap

    return true;
}

// ─── Built-in Component Serializers ──────────────────────────────────────

namespace serializers {

ComponentSerializer TransformSerializer() {
    ComponentSerializer s;
    s.name = "Transform";
    s.typeId = 1;
    s.fixedSize = sizeof(f32) * 10; // pos(3) + rot(4) + scale(3)
    // Transform is handled specially in the main serialize loop
    s.serialize = nullptr;
    s.deserialize = nullptr;
    return s;
}

ComponentSerializer CameraSerializer() {
    ComponentSerializer s;
    s.name = "Camera";
    s.typeId = 2;
    s.serialize = [](ecs::Entity /*entity*/, std::vector<u8>& buf) -> bool {
        // TODO: Get camera component and serialize fov, near, far, aspect
        (void)buf;
        return false; // Not present
    };
    s.deserialize = [](ecs::Entity /*entity*/, const u8* /*data*/, usize /*size*/) -> bool {
        // TODO: Deserialize and attach camera component
        return true;
    };
    return s;
}

ComponentSerializer LightSerializer() {
    ComponentSerializer s;
    s.name = "Light";
    s.typeId = 3;
    s.serialize = [](ecs::Entity /*entity*/, std::vector<u8>& /*buf*/) -> bool {
        return false;
    };
    s.deserialize = [](ecs::Entity /*entity*/, const u8* /*data*/, usize /*size*/) -> bool {
        return true;
    };
    return s;
}

ComponentSerializer MeshRendererSerializer() {
    ComponentSerializer s;
    s.name = "MeshRenderer";
    s.typeId = 4;
    s.serialize = [](ecs::Entity /*entity*/, std::vector<u8>& /*buf*/) -> bool {
        return false;
    };
    s.deserialize = [](ecs::Entity /*entity*/, const u8* /*data*/, usize /*size*/) -> bool {
        return true;
    };
    return s;
}

ComponentSerializer RigidBodySerializer() {
    ComponentSerializer s;
    s.name = "RigidBody";
    s.typeId = 5;
    s.serialize = [](ecs::Entity /*entity*/, std::vector<u8>& /*buf*/) -> bool {
        return false;
    };
    s.deserialize = [](ecs::Entity /*entity*/, const u8* /*data*/, usize /*size*/) -> bool {
        return true;
    };
    return s;
}

ComponentSerializer ScriptSerializer() {
    ComponentSerializer s;
    s.name = "Script";
    s.typeId = 6;
    s.serialize = [](ecs::Entity /*entity*/, std::vector<u8>& /*buf*/) -> bool {
        return false;
    };
    s.deserialize = [](ecs::Entity /*entity*/, const u8* /*data*/, usize /*size*/) -> bool {
        return true;
    };
    return s;
}

} // namespace serializers

} // namespace nge::scene
