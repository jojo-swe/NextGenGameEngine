#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/pipeline/mesh_registry.h"
#include "engine/renderer/materials/material_system.h"

namespace nge::scene {

struct MeshRenderer {
    renderer::MeshId meshId = renderer::INVALID_MESH_ID;
    u32 submeshIndex = 0;
    renderer::MaterialId materialId = 0;
    bool visible = true;
    bool castShadow = true;
    bool receiveShadow = true;
};

} // namespace nge::scene
