#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import Kairo.Assets;

using namespace kairo::assets;

namespace
{
    [[nodiscard]] AssetID FixedID(std::string_view suffix)
    {
        return AssetID::Parse("00000000-0000-4000-8000-" + std::string(suffix));
    }

    [[nodiscard]] AssetMetadata Metadata(AssetID id, AssetType type, std::string path)
    {
        return { id, type, AssetOrigin::SourceFile, std::move(path), "test.importer", 1u, {} };
    }
}

TEST_CASE("Asset IDs round-trip canonical UUID text", "[KairoAssets][Identity]")
{
    const AssetID id = AssetID::Parse("12345678-90AB-4def-8123-456789abcdef");
    CHECK(id.ToString() == "12345678-90ab-4def-8123-456789abcdef");
    CHECK(id.IsValid());
    CHECK_FALSE(AssetID{}.IsValid());
    REQUIRE_THROWS_AS(AssetID::Parse("not-an-asset-id"), std::invalid_argument);

    const AssetID generated = GenerateAssetID();
    CHECK(generated.IsValid());
    CHECK((generated.Bytes()[6] & 0xf0u) == 0x40u);
    CHECK((generated.Bytes()[8] & 0xc0u) == 0x80u);
}

TEST_CASE("Asset paths normalize portably and prevent traversal", "[KairoAssets][Metadata]")
{
    CHECK(NormalizeAssetPath("meshes/props/../cube.obj") == std::filesystem::path("meshes/cube.obj"));
    CHECK(PortableAssetPathKey("Textures/Brick.PNG") == "textures/brick.png");
    REQUIRE_THROWS(NormalizeAssetPath("../outside.mesh"));
    REQUIRE_THROWS(NormalizeAssetPath("C:/absolute.mesh"));
    REQUIRE_THROWS(NormalizeAssetPath("folder\\windows.mesh"));
    REQUIRE_THROWS(NormalizeAssetPath("."));
}

TEST_CASE("Registry preserves typed identity and portable path uniqueness", "[KairoAssets][Registry]")
{
    AssetRegistry registry;
    const AssetID mesh = registry.Create({ AssetType::Mesh, AssetOrigin::Builtin,
        "builtin/cube", "kairo.builtin", {} });
    CHECK(registry.Size() == 1u);
    CHECK(registry.Resolve(MeshAssetHandle{ mesh }).Path == "builtin/cube");
    REQUIRE_THROWS(registry.Resolve(MaterialAssetHandle{ mesh }));
    REQUIRE_THROWS(registry.Create({ AssetType::Mesh, AssetOrigin::Builtin,
        "Builtin/Cube", "kairo.builtin", {} }));

    registry.Move(mesh, "builtin/unit_cube");
    CHECK(registry.At(mesh).Revision == 2u);
    CHECK(registry.FindByPath("BUILTIN/UNIT_CUBE")->ID == mesh);
}

TEST_CASE("Registry rejects missing, mistyped, cyclic, and dangling dependencies", "[KairoAssets][Registry]")
{
    AssetRegistry registry;
    const AssetID texture = FixedID("000000000001");
    const AssetID material = FixedID("000000000002");
    registry.Insert(Metadata(texture, AssetType::Texture2D, "textures/albedo.png"));
    AssetMetadata materialMetadata = Metadata(material, AssetType::Material, "materials/brick.kmat");
    materialMetadata.Dependencies.push_back({ texture, AssetType::Texture2D });
    registry.Insert(materialMetadata);

    REQUIRE_THROWS(registry.Remove(texture));
    REQUIRE_THROWS(registry.SetDependencies(texture, { { material, AssetType::Material } }));
    REQUIRE_THROWS(registry.SetDependencies(material, { { texture, AssetType::Mesh } }));
    REQUIRE_THROWS(registry.SetDependencies(material, { { FixedID("000000000099"), AssetType::Texture2D } }));

    registry.SetDependencies(material, {});
    registry.Remove(texture);
    CHECK_FALSE(registry.Contains(texture));
    REQUIRE(registry.Snapshot().size() == 1u);
    CHECK(registry.Snapshot().front().ID == material);
}

TEST_CASE("Registry mutations preserve state when revision is exhausted", "[KairoAssets][Registry]")
{
    AssetRegistry registry;
    AssetMetadata metadata = Metadata(FixedID("000000000010"), AssetType::Mesh, "meshes/frozen.mesh");
    metadata.Revision = std::numeric_limits<std::uint64_t>::max();
    registry.Insert(metadata);

    REQUIRE_THROWS_AS(registry.Move(metadata.ID, "meshes/moved.mesh"), std::overflow_error);
    CHECK(registry.At(metadata.ID).Path == "meshes/frozen.mesh");
    CHECK_FALSE(registry.FindByPath("meshes/moved.mesh").has_value());
    REQUIRE_THROWS_AS(registry.SetDependencies(metadata.ID, {}), std::overflow_error);
    CHECK(registry.At(metadata.ID).Dependencies.empty());
}

TEST_CASE("Registry creates assets safely from concurrent workers", "[KairoAssets][Registry][Threading]")
{
    AssetRegistry registry;
    std::atomic<bool> failed = false;
    std::vector<std::jthread> workers;
    for (int index = 0; index < 24; ++index)
    {
        workers.emplace_back([&registry, &failed, index]
        {
            try
            {
                registry.Create({ AssetType::Texture2D, AssetOrigin::Generated,
                    "generated/texture_" + std::to_string(index), "test.concurrent", {} });
            }
            catch (...)
            {
                failed.store(true);
            }
        });
    }
    workers.clear();
    CHECK_FALSE(failed.load());
    CHECK(registry.Size() == 24u);
}
