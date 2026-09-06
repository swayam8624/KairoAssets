#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

import Kairo.Assets;

using namespace kairo::assets;

namespace
{
    [[nodiscard]] std::array<float, 16u> Identity()
    {
        return {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
    }

    [[nodiscard]] MeshArtifactData Triangle()
    {
        MeshArtifactData mesh;
        mesh.Vertices = {
            { { 0.0f, 0.0f, 0.0f }, {}, {} },
            { { 1.0f, 0.0f, 0.0f }, {}, {} },
            { { 0.0f, 1.0f, 0.0f }, {}, {} }
        };
        mesh.Indices = { 0u, 1u, 2u };
        return mesh;
    }

    [[nodiscard]] GltfSceneArtifactData AnimatedScene()
    {
        GltfSceneArtifactData scene;
        GltfPrimitiveData primitive;
        primitive.Mesh = Triangle();
        primitive.Skinning.resize(3u);
        for (auto& influence : primitive.Skinning)
        {
            influence.Joints = { 0u, 0u, 0u, 0u };
            influence.Weights = { 1.0f, 0.0f, 0.0f, 0.0f };
        }
        scene.Primitives.push_back(primitive);

        GltfNodeData root;
        root.Name = "RootJoint";
        scene.Nodes.push_back(root);

        GltfNodeData meshNode;
        meshNode.Name = "Character";
        meshNode.Parent = 0;
        meshNode.PrimitiveIndices = { 0u };
        meshNode.SkinIndex = 0u;
        scene.Nodes.push_back(meshNode);
        scene.RootNodes = { 0u };

        GltfSkinData skin;
        skin.Name = "Rig";
        skin.SkeletonRoot = 0u;
        skin.Joints = { 0u };
        skin.InverseBindMatrices = { Identity() };
        scene.Skins.push_back(skin);

        GltfAnimationChannelData channel;
        channel.TargetNode = 1u;
        channel.Path = GltfAnimationPath::Translation;
        channel.Interpolation = GltfAnimationInterpolation::Linear;
        channel.Keyframes = {
            { 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f }, {}, {} },
            { 1.0f, { 1.0f, 0.0f, 0.0f, 0.0f }, {}, {} }
        };
        GltfAnimationClipData clip;
        clip.Name = "Move";
        clip.Channels = { channel };
        scene.Animations.push_back(clip);
        return scene;
    }

    [[nodiscard]] std::vector<std::byte> LegacyV1Payload(const GltfSceneArtifactData& source)
    {
        BinaryWriter writer;
        const std::array<std::byte, 8u> magic{
            std::byte{'K'}, std::byte{'G'}, std::byte{'L'}, std::byte{'T'},
            std::byte{'F'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        writer.WriteBytes(magic);
        writer.WriteU32(1u);
        writer.WriteU32(0u);
        writer.WriteU32(1u);
        writer.WriteU32(1u);
        writer.WriteU32(1u);

        const auto mesh = SerializeMeshArtifactData(source.Primitives[0].Mesh);
        writer.WriteU64(static_cast<std::uint64_t>(mesh.size()));
        writer.WriteBytes(mesh);
        writer.WriteU32(0u);
        writer.WriteU32(GltfMissingIndex);

        writer.WriteU32(0u);
        writer.WriteU32(std::bit_cast<std::uint32_t>(std::int32_t{-1}));
        for (float value : Identity()) writer.WriteF32(value);
        writer.WriteU32(1u);
        writer.WriteU32(0u);
        writer.WriteU32(0u);
        return std::move(writer).TakeBytes();
    }
}

TEST_CASE("glTF scene v2 round trips skinning and animation")
{
    const auto scene = AnimatedScene();
    CHECK_NOTHROW(ValidateGltfSceneArtifactData(scene));
    const auto bytes = SerializeGltfSceneArtifactData(scene);
    const auto parsed = ParseGltfSceneArtifactData(bytes);
    CHECK(parsed == scene);
    REQUIRE(parsed.Animations.size() == 1u);
    CHECK(parsed.Animations[0].DurationSeconds() == 1.0f);

    const auto artifact = MakeGltfSceneDerivedArtifact(scene);
    CHECK(artifact.FormatVersion == 2u);
    CHECK(artifact.Format == "kairo.gltf-scene.v2");
    CHECK(ParseGltfSceneDerivedArtifact(artifact) == scene);
}

TEST_CASE("glTF scene parser remains compatible with v1 static payloads")
{
    auto source = AnimatedScene();
    source.Skins.clear();
    source.Animations.clear();
    source.Primitives[0].Skinning.clear();
    source.Nodes[1].SkinIndex = GltfMissingIndex;
    source.Nodes.erase(source.Nodes.begin());
    source.Nodes[0].Parent = -1;
    source.RootNodes = { 0u };

    const auto payload = LegacyV1Payload(source);
    const auto parsed = ParseGltfSceneArtifactData(payload);
    CHECK(parsed.Primitives.size() == 1u);
    CHECK(parsed.Nodes.size() == 1u);
    CHECK(parsed.Skins.empty());
    CHECK(parsed.Animations.empty());
    CHECK(parsed.Primitives[0].Skinning.empty());
    CHECK(parsed.Nodes[0].SkinIndex == GltfMissingIndex);

    const DerivedArtifact artifact{ AssetType::Scene, 1u, "kairo.gltf-scene.v1", payload };
    CHECK(ParseGltfSceneDerivedArtifact(artifact) == parsed);
}

TEST_CASE("glTF scene rejects invalid skin weights and palette references")
{
    auto scene = AnimatedScene();
    scene.Primitives[0].Skinning[0].Weights = { 0.5f, 0.0f, 0.0f, 0.0f };
    REQUIRE_THROWS_AS(ValidateGltfSceneArtifactData(scene), std::invalid_argument);

    scene = AnimatedScene();
    scene.Primitives[0].Skinning[0].Joints[0] = 3u;
    REQUIRE_THROWS_AS(ValidateGltfSceneArtifactData(scene), std::out_of_range);
}

TEST_CASE("glTF scene rejects ambiguous or non-monotonic animation channels")
{
    auto scene = AnimatedScene();
    scene.Animations[0].Channels[0].Keyframes[1].TimeSeconds = 0.0f;
    REQUIRE_THROWS_AS(ValidateGltfSceneArtifactData(scene), std::invalid_argument);

    scene = AnimatedScene();
    scene.Animations[0].Channels.push_back(scene.Animations[0].Channels[0]);
    REQUIRE_THROWS_AS(ValidateGltfSceneArtifactData(scene), std::invalid_argument);
}
