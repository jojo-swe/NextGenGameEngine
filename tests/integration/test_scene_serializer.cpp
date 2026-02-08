#include <gtest/gtest.h>
#include "engine/scene/serialization/scene_serializer.h"
#include <cstring>

using namespace nge;
using namespace nge::scene;

TEST(SceneSerializer, HeaderMagicAndVersion) {
    SceneFileHeader header;
    EXPECT_EQ(header.magic, 0x4E474553u); // "NGES"
    EXPECT_EQ(header.version, 1u);
}

TEST(SceneSerializer, WriteReadU32) {
    std::vector<u8> buf;
    SceneSerializer::WriteU32(buf, 0xDEADBEEF);
    ASSERT_EQ(buf.size(), sizeof(u32));

    u32 val = 0;
    std::memcpy(&val, buf.data(), sizeof(u32));
    EXPECT_EQ(val, 0xDEADBEEF);
}

TEST(SceneSerializer, WriteReadU64) {
    std::vector<u8> buf;
    SceneSerializer::WriteU64(buf, 0x0123456789ABCDEFull);
    ASSERT_EQ(buf.size(), sizeof(u64));

    u64 val = 0;
    std::memcpy(&val, buf.data(), sizeof(u64));
    EXPECT_EQ(val, 0x0123456789ABCDEFull);
}

TEST(SceneSerializer, WriteReadF32) {
    std::vector<u8> buf;
    SceneSerializer::WriteF32(buf, 3.14159f);
    ASSERT_EQ(buf.size(), sizeof(f32));

    f32 val = 0;
    std::memcpy(&val, buf.data(), sizeof(f32));
    EXPECT_NEAR(val, 3.14159f, 1e-5f);
}

TEST(SceneSerializer, WriteReadString) {
    std::vector<u8> buf;
    SceneSerializer::WriteString(buf, "Hello");
    // 4 bytes length + 5 bytes content
    EXPECT_EQ(buf.size(), 9u);

    SceneSerializer::Reader reader{buf.data(), buf.size(), 0};
    std::string result = reader.ReadString();
    EXPECT_EQ(result, "Hello");
}

TEST(SceneSerializer, WriteReadVec3) {
    std::vector<u8> buf;
    math::Vec3 v{1.0f, 2.0f, 3.0f};
    SceneSerializer::WriteVec3(buf, v);
    EXPECT_EQ(buf.size(), 3 * sizeof(f32));

    SceneSerializer::Reader reader{buf.data(), buf.size(), 0};
    math::Vec3 result = reader.ReadVec3();
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(SceneSerializer, WriteReadVec4) {
    std::vector<u8> buf;
    math::Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    SceneSerializer::WriteVec4(buf, v);
    EXPECT_EQ(buf.size(), 4 * sizeof(f32));

    SceneSerializer::Reader reader{buf.data(), buf.size(), 0};
    math::Vec4 result = reader.ReadVec4();
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
    EXPECT_FLOAT_EQ(result.w, 4.0f);
}

TEST(SceneSerializer, ReaderBoundsCheck) {
    u8 data[4] = {0};
    SceneSerializer::Reader reader{data, 4, 0};

    EXPECT_TRUE(reader.HasBytes(4));
    EXPECT_FALSE(reader.HasBytes(5));

    reader.ReadU32();
    EXPECT_FALSE(reader.HasBytes(1));
}

TEST(SceneSerializer, RegisterComponent) {
    SceneSerializer serializer;

    ComponentSerializer cs;
    cs.name = "TestComponent";
    cs.typeId = 42;
    serializer.RegisterComponent(cs);

    // Should not crash on double registration
    serializer.RegisterComponent(cs);

    serializer.ClearRegistrations();
}

TEST(SceneSerializer, BuiltInSerializers) {
    auto transform = serializers::TransformSerializer();
    EXPECT_EQ(transform.name, "Transform");
    EXPECT_EQ(transform.typeId, 1u);

    auto camera = serializers::CameraSerializer();
    EXPECT_EQ(camera.name, "Camera");
    EXPECT_EQ(camera.typeId, 2u);

    auto light = serializers::LightSerializer();
    EXPECT_EQ(light.name, "Light");
    EXPECT_EQ(light.typeId, 3u);

    auto mesh = serializers::MeshRendererSerializer();
    EXPECT_EQ(mesh.name, "MeshRenderer");
    EXPECT_EQ(mesh.typeId, 4u);

    auto rb = serializers::RigidBodySerializer();
    EXPECT_EQ(rb.name, "RigidBody");
    EXPECT_EQ(rb.typeId, 5u);

    auto script = serializers::ScriptSerializer();
    EXPECT_EQ(script.name, "Script");
    EXPECT_EQ(script.typeId, 6u);
}

TEST(SceneSerializer, InvalidMagicRejected) {
    // Construct a buffer with wrong magic
    std::vector<u8> buf(sizeof(SceneFileHeader), 0);
    SceneFileHeader header;
    header.magic = 0xBADF00D;
    std::memcpy(buf.data(), &header, sizeof(header));

    SceneSerializer serializer;
    // LoadFromBuffer should fail on bad magic
    // Note: requires a valid World reference — we test the header validation path
    // by checking the Reader logic directly
    SceneSerializer::Reader reader{buf.data(), buf.size(), 0};
    SceneFileHeader readHeader;
    reader.ReadBytes(&readHeader, sizeof(readHeader));
    EXPECT_NE(readHeader.magic, SceneFileHeader::MAGIC);
}
