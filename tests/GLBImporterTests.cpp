#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.GLBImporter;
import Kairo.Assets.MeshArtifact;
import Kairo.Assets.Types;

namespace
{
    void U32(std::vector<std::byte>& bytes, std::uint32_t value)
    {
        for (std::size_t index = 0u; index < 4u; ++index)
            bytes.push_back(std::byte{ static_cast<std::uint8_t>(value >> (index * 8u)) });
    }

    void F32(std::vector<std::byte>& bytes, float value)
    {
        U32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::vector<std::byte> MinimalTriangleGLB()
    {
        std::vector<std::byte> bin;
        F32(bin, 0.0f); F32(bin, 0.0f); F32(bin, 0.0f);
        F32(bin, 1.0f); F32(bin, 0.0f); F32(bin, 0.0f);
        F32(bin, 0.0f); F32(bin, 1.0f); F32(bin, 0.0f);
        U32(bin, 0u); U32(bin, 1u); U32(bin, 2u);

        nlohmann::json json = {
            { "asset", { { "version", "2.0" } } },
            { "buffers", nlohmann::json::array({ { { "byteLength", bin.size() } } }) },
            { "bufferViews", nlohmann::json::array({
                { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 36 } },
                { { "buffer", 0 }, { "byteOffset", 36 }, { "byteLength", 12 } }
            }) },
            { "accessors", nlohmann::json::array({
                { { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" } },
                { { "bufferView", 1 }, { "componentType", 5125 }, { "count", 3 }, { "type", "SCALAR" } }
            }) },
            { "meshes", nlohmann::json::array({
                { { "primitives", nlohmann::json::array({
                    { { "attributes", { { "POSITION", 0 } } }, { "indices", 1 }, { "mode", 4 } }
                }) } }
            }) }
        };
        std::string jsonText = json.dump();
        while (jsonText.size() % 4u != 0u) jsonText.push_back(' ');
        while (bin.size() % 4u != 0u) bin.push_back(std::byte{ 0u });

        std::vector<std::byte> glb;
        U32(glb, 0x46546c67u);
        U32(glb, 2u);
        U32(glb, static_cast<std::uint32_t>(12u + 8u + jsonText.size() + 8u + bin.size()));
        U32(glb, static_cast<std::uint32_t>(jsonText.size()));
        U32(glb, 0x4e4f534au);
        for (const char character : jsonText)
            glb.push_back(std::byte{ static_cast<unsigned char>(character) });
        U32(glb, static_cast<std::uint32_t>(bin.size()));
        U32(glb, 0x004e4942u);
        glb.insert(glb.end(), bin.begin(), bin.end());
        return glb;
    }
}

TEST_CASE("Static GLB importer produces canonical mesh artifacts")
{
    using namespace kairo::assets;
    const auto bytes = MinimalTriangleGLB();
    StaticGLBImporter importer;
    const DerivedArtifact artifact = importer.Import({ {}, AssetType::Mesh, bytes });
    const MeshArtifactData mesh = ParseMeshDerivedArtifact(artifact);
    REQUIRE(mesh.Vertices.size() == 3u);
    REQUIRE(mesh.Indices.size() == 3u);
    CHECK(mesh.Indices[0] == 0u);
    CHECK(mesh.Indices[1] == 1u);
    CHECK(mesh.Indices[2] == 2u);
    CHECK(mesh.Vertices[1].Position[0] == 1.0f);
    CHECK(mesh.Vertices[2].Position[1] == 1.0f);
    CHECK_FALSE(mesh.HasNormals);
    CHECK_FALSE(mesh.HasTexCoords);
}

TEST_CASE("Static GLB importer rejects malformed and unsupported input")
{
    using namespace kairo::assets;
    StaticGLBImporter importer;
    const std::vector<std::byte> empty;
    CHECK_THROWS(importer.Import({ {}, AssetType::Mesh, empty }));
    const auto valid = MinimalTriangleGLB();
    CHECK_THROWS(importer.Import({ {}, AssetType::Texture2D, valid }));
    for (std::size_t size = 0u; size < valid.size(); ++size)
        CHECK_THROWS(importer.Import({ {}, AssetType::Mesh, std::span(valid.data(), size) }));
}
