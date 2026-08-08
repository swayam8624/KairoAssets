#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets.AssetID;
import Kairo.Assets.BuiltinImporters;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.GltfImporter;
import Kairo.Assets.GltfSceneArtifact;
import Kairo.Assets.ImportDatabase;
import Kairo.Assets.Importer;
import Kairo.Assets.ImporterRegistry;
import Kairo.Assets.MaterialArtifact;
import Kairo.Assets.Metadata;
import Kairo.Assets.TextureArtifact;
import Kairo.Assets.TextureImporter;
import Kairo.Assets.Types;

namespace
{
    [[nodiscard]] std::span<const std::byte> Bytes(const std::string& text)
    {
        return std::as_bytes(std::span(text.data(), text.size()));
    }

    [[nodiscard]] std::vector<std::byte> TinyTga()
    {
        const std::array<std::uint8_t, 34u> bytes{
            0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
            2u, 0u, 2u, 0u, 32u, 0x28u,
            0u, 0u, 255u, 255u,
            0u, 255u, 0u, 255u,
            255u, 0u, 0u, 255u,
            255u, 255u, 255u, 255u
        };
        std::vector<std::byte> result;
        result.reserve(bytes.size());
        for (const std::uint8_t value : bytes) result.push_back(std::byte{ value });
        return result;
    }

    [[nodiscard]] kairo::assets::AssetID FixedAssetID(std::uint8_t seed)
    {
        kairo::assets::AssetID::Storage bytes{};
        for (std::size_t index = 0u; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(seed + index);
        return kairo::assets::AssetID{ bytes };
    }
}

TEST_CASE("Texture artifact generates canonical colour-aware mip chains")
{
    using namespace kairo::assets;
    std::array<std::byte, 16u> pixels{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}
    };
    const TextureArtifactData texture =
        BuildTextureMipChainRGBA8(2u, 2u, pixels, TextureColorSpace::SRGB, false);
    REQUIRE(texture.Mips.size() == 2u);
    CHECK(texture.Mips[0].Width == 2u);
    CHECK(texture.Mips[1].Width == 1u);
    CHECK(ParseTextureDerivedArtifact(MakeTextureDerivedArtifact(texture)) == texture);
}

TEST_CASE("STB texture importer decodes TGA and applies deterministic settings")
{
    using namespace kairo::assets;
    const std::vector<std::byte> source = TinyTga();
    ImportRecord record;
    record.CanonicalSettings = CanonicalTextureImportSettings({
        TextureColorSpace::SRGB, false, true, 4096u });
    StbTextureImporter importer;
    const DerivedArtifact artifact = importer.Import({
        record, AssetType::Texture2D, source, {} });
    const TextureArtifactData texture = ParseTextureDerivedArtifact(artifact);
    REQUIRE(texture.Mips.size() == 2u);
    CHECK(texture.Mips.front().Width == 2u);
    CHECK(texture.Mips.front().Height == 2u);
    CHECK(texture.ColorSpace == TextureColorSpace::SRGB);
}

TEST_CASE("Material artifact round trips PBR factors and typed texture references")
{
    using namespace kairo::assets;
    MaterialArtifactData material;
    material.BaseColorFactor = { 0.8f, 0.2f, 0.1f, 1.0f };
    material.MetallicFactor = 0.9f;
    material.RoughnessFactor = 0.35f;
    material.EmissiveFactor = { 0.1f, 0.0f, 0.0f };
    material.AlphaMode = MaterialAlphaMode::Mask;
    material.AlphaCutoff = 0.45f;
    material.DoubleSided = true;
    material.NormalScale = 0.75f;
    material.Textures.BaseColor = TextureAssetHandle{ FixedAssetID(1u) };
    material.Textures.Normal = TextureAssetHandle{ FixedAssetID(32u) };
    CHECK(ParseMaterialDerivedArtifact(MakeMaterialDerivedArtifact(material)) == material);
}

TEST_CASE("glTF importer produces a hierarchy-preserving static scene artifact")
{
    using namespace kairo::assets;
    const std::string source = R"json({
      "asset":{"version":"2.0"},
      "buffers":[{"byteLength":102,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA"}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":36},
        {"buffer":0,"byteOffset":72,"byteLength":24},
        {"buffer":0,"byteOffset":96,"byteLength":6}
      ],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
        {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
      ],
      "meshes":[{"name":"Triangle","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],
      "nodes":[{"name":"Root triangle","mesh":0,"translation":[2,3,4]}],
      "scenes":[{"nodes":[0]}],
      "scene":0
    })json";

    ImportRecord record;
    GltfSceneImporter importer;
    const DerivedArtifact artifact = importer.Import({
        record, AssetType::Scene, Bytes(source), {} });
    const GltfSceneArtifactData scene = ParseGltfSceneDerivedArtifact(artifact);
    REQUIRE(scene.Primitives.size() == 1u);
    REQUIRE(scene.Nodes.size() == 1u);
    REQUIRE(scene.RootNodes == std::vector<std::uint32_t>{ 0u });
    CHECK(scene.Nodes[0].Name == "Root triangle");
    CHECK(scene.Nodes[0].PrimitiveIndices == std::vector<std::uint32_t>{ 0u });
    CHECK(scene.Nodes[0].LocalTransform[12] == 2.0f);
    CHECK(scene.Nodes[0].LocalTransform[13] == 3.0f);
    CHECK(scene.Nodes[0].LocalTransform[14] == 4.0f);
    CHECK(scene.Primitives[0].Mesh.Vertices.size() == 3u);
    CHECK(scene.Primitives[0].Mesh.Indices == std::vector<std::uint32_t>{ 0u, 1u, 2u });
}

TEST_CASE("Built-in importer registry exposes exact reproducible identities")
{
    using namespace kairo::assets;
    ImporterRegistry registry;
    RegisterBuiltinImporters(registry);
    CHECK(registry.Size() == 4u);
    CHECK(registry.Contains("kairo.obj", "1"));
    CHECK(registry.Contains("kairo.texture.stb", "1"));
    CHECK(registry.Contains("kairo.gltf.scene", "1"));
}
