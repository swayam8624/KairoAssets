module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.MaterialArtifact;

import Kairo.Assets.AssetID;
import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Metadata;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    enum class MaterialAlphaMode : std::uint8_t
    {
        Opaque = 1u,
        Mask = 2u,
        Blend = 3u
    };

    struct MaterialTextureSlots final
    {
        std::optional<TextureAssetHandle> BaseColor;
        std::optional<TextureAssetHandle> Normal;
        std::optional<TextureAssetHandle> MetallicRoughness;
        std::optional<TextureAssetHandle> Emissive;
        std::optional<TextureAssetHandle> Occlusion;

        friend bool operator==(const MaterialTextureSlots& left, const MaterialTextureSlots& right) noexcept
        {
            const auto same = [](const std::optional<TextureAssetHandle>& a,
                const std::optional<TextureAssetHandle>& b) noexcept
            {
                if (a.has_value() != b.has_value()) return false;
                return !a.has_value() || a->ID == b->ID;
            };
            return same(left.BaseColor, right.BaseColor) &&
                same(left.Normal, right.Normal) &&
                same(left.MetallicRoughness, right.MetallicRoughness) &&
                same(left.Emissive, right.Emissive) &&
                same(left.Occlusion, right.Occlusion);
        }
    };

    struct MaterialArtifactData final
    {
        std::array<float, 4u> BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        std::array<float, 3u> EmissiveFactor{};
        float NormalScale = 1.0f;
        float OcclusionStrength = 1.0f;
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;
        MaterialTextureSlots Textures;

        friend bool operator==(const MaterialArtifactData& left, const MaterialArtifactData& right) noexcept
        {
            for (std::size_t index = 0u; index < left.BaseColorFactor.size(); ++index)
                if (left.BaseColorFactor[index] != right.BaseColorFactor[index]) return false;
            for (std::size_t index = 0u; index < left.EmissiveFactor.size(); ++index)
                if (left.EmissiveFactor[index] != right.EmissiveFactor[index]) return false;
            return left.MetallicFactor == right.MetallicFactor &&
                left.RoughnessFactor == right.RoughnessFactor &&
                left.NormalScale == right.NormalScale &&
                left.OcclusionStrength == right.OcclusionStrength &&
                left.AlphaMode == right.AlphaMode &&
                left.AlphaCutoff == right.AlphaCutoff &&
                left.DoubleSided == right.DoubleSided &&
                left.Textures == right.Textures;
        }
    };

    namespace material_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'M'}, std::byte{'A'}, std::byte{'T'},
            std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::uint8_t DoubleSidedFlag = 1u << 0u;

        [[nodiscard]] inline bool Finite(std::span<const float> values) noexcept
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
        }

        inline void WriteTexture(BinaryWriter& writer, const std::optional<TextureAssetHandle>& texture)
        {
            writer.WriteU8(texture.has_value() ? 1u : 0u);
            if (texture.has_value())
                for (const std::uint8_t byte : texture->ID.Bytes()) writer.WriteU8(byte);
        }

        [[nodiscard]] inline std::optional<TextureAssetHandle> ReadTexture(BinaryReader& reader)
        {
            const std::uint8_t present = reader.ReadU8();
            if (present > 1u) throw std::invalid_argument("Material texture presence flag is invalid.");
            if (present == 0u) return std::nullopt;
            AssetID::Storage bytes{};
            for (std::uint8_t& byte : bytes) byte = reader.ReadU8();
            TextureAssetHandle handle{ AssetID{ bytes } };
            if (!handle.IsValid()) throw std::invalid_argument("Material texture reference cannot be invalid.");
            return handle;
        }
    }

    inline void ValidateMaterialArtifactData(const MaterialArtifactData& material)
    {
        using namespace material_artifact_detail;
        if (!Finite(material.BaseColorFactor) || !Finite(material.EmissiveFactor) ||
            !std::isfinite(material.MetallicFactor) || !std::isfinite(material.RoughnessFactor) ||
            !std::isfinite(material.NormalScale) || !std::isfinite(material.OcclusionStrength) ||
            !std::isfinite(material.AlphaCutoff))
            throw std::invalid_argument("Material scalar and vector values must be finite.");
        for (const float value : material.BaseColorFactor)
            if (value < 0.0f || value > 1.0f)
                throw std::invalid_argument("Material base-colour factor must be in [0, 1].");
        if (material.MetallicFactor < 0.0f || material.MetallicFactor > 1.0f ||
            material.RoughnessFactor < 0.0f || material.RoughnessFactor > 1.0f)
            throw std::invalid_argument("Material metallic and roughness factors must be in [0, 1].");
        for (const float value : material.EmissiveFactor)
            if (value < 0.0f) throw std::invalid_argument("Material emissive factor cannot be negative.");
        if (material.NormalScale < 0.0f)
            throw std::invalid_argument("Material normal scale cannot be negative.");
        if (material.OcclusionStrength < 0.0f || material.OcclusionStrength > 1.0f)
            throw std::invalid_argument("Material occlusion strength must be in [0, 1].");
        if (material.AlphaCutoff < 0.0f || material.AlphaCutoff > 1.0f)
            throw std::invalid_argument("Material alpha cutoff must be in [0, 1].");
        switch (material.AlphaMode)
        {
            case MaterialAlphaMode::Opaque:
            case MaterialAlphaMode::Mask:
            case MaterialAlphaMode::Blend: break;
            default: throw std::invalid_argument("Material alpha mode is unsupported.");
        }
        const std::array slots{
            &material.Textures.BaseColor, &material.Textures.Normal,
            &material.Textures.MetallicRoughness, &material.Textures.Emissive,
            &material.Textures.Occlusion };
        for (const auto* slot : slots)
            if (slot->has_value() && !slot->value().IsValid())
                throw std::invalid_argument("Material texture handles must be valid.");
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeMaterialArtifactData(
        const MaterialArtifactData& material)
    {
        using namespace material_artifact_detail;
        ValidateMaterialArtifactData(material);
        BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        for (const float value : material.BaseColorFactor) writer.WriteF32(value);
        writer.WriteF32(material.MetallicFactor);
        writer.WriteF32(material.RoughnessFactor);
        for (const float value : material.EmissiveFactor) writer.WriteF32(value);
        writer.WriteF32(material.NormalScale);
        writer.WriteF32(material.OcclusionStrength);
        writer.WriteU8(static_cast<std::uint8_t>(material.AlphaMode));
        writer.WriteF32(material.AlphaCutoff);
        writer.WriteU8(material.DoubleSided ? DoubleSidedFlag : 0u);
        WriteTexture(writer, material.Textures.BaseColor);
        WriteTexture(writer, material.Textures.Normal);
        WriteTexture(writer, material.Textures.MetallicRoughness);
        WriteTexture(writer, material.Textures.Emissive);
        WriteTexture(writer, material.Textures.Occlusion);
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline MaterialArtifactData ParseMaterialArtifactData(
        std::span<const std::byte> payload)
    {
        using namespace material_artifact_detail;
        BinaryReader reader(payload);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Material artifact magic is invalid.");
        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("Material artifact payload version is unsupported.");
        MaterialArtifactData material;
        for (float& value : material.BaseColorFactor) value = reader.ReadF32();
        material.MetallicFactor = reader.ReadF32();
        material.RoughnessFactor = reader.ReadF32();
        for (float& value : material.EmissiveFactor) value = reader.ReadF32();
        material.NormalScale = reader.ReadF32();
        material.OcclusionStrength = reader.ReadF32();
        material.AlphaMode = static_cast<MaterialAlphaMode>(reader.ReadU8());
        material.AlphaCutoff = reader.ReadF32();
        const std::uint8_t flags = reader.ReadU8();
        if ((flags & ~DoubleSidedFlag) != 0u)
            throw std::invalid_argument("Material artifact flags are invalid.");
        material.DoubleSided = (flags & DoubleSidedFlag) != 0u;
        material.Textures.BaseColor = ReadTexture(reader);
        material.Textures.Normal = ReadTexture(reader);
        material.Textures.MetallicRoughness = ReadTexture(reader);
        material.Textures.Emissive = ReadTexture(reader);
        material.Textures.Occlusion = ReadTexture(reader);
        reader.RequireEnd();
        ValidateMaterialArtifactData(material);
        return material;
    }

    [[nodiscard]] inline DerivedArtifact MakeMaterialDerivedArtifact(
        const MaterialArtifactData& material)
    {
        return { AssetType::Material, 1u, "kairo.material.v1",
            SerializeMaterialArtifactData(material) };
    }

    [[nodiscard]] inline MaterialArtifactData ParseMaterialDerivedArtifact(
        const DerivedArtifact& artifact)
    {
        ValidateDerivedArtifact(artifact);
        if (artifact.Type != AssetType::Material || artifact.FormatVersion != 1u ||
            artifact.Format != "kairo.material.v1")
            throw std::invalid_argument("Derived artifact is not a supported Kairo material.");
        return ParseMaterialArtifactData(artifact.Payload);
    }
}
