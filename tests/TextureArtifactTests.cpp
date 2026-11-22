#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.TextureArtifact;

namespace
{
    // Deterministic fixture byte generator: create arbitrary raw pixel blobs
    // sized for the declared mip. No image decoder or GPU is involved.
    [[nodiscard]] std::vector<std::byte> Bytes(std::size_t count, std::uint8_t seed = 0u)
    {
        std::vector<std::byte> result(count);
        for (std::size_t index = 0u; index < count; ++index)
            result[index] = std::byte{ static_cast<std::uint8_t>(seed + index) };
        return result;
    }
}

// TEACHING TEST: canonical 2D texture format/mip metadata survives
// SerializeTextureArtifactData -> ParseTextureArtifactData byte-for-byte.
// Renderer upload depends on this stable size and semantic interpretation.
TEST_CASE("Texture artifacts round trip canonical mip chains")
{
    using namespace kairo::assets;
    TextureArtifactData texture;
    texture.Format = TexturePixelFormat::RGBA8;
    texture.ColorSpace = TextureColorSpace::SRGB;
    texture.Semantic = TextureSemantic::Color;
    texture.Mips = {
        { 4u, 4u, Bytes(4u * 4u * 4u) },
        { 2u, 2u, Bytes(2u * 2u * 4u, 17u) },
        { 1u, 1u, Bytes(4u, 33u) }
    };

    const DerivedArtifact artifact = MakeTextureDerivedArtifact(texture);
    CHECK(ParseTextureDerivedArtifact(artifact) == texture);
}

// TEACHING TEST: byte-correct but wrong width/height progression is invalid.
// VkBufferImageCopy relies on canonical authored mip dimensions.
TEST_CASE("Texture artifacts reject noncanonical mip dimensions")
{
    using namespace kairo::assets;
    TextureArtifactData texture;
    texture.Mips = {
        { 4u, 4u, Bytes(64u) },
        { 3u, 2u, Bytes(24u) }
    };
    CHECK_THROWS(ValidateTextureArtifactData(texture));
}

// TEACHING TEST: normal and numeric data must avoid SRGB hardware decode;
// the semantic/colour-space contract is checked before GPU creation.
TEST_CASE("Normal and data textures require linear colour space")
{
    using namespace kairo::assets;
    TextureArtifactData texture;
    texture.ColorSpace = TextureColorSpace::SRGB;
    texture.Semantic = TextureSemantic::Normal;
    texture.Mips = { { 1u, 1u, Bytes(4u) } };
    CHECK_THROWS(ValidateTextureArtifactData(texture));
    texture.ColorSpace = TextureColorSpace::Linear;
    CHECK_NOTHROW(ValidateTextureArtifactData(texture));
}

// TEACHING TEST: strict versioned payload decoding rejects incomplete and
// surplus bytes rather than silently creating an untrusted GPU upload.
TEST_CASE("Texture parser rejects truncation and trailing bytes")
{
    using namespace kairo::assets;
    TextureArtifactData texture;
    texture.Mips = { { 1u, 1u, Bytes(4u) } };
    const auto bytes = SerializeTextureArtifactData(texture);
    for (std::size_t size = 0u; size < bytes.size(); ++size)
        CHECK_THROWS(ParseTextureArtifactData(std::span(bytes.data(), size)));
    auto trailing = bytes;
    trailing.push_back(std::byte{ 0u });
    CHECK_THROWS(ParseTextureArtifactData(trailing));
}
