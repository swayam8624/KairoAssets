#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

import Kairo.Assets;

using namespace kairo::assets;

namespace
{
    void Write(const std::filesystem::path& path, std::string_view bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << bytes;
    }

    [[nodiscard]] nlohmann::json FingerprintJson(const std::filesystem::path& path)
    {
        const AssetFingerprint fingerprint = FingerprintFile(path);
        return { { "sha256", fingerprint.ToHex() }, { "size", fingerprint.ByteCount } };
    }

    [[nodiscard]] std::filesystem::path MakeBundle()
    {
        const std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("kairo-pipeline-publish-" + GenerateAssetID().ToString());
        const std::filesystem::path bundle = root / "Published/Demo/asset/crate/v001";
        const std::filesystem::path scene = bundle / "geometry/crate.gltf";
        const std::filesystem::path buffer = bundle / "geometry/crate.bin";
        Write(scene, R"({"asset":{"version":"2.0"}})");
        Write(buffer, "binary");
        const nlohmann::json manifest{
            { "schema", "kairo.publish.v1" }, { "kind", "asset" },
            { "project", "Demo" }, { "name", "crate" }, { "version", 1u },
            { "source_host", "blender" }, { "source_path", "art/crate.blend" },
            { "source_fingerprint", {
                { "sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
                { "size", 0u }
            } },
            { "outputs", nlohmann::json::array({ {
                { "path", "geometry/crate.gltf" }, { "role", "scene" },
                { "fingerprint", FingerprintJson(scene) }, { "media_type", "model/gltf+json" }
            } }) },
            { "dependencies", nlohmann::json::array({ {
                { "path", "geometry/crate.bin" }, { "role", "buffer" },
                { "fingerprint", FingerprintJson(buffer) }, { "media_type", "application/octet-stream" }
            } }) },
            { "metadata", { { "export_format", "gltf-separate" } } }
        };
        Write(bundle / "publish.kairo.json", manifest.dump());
        return root;
    }
}

TEST_CASE("Blender publish registers verified immutable glTF", "[KairoAssets][PipelinePublish]")
{
    const std::filesystem::path root = MakeBundle();
    const std::filesystem::path manifestPath = root / "Published/Demo/asset/crate/v001/publish.kairo.json";
    const PipelinePublishManifest manifest = LoadPipelinePublishManifest(manifestPath);
    CHECK(manifest.SourceHost == "blender");
    CHECK(manifest.Outputs.size() == 1u);
    CHECK_NOTHROW(ValidatePipelinePublishBundle(manifestPath, manifest));

    AssetRegistry registry;
    const AssetID id = RegisterBlenderPublish(root, manifestPath, registry);
    const AssetMetadata metadata = registry.At(id);
    CHECK(metadata.Type == AssetType::Scene);
    CHECK(metadata.Importer == "kairo.gltf.scene");
    CHECK(metadata.Path.generic_string() ==
        "Published/Demo/asset/crate/v001/geometry/crate.gltf");
    std::filesystem::remove_all(root);
}

TEST_CASE("Engine rejects a publish mutated after DCC publication", "[KairoAssets][PipelinePublish]")
{
    const std::filesystem::path root = MakeBundle();
    const std::filesystem::path manifestPath = root / "Published/Demo/asset/crate/v001/publish.kairo.json";
    const PipelinePublishManifest manifest = LoadPipelinePublishManifest(manifestPath);
    Write(manifestPath.parent_path() / "geometry/crate.bin", "tampered");
    REQUIRE_THROWS_AS(ValidatePipelinePublishBundle(manifestPath, manifest), std::invalid_argument);
    std::filesystem::remove_all(root);
}

TEST_CASE("Engine rejects unsafe or structurally loose publish manifests", "[KairoAssets][PipelinePublish]")
{
    const std::filesystem::path root = MakeBundle();
    const std::filesystem::path manifestPath = root / "Published/Demo/asset/crate/v001/publish.kairo.json";
    nlohmann::json manifest;
    {
        std::ifstream stream(manifestPath);
        stream >> manifest;
    }
    manifest["outputs"][0]["path"] = "../escape.gltf";
    Write(manifestPath, manifest.dump());
    REQUIRE_THROWS_AS(LoadPipelinePublishManifest(manifestPath), std::invalid_argument);

    manifest["outputs"][0]["path"] = "geometry/crate.gltf";
    manifest["unexpected"] = true;
    Write(manifestPath, manifest.dump());
    REQUIRE_THROWS_AS(LoadPipelinePublishManifest(manifestPath), std::invalid_argument);
    std::filesystem::remove_all(root);
}
