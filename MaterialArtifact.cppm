module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Assets.MaterialArtifact;

import Kairo.Assets.AssetID;
import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    enum class MaterialAlphaMode : std::uint8_t
    {
        Opaque = 1u,
        Mask = 2u,
        Blend = 3u
    };

    struct MaterialTextureReference final
    {
        AssetID Texture;
        std::uint32_t TexCoord = 0u;
        float Scale = 1.0f;

        friend bool operator==(const MaterialTextureReference&, const MaterialTextureReference&) = default;
    };

    struct MaterialArtifactData final
    {
        std::string Name;
        std::array<float, 4u> BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        std::array<float, 3u> EmissiveFactor{};
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;
        std::optional<MaterialTextureReference> BaseColorTexture;
        std::optional<MaterialTextureReference> MetallicRoughnessTexture;
        std::optional<MaterialTextureReference> NormalTexture;
        std::optional<MaterialTextureReference> OcclusionTexture;
        std::optional<MaterialTextureReference> EmissiveTexture;

        friend bool operator==(const MaterialArtifactData&, const MaterialArtifactData&) = default;
    };

    namespace material_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'M'}, std::byte{'A'}, std::byte{'T'},
            std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::size_t MaximumNameBytes = 4096u;
        constexpr std::uint32_t DoubleSidedFlag = 1u << 0u;
        constexpr std::uint32_t KnownFlags = DoubleSidedFlag;

        inline void WriteAssetID(BinaryWriter& writer, AssetID id)
        {
            writer.WriteBytes(std::as_bytes(std::span(id.Bytes())));
        }

        [[nodiscard]] inline AssetID ReadAssetID(BinaryReader& reader)
        {
            AssetID::Storage bytes{};
            const auto source = reader.ReadBytes(bytes.size());
            for (std::size_t index = 0u; index < bytes.size(); ++index)
                bytes[index] = std::to_integer<std::uint8_t>(source[index]);
            return AssetID{ bytes };
        }

        inline void WriteOptionalTexture(BinaryWriter& writer,
            const std::optional<MaterialTextureReference>& reference)
        {
            writer.WriteU8(reference.has_value() ? 1u : 0u);
            if (!reference.has_value()) return;
            WriteAssetID(writer, reference->Texture);
            writer.WriteU32(reference->TexCoord);
            writer.WriteF32(reference->Scale);
        }

        [[nodiscard]] inline std::optional<MaterialTextureReference>
            ReadOptionalTexture(BinaryReader& reader)
        {
            const std::uint8_t present = reader.ReadU8();
            if (present > 1u)
                throw std::invalid_argument("Material texture presence flag is invalid.");
            if (present == 0u) return std::nullopt;
            MaterialTextureReference reference;
            reference.Texture = ReadAssetID(reader);
            reference.TexCoord = reader.ReadU32();
            reference.Scale = reader.ReadF32();
            return reference;
        }

        [[nodiscard]] inline bool Finite(const std::array<float, 4u>& values) noexcept
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
        }

        [[nodiscard]] inline bool Finite(const std::array<float, 3u>& values) noexcept
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
        }
    }

    inline void ValidateMaterialArtifactData(const MaterialArtifactData& material)
    {
        using namespace material_artifact_detail;
        if (material.Name.size() > MaximumNameBytes)
            throw std::length_error("Material name exceeds its safety limit.");
        if (!Finite(material.BaseColorFactor) || !Finite(material.EmissiveFactor) ||
            !std::isfinite(material.MetallicFactor) ||
            !std::isfinite(material.RoughnessFactor) ||
            !std::isfinite(material.AlphaCutoff))
            throw std::invalid_argument("Material factors must be finite.");
        if (material.MetallicFactor < 0.0f || material.MetallicFactor > 1.0f ||
            material.RoughnessFactor < 0.0f || material.RoughnessFactor > 1.0f ||
            material.AlphaCutoff < 0.0f || material.AlphaCutoff > 1.0f)
            throw std::invalid_argument("Material scalar factors must be normalized.");
        for (float value : material.BaseColorFactor)
            if (value < 0.0f || value > 1.0f)
                throw std::invalid_argument("Base-colour factors must be normalized.");
        for (float value : material.EmissiveFactor)
            if (value < 0.0f)
                throw std::invalid_argument("Emissive factors cannot be negative.");
        switch (material.AlphaMode)
        {
            case MaterialAlphaMode::Opaque:
            case MaterialAlphaMode::Mask:
            case MaterialAlphaMode::Blend: break;
            default: throw std::invalid_argument("Material alpha mode is invalid.");
        }

        const auto validateReference = [](const std::optional<MaterialTextureReference>& reference)
        {
            if (!reference.has_value()) return;
            if (!reference->Texture.IsValid())
                throw std::invalid_argument("Material texture references require valid asset IDs.");
            if (reference->TexCoord > 7u)
                throw std::invalid_argument("Material texture coordinate set is outside the supported range.");
            if (!std::isfinite(reference->Scale) || reference->Scale < 0.0f)
                throw std::invalid_argument("Material texture scale must be finite and non-negative.");
        };
        validateReference(material.BaseColorTexture);
        validateReference(material.MetallicRoughnessTexture);
        validateReference(material.NormalTexture);
        validateReference(material.OcclusionTexture);
        validateReference(material.EmissiveTexture);
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeMaterialArtifactData(
        const MaterialArtifactData& material)
    {
        using namespace material_artifact_detail;
        ValidateMaterialArtifactData(material);
        BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        writer.WriteU32(static_cast<std::uint32_t>(material.Name.size()));
        writer.WriteText(material.Name);
        for (float value : material.BaseColorFactor) writer.WriteF32(value);
        writer.WriteF32(material.MetallicFactor);
        writer.WriteF32(material.RoughnessFactor);
        for (float value : material.EmissiveFactor) writer.WriteF32(value);
        writer.WriteU8(static_cast<std::uint8_t>(material.AlphaMode));
        writer.WriteF32(material.AlphaCutoff);
        writer.WriteU32(material.DoubleSided ? DoubleSidedFlag : 0u);
        WriteOptionalTexture(writer, material.BaseColorTexture);
        WriteOptionalTexture(writer, material.MetallicRoughnessTexture);
        WriteOptionalTexture(writer, material.NormalTexture);
        WriteOptionalTexture(writer, material.OcclusionTexture);
        WriteOptionalTexture(writer, material.EmissiveTexture);
        if (writer.Bytes().size() > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("Material artifact exceeds its payload safety limit.");
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline MaterialArtifactData ParseMaterialArtifactData(
        std::span<const std::byte> payload)
    {
        using namespace material_artifact_detail;
        BinaryReader reader(payload);
        if (!std::equal(Magic.begin(), Magic.end(), reader.ReadBytes(Magic.size()).begin()))
            throw std::invalid_argument("Material artifact magic is invalid.");
        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("Material artifact payload version is unsupported.");

        MaterialArtifactData material;
        const std::uint32_t nameBytes = reader.ReadU32();
        if (nameBytes > MaximumNameBytes)
            throw std::length_error("Material name exceeds its safety limit.");
        material.Name = reader.ReadText(nameBytes);
        for (float& value : material.BaseColorFactor) value = reader.ReadF32();
        material.MetallicFactor = reader.ReadF32();
        material.RoughnessFactor = reader.ReadF32();
        for (float& value : material.EmissiveFactor) value = reader.ReadF32();
        material.AlphaMode = static_cast<MaterialAlphaMode>(reader.ReadU8());
        material.AlphaCutoff = reader.ReadF32();
        const std::uint32_t flags = reader.ReadU32();
        if ((flags & ~KnownFlags) != 0u)
            throw std::invalid_argument("Material artifact flags are invalid.");
        material.DoubleSided = (flags & DoubleSidedFlag) != 0u;
        material.BaseColorTexture = ReadOptionalTexture(reader);
        material.MetallicRoughnessTexture = ReadOptionalTexture(reader);
        material.NormalTexture = ReadOptionalTexture(reader);
        material.OcclusionTexture = ReadOptionalTexture(reader);
        material.EmissiveTexture = ReadOptionalTexture(reader);
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
