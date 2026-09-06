#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import Kairo.Assets;

using namespace kairo::assets;

namespace
{
    void AppendU16(std::vector<std::byte>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xffu));
        bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    }

    void AppendF32(std::vector<std::byte>& bytes, float value)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (std::size_t byte = 0u; byte < 4u; ++byte)
            bytes.push_back(static_cast<std::byte>((bits >> (byte * 8u)) & 0xffu));
    }

    void AppendIdentity(std::vector<std::byte>& bytes)
    {
        for (std::size_t index = 0u; index < 16u; ++index)
            AppendF32(bytes, index % 5u == 0u ? 1.0f : 0.0f);
    }
}

TEST_CASE("glTF scene importer preserves skinning and TRS animation")
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-gltf-animation-" + GenerateAssetID().ToString());
    std::filesystem::create_directories(root);
    const auto gltfPath = root / "scene.gltf";
    const auto binPath = root / "scene.bin";

    std::vector<std::byte> binary;
    // POSITION: three FLOAT VEC3 vertices, offset 0, length 36.
    for (const float value : {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f }) AppendF32(binary, value);
    // JOINTS_0: three UNSIGNED_SHORT VEC4 vertices, offset 36, length 24.
    for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        for (std::size_t slot = 0u; slot < 4u; ++slot) AppendU16(binary, 0u);
    // WEIGHTS_0: three FLOAT VEC4 vertices, offset 60, length 48.
    for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        for (const float weight : { 1.0f, 0.0f, 0.0f, 0.0f }) AppendF32(binary, weight);
    // Triangle indices, offset 108, length 6, then two bytes of alignment padding.
    AppendU16(binary, 0u); AppendU16(binary, 1u); AppendU16(binary, 2u);
    AppendU16(binary, 0u);
    // One inverse bind matrix, offset 116, length 64.
    AppendIdentity(binary);
    // Animation input times, offset 180, length 8.
    AppendF32(binary, 0.0f); AppendF32(binary, 1.0f);
    // Translation output, offset 188, length 24.
    for (const float value : { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }) AppendF32(binary, value);
    REQUIRE(binary.size() == 212u);

    {
        std::ofstream output(binPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(binary.data()),
            static_cast<std::streamsize>(binary.size()));
    }

    const std::string source = R"json({
  "asset": { "version": "2.0" },
  "buffers": [{ "uri": "scene.bin", "byteLength": 212 }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 108, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 116, "byteLength": 64 },
    { "buffer": 0, "byteOffset": 180, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 188, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 4, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3" }
  ],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
      "indices": 3
    }]
  }],
  "nodes": [
    { "name": "Joint" },
    { "name": "Character", "mesh": 0, "skin": 0 }
  ],
  "skins": [{
    "name": "Rig",
    "inverseBindMatrices": 4,
    "skeleton": 0,
    "joints": [0]
  }],
  "animations": [{
    "name": "Move",
    "samplers": [{ "input": 5, "output": 6, "interpolation": "LINEAR" }],
    "channels": [{ "sampler": 0, "target": { "node": 1, "path": "translation" } }]
  }],
  "scenes": [{ "nodes": [0, 1] }],
  "scene": 0
})json";
    {
        std::ofstream output(gltfPath, std::ios::binary);
        output << source;
    }
    std::vector<std::byte> sourceBytes(source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
        sourceBytes[index] = static_cast<std::byte>(static_cast<unsigned char>(source[index]));

    GltfSceneImporter importer;
    CHECK(importer.Version() == "2");
    const auto artifact = importer.Import({ {}, AssetType::Scene, sourceBytes, gltfPath });
    REQUIRE(artifact.Format == "kairo.gltf-scene.v2");
    const auto scene = ParseGltfSceneDerivedArtifact(artifact);
    REQUIRE(scene.Primitives.size() == 1u);
    REQUIRE(scene.Primitives[0].Skinning.size() == 3u);
    CHECK(scene.Primitives[0].Skinning[0].Joints[0] == 0u);
    CHECK(scene.Primitives[0].Skinning[0].Weights[0] == 1.0f);
    REQUIRE(scene.Skins.size() == 1u);
    CHECK(scene.Skins[0].Name == "Rig");
    CHECK(scene.Skins[0].Joints == std::vector<std::uint32_t>{ 0u });
    REQUIRE(scene.Animations.size() == 1u);
    CHECK(scene.Animations[0].Name == "Move");
    REQUIRE(scene.Animations[0].Channels.size() == 1u);
    CHECK(scene.Animations[0].Channels[0].TargetNode == 1u);
    CHECK(scene.Animations[0].Channels[0].Path == GltfAnimationPath::Translation);
    REQUIRE(scene.Animations[0].Channels[0].Keyframes.size() == 2u);
    CHECK(scene.Animations[0].Channels[0].Keyframes[1].Value[0] == 1.0f);
    CHECK(scene.Animations[0].DurationSeconds() == 1.0f);

    std::filesystem::remove_all(root);
}