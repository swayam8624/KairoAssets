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

    struct GltfPrimitiveData final
    {
        MeshArtifactData Mesh;
        std::vector<std::array<float, 4u>> Tangents;
        std::uint32_t MaterialIndex = std::numeric_limits<std::uint32_t>::max();

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

        friend bool operator==(const GltfNodeData&, const GltfNodeData&) = default;
    };

    struct GltfSceneArtifactData final
    {
        std::vector<GltfMaterialData> Materials;
        std::vector<GltfPrimitiveData> Primitives;
        std::vector<GltfNodeData> Nodes;
        std::vector<std::uint32_t> RootNodes;

        friend bool operator==(const GltfSceneArtifactData&, const GltfSceneArtifactData&) = default;
    };

    namespace gltf_scene_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'G'}, std::byte{'L'}, std::byte{'T'},
            std::byte{'F'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::size_t MaximumNameBytes = 4096u;
        constexpr std::size_t MaximumUriBytes = 64u * 1024u;
        constexpr std::uint32_t MaximumRecords = 1'000'000u;
        constexpr std::uint32_t MissingIndex = std::numeric_limits<std::uint32_t>::max();

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
            scene.Nodes.size() > MaximumRecords || scene.RootNodes.size() > MaximumRecords)
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
        }

        for (const GltfNodeData& node : scene.Nodes)
        {
            WriteString(writer, node.Name, MaximumNameBytes, "glTF node name");
            writer.WriteU32(std::bit_cast<std::uint32_t>(node.Parent));
            for (float value : node.LocalTransform) writer.WriteF32(value);
            writer.WriteU32(static_cast<std::uint32_t>(node.PrimitiveIndices.size()));
            for (const std::uint32_t primitive : node.PrimitiveIndices) writer.WriteU32(primitive);
        }
        for (const std::uint32_t root : scene.RootNodes) writer.WriteU32(root);

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
        if (reader.ReadU32() != PayloadVersion)
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
            scene.Nodes.push_back(std::move(node));
        }
        for (std::uint32_t index = 0u; index < rootCount; ++index)
            scene.RootNodes.push_back(reader.ReadU32());

        reader.RequireEnd();
        ValidateGltfSceneArtifactData(scene);
        return scene;
    }

    [[nodiscard]] inline DerivedArtifact MakeGltfSceneDerivedArtifact(
        const GltfSceneArtifactData& scene)
    {
        return { AssetType::Scene, 1u, "kairo.gltf-scene.v1",
            SerializeGltfSceneArtifactData(scene) };
    }

    [[nodiscard]] inline GltfSceneArtifactData ParseGltfSceneDerivedArtifact(
        const DerivedArtifact& artifact)
    {
        ValidateDerivedArtifact(artifact);
        if (artifact.Type != AssetType::Scene || artifact.FormatVersion != 1u ||
            artifact.Format != "kairo.gltf-scene.v1")
            throw std::invalid_argument("Derived artifact is not a supported Kairo glTF scene.");
        return ParseGltfSceneArtifactData(artifact.Payload);
    }
}
