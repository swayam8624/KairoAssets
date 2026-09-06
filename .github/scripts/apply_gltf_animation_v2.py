from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


path = Path("GltfSceneArtifact.cppm")
text = path.read_text()

# Public portable data model.
marker = '''    struct GltfPrimitiveData final
    {
        MeshArtifactData Mesh;
        std::vector<std::array<float, 4u>> Tangents;
        std::uint32_t MaterialIndex = std::numeric_limits<std::uint32_t>::max();

        friend bool operator==(const GltfPrimitiveData&, const GltfPrimitiveData&) = default;
    };
'''
replacement = '''    inline constexpr std::uint32_t GltfMissingIndex =
        std::numeric_limits<std::uint32_t>::max();

    struct GltfVertexSkinData final
    {
        std::array<std::uint32_t, 4u> Joints{};
        std::array<float, 4u> Weights{};

        friend bool operator==(const GltfVertexSkinData&, const GltfVertexSkinData&) = default;
    };

    enum class GltfAnimationPath : std::uint8_t
    {
        Translation = 1u,
        Rotation = 2u,
        Scale = 3u
    };

    enum class GltfAnimationInterpolation : std::uint8_t
    {
        Linear = 1u,
        Step = 2u,
        CubicSpline = 3u
    };

    struct GltfAnimationKeyframe final
    {
        float TimeSeconds = 0.0f;
        std::array<float, 4u> Value{};
        std::array<float, 4u> InTangent{};
        std::array<float, 4u> OutTangent{};

        friend bool operator==(const GltfAnimationKeyframe&, const GltfAnimationKeyframe&) = default;
    };

    struct GltfAnimationChannelData final
    {
        std::uint32_t TargetNode = GltfMissingIndex;
        GltfAnimationPath Path = GltfAnimationPath::Translation;
        GltfAnimationInterpolation Interpolation = GltfAnimationInterpolation::Linear;
        std::vector<GltfAnimationKeyframe> Keyframes;

        friend bool operator==(const GltfAnimationChannelData&, const GltfAnimationChannelData&) = default;
    };

    struct GltfAnimationClipData final
    {
        std::string Name;
        std::vector<GltfAnimationChannelData> Channels;

        [[nodiscard]] float DurationSeconds() const noexcept
        {
            float duration = 0.0f;
            for (const auto& channel : Channels)
                if (!channel.Keyframes.empty())
                    duration = std::max(duration, channel.Keyframes.back().TimeSeconds);
            return duration;
        }

        friend bool operator==(const GltfAnimationClipData&, const GltfAnimationClipData&) = default;
    };

    struct GltfSkinData final
    {
        std::string Name;
        std::uint32_t SkeletonRoot = GltfMissingIndex;
        std::vector<std::uint32_t> Joints;
        std::vector<std::array<float, 16u>> InverseBindMatrices;

        friend bool operator==(const GltfSkinData&, const GltfSkinData&) = default;
    };

    struct GltfPrimitiveData final
    {
        MeshArtifactData Mesh;
        std::vector<std::array<float, 4u>> Tangents;
        std::uint32_t MaterialIndex = GltfMissingIndex;
        std::vector<GltfVertexSkinData> Skinning;

        friend bool operator==(const GltfPrimitiveData&, const GltfPrimitiveData&) = default;
    };
'''
text = replace_once(text, marker, replacement, "public skin/animation model")

marker = '''        std::vector<std::uint32_t> PrimitiveIndices;

        friend bool operator==(const GltfNodeData&, const GltfNodeData&) = default;
'''
replacement = '''        std::vector<std::uint32_t> PrimitiveIndices;
        std::uint32_t SkinIndex = GltfMissingIndex;

        friend bool operator==(const GltfNodeData&, const GltfNodeData&) = default;
'''
text = replace_once(text, marker, replacement, "node skin binding")

marker = '''        std::vector<GltfNodeData> Nodes;
        std::vector<std::uint32_t> RootNodes;

        friend bool operator==(const GltfSceneArtifactData&, const GltfSceneArtifactData&) = default;
'''
replacement = '''        std::vector<GltfNodeData> Nodes;
        std::vector<std::uint32_t> RootNodes;
        std::vector<GltfSkinData> Skins;
        std::vector<GltfAnimationClipData> Animations;

        friend bool operator==(const GltfSceneArtifactData&, const GltfSceneArtifactData&) = default;
'''
text = replace_once(text, marker, replacement, "scene animation collections")

text = replace_once(
    text,
    '        constexpr std::uint32_t PayloadVersion = 1u;\n',
    '        constexpr std::uint32_t LegacyPayloadVersion = 1u;\n'
    '        constexpr std::uint32_t PayloadVersion = 2u;\n',
    "artifact payload versions",
)
text = replace_once(
    text,
    '        constexpr std::uint32_t MissingIndex = std::numeric_limits<std::uint32_t>::max();\n',
    '        constexpr std::uint32_t MissingIndex = GltfMissingIndex;\n',
    "artifact missing index",
)

# Validation: extend record limits.
text = replace_once(
    text,
    '''        if (scene.Materials.size() > MaximumRecords || scene.Primitives.size() > MaximumRecords ||
            scene.Nodes.size() > MaximumRecords || scene.RootNodes.size() > MaximumRecords)
''',
    '''        if (scene.Materials.size() > MaximumRecords || scene.Primitives.size() > MaximumRecords ||
            scene.Nodes.size() > MaximumRecords || scene.RootNodes.size() > MaximumRecords ||
            scene.Skins.size() > MaximumRecords || scene.Animations.size() > MaximumRecords)
''',
    "scene count validation",
)

marker = '''            if (primitive.MaterialIndex != MissingIndex &&
                primitive.MaterialIndex >= scene.Materials.size())
                throw std::out_of_range("glTF primitive material index is invalid.");
        }

        for (std::size_t nodeIndex = 0u; nodeIndex < scene.Nodes.size(); ++nodeIndex)
'''
replacement = '''            if (primitive.MaterialIndex != MissingIndex &&
                primitive.MaterialIndex >= scene.Materials.size())
                throw std::out_of_range("glTF primitive material index is invalid.");
            if (!primitive.Skinning.empty())
            {
                if (primitive.Skinning.size() != primitive.Mesh.Vertices.size())
                    throw std::invalid_argument(
                        "glTF skin influence count must match the primitive vertex count.");
                for (const GltfVertexSkinData& influence : primitive.Skinning)
                {
                    if (!Finite(influence.Weights))
                        throw std::invalid_argument("glTF skin weights must be finite.");
                    float total = 0.0f;
                    for (float weight : influence.Weights)
                    {
                        if (weight < 0.0f)
                            throw std::invalid_argument("glTF skin weights cannot be negative.");
                        total += weight;
                    }
                    if (std::abs(total - 1.0f) > 1.0e-3f)
                        throw std::invalid_argument(
                            "glTF skin weights must sum to one for every skinned vertex.");
                }
            }
        }

        for (std::size_t nodeIndex = 0u; nodeIndex < scene.Nodes.size(); ++nodeIndex)
'''
text = replace_once(text, marker, replacement, "primitive skin validation")

marker = '''            for (const std::uint32_t primitive : node.PrimitiveIndices)
                if (primitive >= scene.Primitives.size())
                    throw std::out_of_range("glTF node primitive index is invalid.");
        }
'''
replacement = '''            for (const std::uint32_t primitive : node.PrimitiveIndices)
                if (primitive >= scene.Primitives.size())
                    throw std::out_of_range("glTF node primitive index is invalid.");
            if (node.SkinIndex != MissingIndex)
            {
                if (node.SkinIndex >= scene.Skins.size())
                    throw std::out_of_range("glTF node skin index is invalid.");
                if (node.PrimitiveIndices.empty())
                    throw std::invalid_argument("A glTF skin binding requires a mesh-bearing node.");
            }
        }
'''
text = replace_once(text, marker, replacement, "node skin validation")

marker = '''        for (std::size_t nodeIndex = 0u; nodeIndex < scene.Nodes.size(); ++nodeIndex)
        {
            std::size_t cursor = nodeIndex;
            std::size_t steps = 0u;
            while (scene.Nodes[cursor].Parent >= 0)
            {
                cursor = static_cast<std::size_t>(scene.Nodes[cursor].Parent);
                if (++steps > scene.Nodes.size())
                    throw std::invalid_argument("glTF node hierarchy contains a cycle.");
            }
        }
    }
'''
replacement = '''        for (std::size_t nodeIndex = 0u; nodeIndex < scene.Nodes.size(); ++nodeIndex)
        {
            std::size_t cursor = nodeIndex;
            std::size_t steps = 0u;
            while (scene.Nodes[cursor].Parent >= 0)
            {
                cursor = static_cast<std::size_t>(scene.Nodes[cursor].Parent);
                if (++steps > scene.Nodes.size())
                    throw std::invalid_argument("glTF node hierarchy contains a cycle.");
            }
        }

        for (const GltfSkinData& skin : scene.Skins)
        {
            if (skin.Name.size() > MaximumNameBytes)
                throw std::length_error("glTF skin name exceeds its safety limit.");
            if (skin.Joints.empty())
                throw std::invalid_argument("glTF skin requires at least one joint.");
            if (skin.Joints.size() > MaximumRecords)
                throw std::length_error("glTF skin exceeds its joint safety limit.");
            if (skin.InverseBindMatrices.size() != skin.Joints.size())
                throw std::invalid_argument(
                    "glTF skin requires one inverse-bind matrix per joint.");
            if (skin.SkeletonRoot != MissingIndex && skin.SkeletonRoot >= scene.Nodes.size())
                throw std::out_of_range("glTF skeleton root node index is invalid.");
            for (std::size_t jointIndex = 0u; jointIndex < skin.Joints.size(); ++jointIndex)
            {
                if (skin.Joints[jointIndex] >= scene.Nodes.size())
                    throw std::out_of_range("glTF skin joint node index is invalid.");
                if (!Finite(skin.InverseBindMatrices[jointIndex]))
                    throw std::invalid_argument("glTF inverse-bind matrices must be finite.");
                for (std::size_t previous = 0u; previous < jointIndex; ++previous)
                    if (skin.Joints[previous] == skin.Joints[jointIndex])
                        throw std::invalid_argument("glTF skin joints must be unique.");
            }
        }

        for (const GltfNodeData& node : scene.Nodes)
        {
            if (node.SkinIndex == MissingIndex) continue;
            const GltfSkinData& skin = scene.Skins[node.SkinIndex];
            for (const std::uint32_t primitiveIndex : node.PrimitiveIndices)
            {
                const GltfPrimitiveData& primitive = scene.Primitives[primitiveIndex];
                if (primitive.Skinning.empty())
                    throw std::invalid_argument(
                        "A skinned glTF node references a primitive without skin influences.");
                for (const GltfVertexSkinData& influence : primitive.Skinning)
                    for (std::size_t slot = 0u; slot < influence.Weights.size(); ++slot)
                        if (influence.Weights[slot] > 0.0f && influence.Joints[slot] >= skin.Joints.size())
                            throw std::out_of_range(
                                "glTF vertex joint index exceeds the bound skin palette.");
            }
        }

        for (const GltfAnimationClipData& clip : scene.Animations)
        {
            if (clip.Name.size() > MaximumNameBytes)
                throw std::length_error("glTF animation name exceeds its safety limit.");
            if (clip.Channels.empty())
                throw std::invalid_argument("glTF animation requires at least one channel.");
            if (clip.Channels.size() > MaximumRecords)
                throw std::length_error("glTF animation exceeds its channel safety limit.");
            for (std::size_t channelIndex = 0u; channelIndex < clip.Channels.size(); ++channelIndex)
            {
                const GltfAnimationChannelData& channel = clip.Channels[channelIndex];
                if (channel.TargetNode >= scene.Nodes.size())
                    throw std::out_of_range("glTF animation target node index is invalid.");
                switch (channel.Path)
                {
                    case GltfAnimationPath::Translation:
                    case GltfAnimationPath::Rotation:
                    case GltfAnimationPath::Scale: break;
                    default: throw std::invalid_argument("glTF animation path is invalid.");
                }
                switch (channel.Interpolation)
                {
                    case GltfAnimationInterpolation::Linear:
                    case GltfAnimationInterpolation::Step:
                    case GltfAnimationInterpolation::CubicSpline: break;
                    default: throw std::invalid_argument("glTF animation interpolation is invalid.");
                }
                if (channel.Keyframes.empty())
                    throw std::invalid_argument("glTF animation channel requires at least one keyframe.");
                if (channel.Keyframes.size() > MaximumRecords)
                    throw std::length_error("glTF animation channel exceeds its keyframe safety limit.");
                for (std::size_t previous = 0u; previous < channelIndex; ++previous)
                    if (clip.Channels[previous].TargetNode == channel.TargetNode &&
                        clip.Channels[previous].Path == channel.Path)
                        throw std::invalid_argument(
                            "glTF animation cannot contain duplicate channels for one node/path.");

                float previousTime = -1.0f;
                for (const GltfAnimationKeyframe& key : channel.Keyframes)
                {
                    if (!std::isfinite(key.TimeSeconds) || key.TimeSeconds < 0.0f ||
                        key.TimeSeconds <= previousTime)
                        throw std::invalid_argument(
                            "glTF animation key times must be finite, non-negative, and strictly increasing.");
                    previousTime = key.TimeSeconds;
                    if (!Finite(key.Value) || !Finite(key.InTangent) || !Finite(key.OutTangent))
                        throw std::invalid_argument("glTF animation keyframe values must be finite.");
                    if (channel.Path == GltfAnimationPath::Rotation)
                    {
                        float lengthSquared = 0.0f;
                        for (float value : key.Value) lengthSquared += value * value;
                        if (std::abs(lengthSquared - 1.0f) > 1.0e-3f)
                            throw std::invalid_argument(
                                "glTF animation rotation keys must contain unit quaternions.");
                    }
                }
            }
        }
    }
'''
text = replace_once(text, marker, replacement, "skin and animation validation")

# Serialization extensions.
text = replace_once(
    text,
    '''            for (const auto& tangent : primitive.Tangents)
                for (float value : tangent) writer.WriteF32(value);
            writer.WriteU32(primitive.MaterialIndex);
        }
''',
    '''            for (const auto& tangent : primitive.Tangents)
                for (float value : tangent) writer.WriteF32(value);
            writer.WriteU32(primitive.MaterialIndex);
            writer.WriteU32(static_cast<std::uint32_t>(primitive.Skinning.size()));
            for (const GltfVertexSkinData& influence : primitive.Skinning)
            {
                for (std::uint32_t joint : influence.Joints) writer.WriteU32(joint);
                for (float weight : influence.Weights) writer.WriteF32(weight);
            }
        }
''',
    "primitive skin serialization",
)
text = replace_once(
    text,
    '''            writer.WriteU32(static_cast<std::uint32_t>(node.PrimitiveIndices.size()));
            for (const std::uint32_t primitive : node.PrimitiveIndices) writer.WriteU32(primitive);
        }
        for (const std::uint32_t root : scene.RootNodes) writer.WriteU32(root);

        if (writer.Bytes().size() > MaximumDerivedArtifactPayloadBytes)
''',
    '''            writer.WriteU32(static_cast<std::uint32_t>(node.PrimitiveIndices.size()));
            for (const std::uint32_t primitive : node.PrimitiveIndices) writer.WriteU32(primitive);
            writer.WriteU32(node.SkinIndex);
        }
        for (const std::uint32_t root : scene.RootNodes) writer.WriteU32(root);

        writer.WriteU32(static_cast<std::uint32_t>(scene.Skins.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.Animations.size()));
        for (const GltfSkinData& skin : scene.Skins)
        {
            WriteString(writer, skin.Name, MaximumNameBytes, "glTF skin name");
            writer.WriteU32(skin.SkeletonRoot);
            writer.WriteU32(static_cast<std::uint32_t>(skin.Joints.size()));
            for (std::size_t joint = 0u; joint < skin.Joints.size(); ++joint)
            {
                writer.WriteU32(skin.Joints[joint]);
                for (float value : skin.InverseBindMatrices[joint]) writer.WriteF32(value);
            }
        }
        for (const GltfAnimationClipData& clip : scene.Animations)
        {
            WriteString(writer, clip.Name, MaximumNameBytes, "glTF animation name");
            writer.WriteU32(static_cast<std::uint32_t>(clip.Channels.size()));
            for (const GltfAnimationChannelData& channel : clip.Channels)
            {
                writer.WriteU32(channel.TargetNode);
                writer.WriteU8(static_cast<std::uint8_t>(channel.Path));
                writer.WriteU8(static_cast<std::uint8_t>(channel.Interpolation));
                writer.WriteU32(static_cast<std::uint32_t>(channel.Keyframes.size()));
                for (const GltfAnimationKeyframe& key : channel.Keyframes)
                {
                    writer.WriteF32(key.TimeSeconds);
                    for (float value : key.Value) writer.WriteF32(value);
                    for (float value : key.InTangent) writer.WriteF32(value);
                    for (float value : key.OutTangent) writer.WriteF32(value);
                }
            }
        }

        if (writer.Bytes().size() > MaximumDerivedArtifactPayloadBytes)
''',
    "scene v2 serialization",
)

# Parser accepts v1 and v2.
text = replace_once(
    text,
    '''        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("glTF scene artifact version is unsupported.");

        const std::uint32_t materialCount = reader.ReadU32();
''',
    '''        const std::uint32_t payloadVersion = reader.ReadU32();
        if (payloadVersion != LegacyPayloadVersion && payloadVersion != PayloadVersion)
            throw std::invalid_argument("glTF scene artifact version is unsupported.");

        const std::uint32_t materialCount = reader.ReadU32();
''',
    "payload version parse",
)
text = replace_once(
    text,
    '''            primitive.MaterialIndex = reader.ReadU32();
            scene.Primitives.push_back(std::move(primitive));
''',
    '''            primitive.MaterialIndex = reader.ReadU32();
            if (payloadVersion >= PayloadVersion)
            {
                const std::uint32_t influenceCount = reader.ReadU32();
                if (influenceCount > MaximumRecords)
                    throw std::length_error("glTF primitive declares too many skin influences.");
                primitive.Skinning.resize(influenceCount);
                for (GltfVertexSkinData& influence : primitive.Skinning)
                {
                    for (std::uint32_t& joint : influence.Joints) joint = reader.ReadU32();
                    for (float& weight : influence.Weights) weight = reader.ReadF32();
                }
            }
            scene.Primitives.push_back(std::move(primitive));
''',
    "primitive skin parse",
)
text = replace_once(
    text,
    '''            node.PrimitiveIndices.resize(nodePrimitiveCount);
            for (std::uint32_t& primitive : node.PrimitiveIndices)
                primitive = reader.ReadU32();
            scene.Nodes.push_back(std::move(node));
''',
    '''            node.PrimitiveIndices.resize(nodePrimitiveCount);
            for (std::uint32_t& primitive : node.PrimitiveIndices)
                primitive = reader.ReadU32();
            if (payloadVersion >= PayloadVersion) node.SkinIndex = reader.ReadU32();
            scene.Nodes.push_back(std::move(node));
''',
    "node skin parse",
)
text = replace_once(
    text,
    '''        for (std::uint32_t index = 0u; index < rootCount; ++index)
            scene.RootNodes.push_back(reader.ReadU32());

        reader.RequireEnd();
''',
    '''        for (std::uint32_t index = 0u; index < rootCount; ++index)
            scene.RootNodes.push_back(reader.ReadU32());

        if (payloadVersion >= PayloadVersion)
        {
            const std::uint32_t skinCount = reader.ReadU32();
            const std::uint32_t animationCount = reader.ReadU32();
            if (skinCount > MaximumRecords || animationCount > MaximumRecords)
                throw std::length_error("glTF scene declares too many skins or animations.");
            scene.Skins.reserve(skinCount);
            scene.Animations.reserve(animationCount);
            for (std::uint32_t index = 0u; index < skinCount; ++index)
            {
                GltfSkinData skin;
                skin.Name = ReadString(reader, MaximumNameBytes, "glTF skin name");
                skin.SkeletonRoot = reader.ReadU32();
                const std::uint32_t jointCount = reader.ReadU32();
                if (jointCount > MaximumRecords)
                    throw std::length_error("glTF skin declares too many joints.");
                skin.Joints.resize(jointCount);
                skin.InverseBindMatrices.resize(jointCount);
                for (std::uint32_t joint = 0u; joint < jointCount; ++joint)
                {
                    skin.Joints[joint] = reader.ReadU32();
                    for (float& value : skin.InverseBindMatrices[joint]) value = reader.ReadF32();
                }
                scene.Skins.push_back(std::move(skin));
            }
            for (std::uint32_t index = 0u; index < animationCount; ++index)
            {
                GltfAnimationClipData clip;
                clip.Name = ReadString(reader, MaximumNameBytes, "glTF animation name");
                const std::uint32_t channelCount = reader.ReadU32();
                if (channelCount > MaximumRecords)
                    throw std::length_error("glTF animation declares too many channels.");
                clip.Channels.resize(channelCount);
                for (GltfAnimationChannelData& channel : clip.Channels)
                {
                    channel.TargetNode = reader.ReadU32();
                    channel.Path = static_cast<GltfAnimationPath>(reader.ReadU8());
                    channel.Interpolation = static_cast<GltfAnimationInterpolation>(reader.ReadU8());
                    const std::uint32_t keyCount = reader.ReadU32();
                    if (keyCount > MaximumRecords)
                        throw std::length_error("glTF animation channel declares too many keys.");
                    channel.Keyframes.resize(keyCount);
                    for (GltfAnimationKeyframe& key : channel.Keyframes)
                    {
                        key.TimeSeconds = reader.ReadF32();
                        for (float& value : key.Value) value = reader.ReadF32();
                        for (float& value : key.InTangent) value = reader.ReadF32();
                        for (float& value : key.OutTangent) value = reader.ReadF32();
                    }
                }
                scene.Animations.push_back(std::move(clip));
            }
        }

        reader.RequireEnd();
''',
    "skin animation parse",
)

text = replace_once(
    text,
    '''        return { AssetType::Scene, 1u, "kairo.gltf-scene.v1",
            SerializeGltfSceneArtifactData(scene) };
''',
    '''        return { AssetType::Scene, 2u, "kairo.gltf-scene.v2",
            SerializeGltfSceneArtifactData(scene) };
''',
    "derived v2 format",
)
text = replace_once(
    text,
    '''        if (artifact.Type != AssetType::Scene || artifact.FormatVersion != 1u ||
            artifact.Format != "kairo.gltf-scene.v1")
            throw std::invalid_argument("Derived artifact is not a supported Kairo glTF scene.");
        return ParseGltfSceneArtifactData(artifact.Payload);
''',
    '''        const bool v1 = artifact.FormatVersion == 1u &&
            artifact.Format == "kairo.gltf-scene.v1";
        const bool v2 = artifact.FormatVersion == 2u &&
            artifact.Format == "kairo.gltf-scene.v2";
        if (artifact.Type != AssetType::Scene || (!v1 && !v2))
            throw std::invalid_argument("Derived artifact is not a supported Kairo glTF scene.");
        return ParseGltfSceneArtifactData(artifact.Payload);
''',
    "derived parser compatibility",
)

path.write_text(text)

# Focused tests for v2 plus legacy-v1 parsing.
test = Path("tests/GltfSceneArtifactTests.cpp")
test.write_text(r'''#include <catch2/catch_test_macros.hpp>

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
''')

cmake = Path("CMakeLists.txt")
cmake_text = cmake.read_text()
cmake_text = replace_once(
    cmake_text,
    '        tests/MaterialArtifactTests.cpp\n        tests/GLBImporterTests.cpp\n',
    '        tests/MaterialArtifactTests.cpp\n        tests/GltfSceneArtifactTests.cpp\n        tests/GLBImporterTests.cpp\n',
    "CMake glTF artifact tests",
)
cmake.write_text(cmake_text)

Path(".github/workflows/apply-gltf-animation-v2.yml").unlink(missing_ok=True)
Path(".github/scripts/apply_gltf_animation_v2.py").unlink(missing_ok=True)
