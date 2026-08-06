#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

import Kairo.Assets.AssetID;
import Kairo.Assets.MaterialArtifact;
import Kairo.Assets.Metadata;

namespace
{
    [[nodiscard]] kairo::assets::TextureAssetHandle Texture(std::uint8_t seed)
    {
        kairo::assets::AssetID::Storage bytes{};
        bytes[0] = seed;
        return { kairo::assets::AssetID{ bytes } };
    }
}

TEST_CASE("Material artifacts round trip PBR factors and texture references")
{
    using namespace kairo::assets;
    MaterialArtifactData material;
    material.BaseColorFactor = { 0.25f, 0.5f, 0.75f, 0.8f };
    material.MetallicFactor = 0.3f;
    material.RoughnessFactor = 0.65f;
    material.EmissiveFactor = { 1.0f, 0.2f, 0.1f };
    material.NormalScale = 0.7f;
    material.OcclusionStrength = 0.9f;
    material.AlphaMode = MaterialAlphaMode::Mask;
    material.AlphaCutoff = 0.4f;
    material.DoubleSided = true;
    material.Textures.BaseColor = Texture(1u);
    material.Textures.Normal = Texture(2u);
    material.Textures.MetallicRoughness = Texture(3u);

    const DerivedArtifact artifact = MakeMaterialDerivedArtifact(material);
    CHECK(ParseMaterialDerivedArtifact(artifact) == material);
}

TEST_CASE("Material artifacts reject invalid physical ranges")
{
    using namespace kairo::assets;
    MaterialArtifactData material;
    material.RoughnessFactor = 1.1f;
    CHECK_THROWS(ValidateMaterialArtifactData(material));
    material.RoughnessFactor = 1.0f;
    material.OcclusionStrength = -0.1f;
    CHECK_THROWS(ValidateMaterialArtifactData(material));
    material.OcclusionStrength = 1.0f;
    material.AlphaCutoff = 2.0f;
    CHECK_THROWS(ValidateMaterialArtifactData(material));
}

TEST_CASE("Material parser rejects every truncated suffix and trailing bytes")
{
    using namespace kairo::assets;
    MaterialArtifactData material;
    material.Textures.Emissive = Texture(9u);
    const auto bytes = SerializeMaterialArtifactData(material);
    for (std::size_t size = 0u; size < bytes.size(); ++size)
        CHECK_THROWS(ParseMaterialArtifactData(std::span(bytes.data(), size)));
    auto trailing = bytes;
    trailing.push_back(std::byte{ 0u });
    CHECK_THROWS(ParseMaterialArtifactData(trailing));
}
