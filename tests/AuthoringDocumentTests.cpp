#include <algorithm>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets;

TEST_CASE("seams split UV islands and deterministic packing keeps coordinates normalized")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    const auto f0 = mesh.AddFace({ a, b, c });
    const auto f1 = mesh.AddFace({ a, c, d });

    UVLayout layout;
    PlanarUnwrap(mesh, layout, UVProjectionAxis::Z);
    REQUIRE(BuildUVIslands(mesh, layout).size() == 1u);
    layout.MarkSeam(EditableEdgeKey::Canonical(a, c));
    const auto islands = BuildUVIslands(mesh, layout);
    REQUIRE(islands.size() == 2u);
    CHECK(islands[0].Faces.front() == f0);
    CHECK(islands[1].Faces.front() == f1);

    PackUVIslands(mesh, layout, 0.02);
    for (const auto& [corner, uv] : layout.Coordinates())
    {
        (void)corner;
        CHECK(uv[0] >= 0.0);
        CHECK(uv[0] <= 1.0);
        CHECK(uv[1] >= 0.0);
        CHECK(uv[1] <= 1.0);
    }
}

TEST_CASE("material authoring validates texture semantics and exposes PBR channels")
{
    using namespace kairo::assets;
    TextureAuthoringSettings normal;
    normal.Semantic = TextureAuthoringSemantic::Normal;
    normal.ColorSpace = TextureAuthoringColorSpace::Linear;
    normal.Validate();
    CHECK(normal.ResolvedColorSpace() == TextureAuthoringColorSpace::Linear);
    normal.ColorSpace = TextureAuthoringColorSpace::SRGB;
    CHECK_THROWS_AS(normal.Validate(), std::invalid_argument);

    MaterialArtifactData material;
    material.BaseColorFactor = { 0.2f, 0.4f, 0.6f, 1.0f };
    material.MetallicFactor = 0.75f;
    material.RoughnessFactor = 0.25f;
    const auto channels = InspectMaterialChannels(material);
    REQUIRE(channels.size() == 5u);
    CHECK(channels[0].Name == "BaseColor");
    CHECK(channels[1].Name == "Normal");
    CHECK_FALSE(channels[1].UsesSRGB);
}

TEST_CASE("editable mesh document round trips topology UVs modifiers materials and reimport settings")
{
    using namespace kairo::assets;
    EditableMeshDocument document;
    const auto a = document.Mesh.AddVertex({ -1.0, -1.0, 0.0 });
    const auto b = document.Mesh.AddVertex({ 1.0, -1.0, 0.0 });
    const auto c = document.Mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = document.Mesh.AddVertex({ -1.0, 1.0, 0.0 });
    const auto face = document.Mesh.AddFace({ a, b, c, d }, 0u);
    PlanarUnwrap(document.Mesh, document.UVs);
    document.UVs.MarkSeam(EditableEdgeKey::Canonical(a, b));
    document.Modifiers.Add(TranslateModifier{ { 0.0, 0.0, 2.0 } });
    document.Modifiers.Add(TriangulateModifier{});

    MaterialArtifactData material;
    material.BaseColorFactor = { 0.7f, 0.2f, 0.1f, 1.0f };
    material.MetallicFactor = 0.4f;
    material.RoughnessFactor = 0.6f;
    REQUIRE(document.Materials.AddMaterial(material) == 0u);
    AssignMaterialSlot(document.Mesh, { face }, 0u);

    const AssetID textureID = AssetID::Parse("12345678-1234-4abc-8def-1234567890ab");
    TextureAuthoringSettings settings;
    settings.Semantic = TextureAuthoringSemantic::BaseColor;
    settings.ColorSpace = TextureAuthoringColorSpace::SRGB;
    settings.MaximumResolution = 4096u;
    document.Materials.BindTexture(TextureAssetHandle{ textureID }, settings);

    const std::string first = SerializeEditableMeshDocument(document);
    const EditableMeshDocument restored = ParseEditableMeshDocument(first);
    const std::string second = SerializeEditableMeshDocument(restored);
    CHECK(second == first);
    CHECK(restored.Mesh.Vertices().size() == 4u);
    CHECK(restored.Mesh.Faces().size() == 1u);
    CHECK(restored.UVs.Coordinates().size() == 4u);
    CHECK(restored.UVs.Seams().size() == 1u);
    CHECK(restored.Modifiers.Modifiers().size() == 2u);
    CHECK(restored.Materials.Materials().size() == 1u);
    CHECK(restored.Materials.TextureBindings().size() == 1u);
}

TEST_CASE("editable mesh documents save and reopen from a project path")
{
    using namespace kairo::assets;
    EditableMeshDocument document;
    const auto a = document.Mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = document.Mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = document.Mesh.AddVertex({ 0.0, 1.0, 0.0 });
    (void)document.Mesh.AddFace({ a, b, c });
    PlanarUnwrap(document.Mesh, document.UVs);

    const auto root = std::filesystem::temp_directory_path() / "kairo-editable-document-test";
    const auto path = root / "Meshes" / "triangle.kmeshdoc";
    std::filesystem::remove_all(root);
    SaveEditableMeshDocument(document, path);
    REQUIRE(std::filesystem::exists(path));
    const EditableMeshDocument loaded = LoadEditableMeshDocument(path);
    CHECK(SerializeEditableMeshDocument(loaded) == SerializeEditableMeshDocument(document));
    std::filesystem::remove_all(root);
}
