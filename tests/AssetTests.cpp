#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
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

    // Ordered containers use AssetID's canonical byte ordering. This is part
    // of deterministic asset and document serialization, not presentation.
    const AssetID lower = AssetID::Parse("00000000-0000-4000-8000-000000000001");
    const AssetID higher = AssetID::Parse("00000000-0000-4000-8000-000000000002");
    CHECK((lower <=> higher) == std::strong_ordering::less);
    std::map<AssetID, int> ordered{ { higher, 2 }, { lower, 1 } };
    REQUIRE(ordered.begin()->first == lower);
}

TEST_CASE("Asset fingerprints use portable SHA-256 content identity", "[KairoAssets][Fingerprint]")
{
    const std::array<std::byte, 0u> empty{};
    const AssetFingerprint emptyFingerprint = FingerprintBytes(empty);
    CHECK(emptyFingerprint.ByteCount == 0u);
    CHECK(emptyFingerprint.ToHex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::array<std::byte, 3u> abc{ std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' } };
    const AssetFingerprint abcFingerprint = FingerprintBytes(abc);
    CHECK(abcFingerprint.ByteCount == 3u);
    CHECK(abcFingerprint.ToHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(AssetFingerprint::Parse(abcFingerprint.ToHex(), 3u) == abcFingerprint);
    REQUIRE_THROWS_AS(AssetFingerprint::Parse("not-a-sha256", 0u), std::invalid_argument);

    const auto path = std::filesystem::temp_directory_path() /
        ("kairo-fingerprint-" + GenerateAssetID().ToString());
    {
        std::ofstream file(path, std::ios::binary);
        file << "abc";
    }
    CHECK(FingerprintFile(path) == abcFingerprint);
    std::filesystem::remove(path);
}

TEST_CASE("Import provenance detects source changes without owning import execution", "[KairoAssets][Import]")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("kairo-import-" + GenerateAssetID().ToString());
    const std::filesystem::path source = root / "meshes/cube.obj";
    std::filesystem::create_directories(source.parent_path());
    {
        std::ofstream file(source, std::ios::binary);
        file << "v 0 0 0\n";
    }

    AssetRegistry registry;
    const AssetID mesh = registry.Create({ AssetType::Mesh, AssetOrigin::SourceFile,
        "assets/cube.mesh", "kairo.obj", {} });
    const ImportRecord record{ mesh, "meshes/cube.obj", "kairo.obj", "1.0.0",
        "triangulate=true", FingerprintFile(source), 1u };
    ImportDatabase imports;
    imports.Upsert(record, registry);
    CHECK(imports.Evaluate(root, mesh) == SourceImportState::Current);

    {
        std::ofstream file(source, std::ios::binary | std::ios::trunc);
        file << "v 0 0 0\nv 1 0 0\n";
    }
    CHECK(imports.Evaluate(root, mesh) == SourceImportState::Changed);
    std::filesystem::remove(source);
    CHECK(imports.Evaluate(root, mesh) == SourceImportState::Missing);

    REQUIRE_THROWS(imports.Upsert({ mesh, "../outside.obj", "kairo.obj", "1", "", record.SourceFingerprint, 1u }, registry));
    REQUIRE_THROWS(imports.Upsert({ mesh, "meshes/cube.obj", "other.importer", "1", "", record.SourceFingerprint, 1u }, registry));
    std::filesystem::remove_all(root);
}

TEST_CASE("Derived data cache is content addressed and byte exact", "[KairoAssets][Cache]")
{
    const std::array<std::byte, 3u> source{ std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' } };
    const AssetFingerprint fingerprint = FingerprintBytes(source);
    const DerivedDataKey key = MakeDerivedDataKey(fingerprint, "kairo.obj", "1.0", "triangulate=true");
    CHECK(key != MakeDerivedDataKey(fingerprint, "kairo.obj", "1.1", "triangulate=true"));
    CHECK(key != MakeDerivedDataKey(fingerprint, "kairo.obj", "1.0", "triangulate=false"));

    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("kairo-cache-" + GenerateAssetID().ToString());
    DerivedDataCache cache(root);
    CHECK_FALSE(cache.Contains(key));
    const std::array<std::byte, 4u> artifact{ std::byte{ 1u }, std::byte{ 2u }, std::byte{ 3u }, std::byte{ 4u } };
    cache.Store(key, artifact);
    CHECK(cache.Contains(key));
    CHECK(cache.Load(key) == std::vector<std::byte>(artifact.begin(), artifact.end()));
    CHECK(DerivedDataKey::Parse(key.ToString()) == key);
    std::filesystem::remove_all(root);
}

TEST_CASE("Derived artifacts require a stable declared format", "[KairoAssets][Artifact]")
{
    DerivedArtifact artifact{ AssetType::Mesh, 1u, "kairo.mesh.v1", { std::byte{ 1u } } };
    CHECK_NOTHROW(ValidateDerivedArtifact(artifact));
    artifact.Format.clear();
    REQUIRE_THROWS_AS(ValidateDerivedArtifact(artifact), std::invalid_argument);
    artifact.Format = "kairo.mesh.v1";
    artifact.FormatVersion = 0u;
    REQUIRE_THROWS_AS(ValidateDerivedArtifact(artifact), std::invalid_argument);
}

TEST_CASE("Source watcher reports only deterministic provenance transitions", "[KairoAssets][Watch]")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("kairo-watch-" + GenerateAssetID().ToString());
    const std::filesystem::path source = root / "source/mesh.obj";
    std::filesystem::create_directories(source.parent_path());
    { std::ofstream file(source, std::ios::binary); file << "one"; }
    AssetRegistry registry;
    const AssetID asset = registry.Create({ AssetType::Mesh, AssetOrigin::SourceFile, "assets/mesh", "kairo.obj", {} });
    ImportDatabase imports;
    imports.Upsert({ asset, "source/mesh.obj", "kairo.obj", "1", "", FingerprintFile(source), 1u }, registry);
    SourceWatcher watcher(root);
    watcher.Synchronize(imports);
    CHECK(watcher.Poll(imports).empty());
    { std::ofstream file(source, std::ios::binary | std::ios::trunc); file << "two"; }
    REQUIRE(watcher.Poll(imports) == std::vector<SourceChange>{ { asset, SourceImportState::Current, SourceImportState::Changed } });
    CHECK(watcher.Poll(imports).empty());
    std::filesystem::remove(source);
    REQUIRE(watcher.Poll(imports) == std::vector<SourceChange>{ { asset, SourceImportState::Changed, SourceImportState::Missing } });
    std::filesystem::remove_all(root);
}

TEST_CASE("Import service caches successful plugin output before recording provenance", "[KairoAssets][Importer]")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("kairo-importer-" + GenerateAssetID().ToString());
    std::filesystem::create_directories(root / "source");
    { std::ofstream file(root / "source/data.txt", std::ios::binary); file << "kairo"; }
    AssetRegistry registry;
    const AssetID asset = registry.Create({ AssetType::Document, AssetOrigin::SourceFile, "assets/data", "kairo.passthrough", {} });
    ImportDatabase imports; DerivedDataCache cache(root / "cache"); PassthroughImporter importer;
    ImportRecord record{ asset, "source/data.txt", importer.Identifier(), importer.Version(), "", {}, 1u };
    const ImportOutcome first = ImportSourceAsset(root, record, importer, registry, imports, cache);
    CHECK_FALSE(first.CacheHit); CHECK(cache.Load(first.Key).size() == 5u); CHECK(imports.Evaluate(root, asset) == SourceImportState::Current);
    const ImportOutcome second = ImportSourceAsset(root, imports.At(asset), importer, registry, imports, cache);
    CHECK(second.CacheHit); CHECK(second.Key == first.Key);
    std::filesystem::remove_all(root);
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

TEST_CASE("Asset categories preserve typed authoring documents", "[KairoAssets][Metadata]")
{
    CHECK(NameOfAssetType(AssetType::Document) == "document");
    REQUIRE(ParseAssetType("document").has_value());
    CHECK(*ParseAssetType("document") == AssetType::Document);

    AssetRegistry registry;
    const AssetID graph = registry.Create({ AssetType::Document, AssetOrigin::SourceFile,
        "Logic/player-controller.kdoc", "kairo.document-v1", {} });
    CHECK(registry.Resolve(DocumentAssetHandle{ graph }).Type == AssetType::Document);
    REQUIRE_THROWS(registry.Resolve(SceneAssetHandle{ graph }));
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

TEST_CASE("Asset manifests round-trip deterministically with arbitrary dependency order", "[KairoAssets][Manifest]")
{
    const AssetID texture = FixedID("000000000001");
    const AssetID material = FixedID("000000000002");
    const std::string source =
        "kairo-assets 1\n"
        "asset " + material.ToString() + " material source 4 \"materials/brick \\\"red\\\".kmat\" \"kairo.material-v1\"\n"
        "dependency " + texture.ToString() + " texture2d\n"
        "end\n"
        "asset " + texture.ToString() + " texture2d source 2 \"textures/brick.png\" \"kairo.image\"\n"
        "end\n";

    AssetRegistry registry;
    registry.ReplaceAll(ParseAssetManifest(source));
    REQUIRE(registry.Size() == 2u);
    CHECK(registry.At(material).Dependencies.front().ID == texture);
    CHECK(registry.At(material).Path == "materials/brick \"red\".kmat");
    CHECK(registry.At(material).Importer == "kairo.material-v1");

    const std::string serialized = SerializeAssetManifest(registry);
    AssetRegistry restored;
    restored.ReplaceAll(ParseAssetManifest(serialized));
    CHECK(SerializeAssetManifest(restored) == serialized);
}

TEST_CASE("Asset manifest syntax errors preserve exact source location", "[KairoAssets][Manifest]")
{
    try
    {
        (void)ParseAssetManifest("kairo-assets 1\nasset broken\n");
        FAIL("Malformed manifest should throw");
    }
    catch (const AssetManifestError& error)
    {
        CHECK(error.Line == 2u);
        CHECK(error.Column == 1u);
        CHECK(std::string(error.what()).find("2:1") != std::string::npos);
    }

    REQUIRE_THROWS_AS(ParseAssetManifest(
        "kairo-assets 1\nasset 00000000-0000-4000-8000-000000000001 mesh source 1 \"unterminated\n"),
        AssetManifestError);
}

TEST_CASE("Asset manifest files replace and reload registry state", "[KairoAssets][Manifest][IO]")
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("kairo-assets-test-" + GenerateAssetID().ToString());
    const std::filesystem::path manifest = directory / "project.kassets";
    AssetRegistry source;
    const AssetID mesh = source.Create({ AssetType::Mesh, AssetOrigin::Builtin,
        "builtin/cube", "kairo.builtin", {} });

    SaveAssetManifest(manifest, source);
    REQUIRE(std::filesystem::exists(manifest));
    AssetRegistry loaded;
    LoadAssetManifest(manifest, loaded);
    CHECK(loaded.Resolve(MeshAssetHandle{ mesh }).Path == "builtin/cube");

    {
        std::ofstream corrupt(manifest, std::ios::trunc);
        corrupt << "not-a-manifest\n";
    }
    REQUIRE_THROWS(LoadAssetManifest(manifest, loaded));
    CHECK(loaded.Contains(mesh));
    std::filesystem::remove_all(directory);
}
