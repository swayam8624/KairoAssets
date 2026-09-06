module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Assets.GltfSceneArtifact;

import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.MeshArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    enum class GltfAlphaMode : std::uint8_t
    {
        Opaque = 1u,
        Mask = 2u,
        Blend = 3u
    };

    struct GltfTextureBinding final
    {
        std::string Uri;
        std::uint32_t TexCoord = 0u;
        float Scale = 1.0f;

        friend bool operator==(const GltfTextureBinding&, const GltfTextureBinding&) = default;
    };

    struct GltfMaterialData final
    {
        std::string Name;
        std::array<float, 4u> BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        std::array<float, 3u> EmissiveFactor{};
        GltfAlphaMode AlphaMode = GltfAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;
        GltfTextureBinding BaseColorTexture;
        GltfTextureBinding MetallicRoughnessTexture;
        GltfTextureBinding NormalTexture;
        GltfTextureBinding OcclusionTexture;
        GltfTextureBinding EmissiveTexture;

        friend bool operator==(const GltfMaterialData&, const GltfMaterialData&) = default;
    };

    inline constexpr std::uint32_t GltfMissingIndex =
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

    struct GltfNodeData final
    {
        std::string Name;
        std::int32_t Parent = -1;
        std::array<float, 16u> LocalTransform{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
        std::vector<std::uint32_t> PrimitiveIndices;
        std::uint32_t SkinIndex = GltfMissingIndex;

        friend bool operator==(const GltfNodeData&, const GltfNodeData&) = default;
    };

    struct GltfSceneArtifactData final
    {
        std::vector<GltfMaterialData> Materials;
        std::vector<GltfPrimitiveData> Primitives;
        std::vector<GltfNodeData> Nodes;
        std::vector<std::uint32_t> RootNodes;
        std::vector<GltfSkinData> Skins;
        std::vector<GltfAnimationClipData> Animations;

        friend bool operator==(const GltfSceneArtifactData&, const GltfSceneArtifactData&) = default;
    };

    namespace gltf_scene_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'G'}, std::byte{'L'}, std::byte{'T'},
            std::byte{'F'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t LegacyPayloadVersion = 1u;
        constexpr std::uint32_t PayloadVersion = 2u;
        constexpr std::size_t MaximumNameBytes = 4096u;
        constexpr std::size_t MaximumUriBytes = 64u * 1024u;
        constexpr std::uint32_t MaximumRecords = 1'000'000u;
        constexpr std::uint32_t MissingIndex = GltfMissingIndex;

        inline void WriteString(BinaryWriter& writer, std::string_view value,
            std::size_t maximumBytes, const char* role)
        {
            if (value.size() > maximumBytes)
                throw std::length_error(std::string(role) + " exceeds its safety limit.");
            writer.WriteU32(static_cast<std::uint32_t>(value.size()));
            writer.WriteText(value);
        }

        [[nodiscard]] inline std::string ReadString(BinaryReader& reader,
            std::size_t maximumBytes, const char* role)
        {
            const std::uint32_t size = reader.ReadU32();
            if (size > maximumBytes)
                throw std::length_error(std::string(role) + " exceeds its safety limit.");
            return reader.ReadText(size);
        }

        inline void WriteTextureBinding(BinaryWriter& writer, const GltfTextureBinding& binding)
        {
            WriteString(writer, binding.Uri, MaximumUriBytes, "glTF texture URI");
            writer.WriteU32(binding.TexCoord);
            writer.WriteF32(binding.Scale);
        }

        [[nodiscard]] inline GltfTextureBinding ReadTextureBinding(BinaryReader& reader)
        {
            GltfTextureBinding binding;
            binding.Uri = ReadString(reader, MaximumUriBytes, "glTF texture URI");
            binding.TexCoord = reader.ReadU32();
            binding.Scale = reader.ReadF32();
            return binding;
        }

        [[nodiscard]] inline bool Finite(std::span<const float> values) noexcept
        {
            return std::all_of(values.begin(), values.end(),
                [](float value) { return std::isfinite(value); });
        }
    }

    inline void ValidateGltfSceneArtifactData(const GltfSceneArtifactData& scene)
    {
        using namespace gltf_scene_artifact_detail;
        if (scene.Materials.size() > MaximumRecords || scene.Primitives.size() > MaximumRecords ||
            scene.Nodes.size() > MaximumRecords || scene.RootNodes.size() > MaximumRecords ||
            scene.Skins.size() > MaximumRecords || scene.Animations.size() > MaximumRecords)
            throw std::length_error("glTF scene exceeds its record safety limit.");
        if (scene.Primitives.empty())
            throw std::invalid_argument("glTF scene requires at least one triangle primitive.");

        const auto validateBinding = [](const GltfTextureBinding& binding)
        {
            if (binding.Uri.size() > MaximumUriBytes)
                throw std::length_error("glTF texture URI exceeds its safety limit.");
            if (binding.TexCoord > 7u)
                throw std::invalid_argument("glTF texture coordinate set is outside the supported range.");
            if (!std::isfinite(binding.Scale) || binding.Scale < 0.0f)
                throw std::invalid_argument("glTF texture scale must be finite and non-negative.");
        };

        for (const GltfMaterialData& material : scene.Materials)
        {
            if (material.Name.size() > MaximumNameBytes)
                throw std::length_error("glTF material name exceeds its safety limit.");
            if (!Finite(material.BaseColorFactor) || !Finite(material.EmissiveFactor) ||
                !std::isfinite(material.MetallicFactor) ||
                !std::isfinite(material.RoughnessFactor) ||
                !std::isfinite(material.AlphaCutoff))
                throw std::invalid_argument("glTF material factors must be finite.");
            if (material.MetallicFactor < 0.0f || material.MetallicFactor > 1.0f ||
                material.RoughnessFactor < 0.0f || material.RoughnessFactor > 1.0f ||
                material.AlphaCutoff < 0.0f || material.AlphaCutoff > 1.0f)
                throw std::invalid_argument("glTF material scalar factors must be normalized.");
            switch (material.AlphaMode)
            {
                case GltfAlphaMode::Opaque:
                case GltfAlphaMode::Mask:
                case GltfAlphaMode::Blend: break;
                default: throw std::invalid_argument("glTF material alpha mode is invalid.");
            }
            validateBinding(material.BaseColorTexture);
            validateBinding(material.MetallicRoughnessTexture);
            validateBinding(material.NormalTexture);
            validateBinding(material.OcclusionTexture);
            validateBinding(material.EmissiveTexture);
        }

        for (const GltfPrimitiveData& primitive : scene.Primitives)
        {
            ValidateMeshArtifactData(primitive.Mesh);
            if (!primitive.Tangents.empty())
            {
                if (primitive.Tangents.size() != primitive.Mesh.Vertices.size())
                    throw std::invalid_argument("glTF tangent count must match the vertex count.");
                for (const auto& tangent : primitive.Tangents)
                {
                    if (!Finite(tangent))
                        throw std::invalid_argument("glTF tangents must be finite.");
                    const float lengthSquared = tangent[0] * tangent[0] +
                        tangent[1] * tangent[1] + tangent[2] * tangent[2];
                    if (std::abs(lengthSquared - 1.0f) > 1.0e-3f ||
                        (tangent[3] != -1.0f && tangent[3] != 1.0f))
                        throw std::invalid_argument("glTF tangents must contain a unit direction and handedness.");
                }
            }
            if (primitive.MaterialIndex != MissingIndex &&
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
        {
            const GltfNodeData& node = scene.Nodes[nodeIndex];
            if (node.Name.size() > MaximumNameBytes)
                throw std::length_error("glTF node name exceeds its safety limit.");
            if (!Finite(node.LocalTransform))
                throw std::invalid_argument("glTF node transforms must be finite.");
            if (node.Parent < -1 || (node.Parent >= 0 &&
                static_cast<std::size_t>(node.Parent) >= scene.Nodes.size()))
                throw std::out_of_range("glTF node parent index is invalid.");
            if (node.Parent == static_cast<std::int32_t>(nodeIndex))
                throw std::invalid_argument("glTF node cannot parent itself.");
            for (const std::uint32_t primitive : node.PrimitiveIndices)
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
        for (const std::uint32_t root : scene.RootNodes)
        {
            if (root >= scene.Nodes.size())
                throw std::out_of_range("glTF root node index is invalid.");
            if (scene.Nodes[root].Parent != -1)
                throw std::invalid_argument("glTF root node cannot have a parent.");
        }

        for (std::size_t nodeIndex = 0u; nodeIndex < scene.Nodes.size(); ++nodeIndex)
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

    [[nodiscard]] inline std::vector<std::byte> SerializeGltfSceneArtifactData(
        const GltfSceneArtifactData& scene)
    {
        using namespace gltf_scene_artifact_detail;
        ValidateGltfSceneArtifactData(scene);
        BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        writer.WriteU32(static_cast<std::uint32_t>(scene.Materials.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.Primitives.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.Nodes.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.RootNodes.size()));

        for (const GltfMaterialData& material : scene.Materials)
        {
            WriteString(writer, material.Name, MaximumNameBytes, "glTF material name");
            for (float value : material.BaseColorFactor) writer.WriteF32(value);
            writer.WriteF32(material.MetallicFactor);
            writer.WriteF32(material.RoughnessFactor);
            for (float value : material.EmissiveFactor) writer.WriteF32(value);
            writer.WriteU8(static_cast<std::uint8_t>(material.AlphaMode));
            writer.WriteF32(material.AlphaCutoff);
            writer.WriteU8(material.DoubleSided ? 1u : 0u);
            WriteTextureBinding(writer, material.BaseColorTexture);
            WriteTextureBinding(writer, material.MetallicRoughnessTexture);
            WriteTextureBinding(writer, material.NormalTexture);
            WriteTextureBinding(writer, material.OcclusionTexture);
            WriteTextureBinding(writer, material.EmissiveTexture);
        }

        for (const GltfPrimitiveData& primitive : scene.Primitives)
        {
            const std::vector<std::byte> mesh = SerializeMeshArtifactData(primitive.Mesh);
            writer.WriteU64(static_cast<std::uint64_t>(mesh.size()));
            writer.WriteBytes(mesh);
            writer.WriteU32(static_cast<std::uint32_t>(primitive.Tangents.size()));
            for (const auto& tangent : primitive.Tangents)
                for (float value : tangent) writer.WriteF32(value);
            writer.WriteU32(primitive.MaterialIndex);
            writer.WriteU32(static_cast<std::uint32_t>(primitive.Skinning.size()));
            for (const GltfVertexSkinData& influence : primitive.Skinning)
            {
                for (std::uint32_t joint : influence.Joints) writer.WriteU32(joint);
                for (float weight : influence.Weights) writer.WriteF32(weight);
            }
        }

        for (const GltfNodeData& node : scene.Nodes)
        {
            WriteString(writer, node.Name, MaximumNameBytes, "glTF node name");
            writer.WriteU32(std::bit_cast<std::uint32_t>(node.Parent));
            for (float value : node.LocalTransform) writer.WriteF32(value);
            writer.WriteU32(static_cast<std::uint32_t>(node.PrimitiveIndices.size()));
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
            throw std::length_error("glTF scene artifact exceeds its payload safety limit.");
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline GltfSceneArtifactData ParseGltfSceneArtifactData(
        std::span<const std::byte> payload)
    {
        using namespace gltf_scene_artifact_detail;
        BinaryReader reader(payload);
        if (!std::equal(Magic.begin(), Magic.end(), reader.ReadBytes(Magic.size()).begin()))
            throw std::invalid_argument("glTF scene artifact magic is invalid.");
        const std::uint32_t payloadVersion = reader.ReadU32();
        if (payloadVersion != LegacyPayloadVersion && payloadVersion != PayloadVersion)
            throw std::invalid_argument("glTF scene artifact version is unsupported.");

        const std::uint32_t materialCount = reader.ReadU32();
        const std::uint32_t primitiveCount = reader.ReadU32();
        const std::uint32_t nodeCount = reader.ReadU32();
        const std::uint32_t rootCount = reader.ReadU32();
        if (materialCount > MaximumRecords || primitiveCount > MaximumRecords ||
            nodeCount > MaximumRecords || rootCount > MaximumRecords)
            throw std::length_error("glTF scene declares too many records.");

        GltfSceneArtifactData scene;
        scene.Materials.reserve(materialCount);
        scene.Primitives.reserve(primitiveCount);
        scene.Nodes.reserve(nodeCount);
        scene.RootNodes.reserve(rootCount);

        for (std::uint32_t index = 0u; index < materialCount; ++index)
        {
            GltfMaterialData material;
            material.Name = ReadString(reader, MaximumNameBytes, "glTF material name");
            for (float& value : material.BaseColorFactor) value = reader.ReadF32();
            material.MetallicFactor = reader.ReadF32();
            material.RoughnessFactor = reader.ReadF32();
            for (float& value : material.EmissiveFactor) value = reader.ReadF32();
            material.AlphaMode = static_cast<GltfAlphaMode>(reader.ReadU8());
            material.AlphaCutoff = reader.ReadF32();
            const std::uint8_t doubleSided = reader.ReadU8();
            if (doubleSided > 1u)
                throw std::invalid_argument("glTF double-sided flag is invalid.");
            material.DoubleSided = doubleSided != 0u;
            material.BaseColorTexture = ReadTextureBinding(reader);
            material.MetallicRoughnessTexture = ReadTextureBinding(reader);
            material.NormalTexture = ReadTextureBinding(reader);
            material.OcclusionTexture = ReadTextureBinding(reader);
            material.EmissiveTexture = ReadTextureBinding(reader);
            scene.Materials.push_back(std::move(material));
        }

        for (std::uint32_t index = 0u; index < primitiveCount; ++index)
        {
            const std::uint64_t meshBytes = reader.ReadU64();
            if (meshBytes > reader.Remaining())
                throw std::invalid_argument("glTF primitive mesh payload is truncated.");
            GltfPrimitiveData primitive;
            primitive.Mesh = ParseMeshArtifactData(
                reader.ReadBytes(static_cast<std::size_t>(meshBytes)));
            const std::uint32_t tangentCount = reader.ReadU32();
            if (tangentCount > MaximumRecords)
                throw std::length_error("glTF primitive declares too many tangents.");
            primitive.Tangents.resize(tangentCount);
            for (auto& tangent : primitive.Tangents)
                for (float& value : tangent) value = reader.ReadF32();
            primitive.MaterialIndex = reader.ReadU32();
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
        }

        for (std::uint32_t index = 0u; index < nodeCount; ++index)
        {
            GltfNodeData node;
            node.Name = ReadString(reader, MaximumNameBytes, "glTF node name");
            node.Parent = std::bit_cast<std::int32_t>(reader.ReadU32());
            for (float& value : node.LocalTransform) value = reader.ReadF32();
            const std::uint32_t nodePrimitiveCount = reader.ReadU32();
            if (nodePrimitiveCount > MaximumRecords)
                throw std::length_error("glTF node declares too many primitives.");
            node.PrimitiveIndices.resize(nodePrimitiveCount);
            for (std::uint32_t& primitive : node.PrimitiveIndices)
                primitive = reader.ReadU32();
            if (payloadVersion >= PayloadVersion) node.SkinIndex = reader.ReadU32();
            scene.Nodes.push_back(std::move(node));
        }
        for (std::uint32_t index = 0u; index < rootCount; ++index)
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
        ValidateGltfSceneArtifactData(scene);
        return scene;
    }

    [[nodiscard]] inline DerivedArtifact MakeGltfSceneDerivedArtifact(
        const GltfSceneArtifactData& scene)
    {
        return { AssetType::Scene, 2u, "kairo.gltf-scene.v2",
            SerializeGltfSceneArtifactData(scene) };
    }

    [[nodiscard]] inline GltfSceneArtifactData ParseGltfSceneDerivedArtifact(
        const DerivedArtifact& artifact)
    {
        ValidateDerivedArtifact(artifact);
        const bool v1 = artifact.FormatVersion == 1u &&
            artifact.Format == "kairo.gltf-scene.v1";
        const bool v2 = artifact.FormatVersion == 2u &&
            artifact.Format == "kairo.gltf-scene.v2";
        if (artifact.Type != AssetType::Scene || (!v1 && !v2))
            throw std::invalid_argument("Derived artifact is not a supported Kairo glTF scene.");
        return ParseGltfSceneArtifactData(artifact.Payload);
    }
}
