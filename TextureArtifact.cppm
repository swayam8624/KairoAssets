module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.TextureArtifact;

import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    enum class TexturePixelFormat : std::uint8_t
    {
        R8 = 1u,
        RG8 = 2u,
        RGBA8 = 3u,
        RGBA16Float = 4u
    };

    enum class TextureColorSpace : std::uint8_t
    {
        Linear = 1u,
        SRGB = 2u
    };

    enum class TextureSemantic : std::uint8_t
    {
        Color = 1u,
        Normal = 2u,
        Data = 3u,
        HDR = 4u
    };

    struct TextureMipLevel final
    {
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::vector<std::byte> Pixels;

        friend bool operator==(const TextureMipLevel&, const TextureMipLevel&) = default;
    };

    struct TextureArtifactData final
    {
        TexturePixelFormat Format = TexturePixelFormat::RGBA8;
        TextureColorSpace ColorSpace = TextureColorSpace::SRGB;
        TextureSemantic Semantic = TextureSemantic::Color;
        std::vector<TextureMipLevel> Mips;

        friend bool operator==(const TextureArtifactData&, const TextureArtifactData&) = default;
    };

    namespace texture_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'T'}, std::byte{'E'}, std::byte{'X'},
            std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::uint32_t MaximumDimension = 32'768u;
        constexpr std::uint32_t MaximumMipLevels = 16u;

        [[nodiscard]] constexpr std::uint32_t BytesPerPixel(TexturePixelFormat format)
        {
            switch (format)
            {
                case TexturePixelFormat::R8: return 1u;
                case TexturePixelFormat::RG8: return 2u;
                case TexturePixelFormat::RGBA8: return 4u;
                case TexturePixelFormat::RGBA16Float: return 8u;
            }
            throw std::invalid_argument("Texture pixel format is unsupported.");
        }

        inline void ValidateEnums(const TextureArtifactData& texture)
        {
            (void)BytesPerPixel(texture.Format);
            switch (texture.ColorSpace)
            {
                case TextureColorSpace::Linear:
                case TextureColorSpace::SRGB: break;
                default: throw std::invalid_argument("Texture colour space is unsupported.");
            }
            switch (texture.Semantic)
            {
                case TextureSemantic::Color:
                case TextureSemantic::Normal:
                case TextureSemantic::Data:
                case TextureSemantic::HDR: break;
                default: throw std::invalid_argument("Texture semantic is unsupported.");
            }
            if ((texture.Semantic == TextureSemantic::Normal || texture.Semantic == TextureSemantic::Data ||
                 texture.Semantic == TextureSemantic::HDR) && texture.ColorSpace != TextureColorSpace::Linear)
                throw std::invalid_argument("Normal, data, and HDR textures must use linear colour space.");
            if (texture.Semantic == TextureSemantic::HDR && texture.Format != TexturePixelFormat::RGBA16Float)
                throw std::invalid_argument("HDR textures require RGBA16Float storage.");
        }
    }

    inline void ValidateTextureArtifactData(const TextureArtifactData& texture)
    {
        using namespace texture_artifact_detail;
        ValidateEnums(texture);
        if (texture.Mips.empty() || texture.Mips.size() > MaximumMipLevels)
            throw std::invalid_argument("Texture artifact requires between 1 and 16 mip levels.");

        const std::uint32_t bytesPerPixel = BytesPerPixel(texture.Format);
        std::uint32_t expectedWidth = texture.Mips.front().Width;
        std::uint32_t expectedHeight = texture.Mips.front().Height;
        if (expectedWidth == 0u || expectedHeight == 0u ||
            expectedWidth > MaximumDimension || expectedHeight > MaximumDimension)
            throw std::invalid_argument("Texture dimensions are outside the supported range.");

        std::size_t totalBytes = 0u;
        for (const TextureMipLevel& mip : texture.Mips)
        {
            if (mip.Width != expectedWidth || mip.Height != expectedHeight)
                throw std::invalid_argument("Texture mip dimensions do not form a canonical chain.");
            const std::uint64_t expectedBytes = std::uint64_t(mip.Width) * mip.Height * bytesPerPixel;
            if (expectedBytes > MaximumDerivedArtifactPayloadBytes || mip.Pixels.size() != expectedBytes)
                throw std::invalid_argument("Texture mip byte size does not match its dimensions and format.");
            if (mip.Pixels.size() > MaximumDerivedArtifactPayloadBytes - totalBytes)
                throw std::length_error("Texture artifact exceeds its payload safety limit.");
            totalBytes += mip.Pixels.size();
            expectedWidth = std::max(1u, expectedWidth / 2u);
            expectedHeight = std::max(1u, expectedHeight / 2u);
        }
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeTextureArtifactData(
        const TextureArtifactData& texture)
    {
        using namespace texture_artifact_detail;
        ValidateTextureArtifactData(texture);
        std::size_t reserve = Magic.size() + 4u + 4u + texture.Mips.size() * 12u;
        for (const TextureMipLevel& mip : texture.Mips) reserve += mip.Pixels.size();
        BinaryWriter writer(reserve);
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        writer.WriteU8(static_cast<std::uint8_t>(texture.Format));
        writer.WriteU8(static_cast<std::uint8_t>(texture.ColorSpace));
        writer.WriteU8(static_cast<std::uint8_t>(texture.Semantic));
        writer.WriteU8(static_cast<std::uint8_t>(texture.Mips.size()));
        for (const TextureMipLevel& mip : texture.Mips)
        {
            writer.WriteU32(mip.Width);
            writer.WriteU32(mip.Height);
            writer.WriteU32(static_cast<std::uint32_t>(mip.Pixels.size()));
            writer.WriteBytes(mip.Pixels);
        }
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline TextureArtifactData ParseTextureArtifactData(
        std::span<const std::byte> payload)
    {
        using namespace texture_artifact_detail;
        BinaryReader reader(payload);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Texture artifact magic is invalid.");
        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("Texture artifact payload version is unsupported.");

        TextureArtifactData texture;
        texture.Format = static_cast<TexturePixelFormat>(reader.ReadU8());
        texture.ColorSpace = static_cast<TextureColorSpace>(reader.ReadU8());
        texture.Semantic = static_cast<TextureSemantic>(reader.ReadU8());
        const std::uint8_t mipCount = reader.ReadU8();
        if (mipCount == 0u || mipCount > MaximumMipLevels)
            throw std::invalid_argument("Texture artifact mip count is invalid.");
        texture.Mips.reserve(mipCount);
        for (std::uint8_t index = 0u; index < mipCount; ++index)
        {
            TextureMipLevel mip;
            mip.Width = reader.ReadU32();
            mip.Height = reader.ReadU32();
            const std::uint32_t byteCount = reader.ReadU32();
            const auto bytes = reader.ReadBytes(byteCount);
            mip.Pixels.assign(bytes.begin(), bytes.end());
            texture.Mips.push_back(std::move(mip));
        }
        reader.RequireEnd();
        ValidateTextureArtifactData(texture);
        return texture;
    }

    [[nodiscard]] inline DerivedArtifact MakeTextureDerivedArtifact(
        const TextureArtifactData& texture)
    {
        return { AssetType::Texture2D, 1u, "kairo.texture2d.v1",
            SerializeTextureArtifactData(texture) };
    }

    [[nodiscard]] inline TextureArtifactData ParseTextureDerivedArtifact(
        const DerivedArtifact& artifact)
    {
        ValidateDerivedArtifact(artifact);
        if (artifact.Type != AssetType::Texture2D || artifact.FormatVersion != 1u ||
            artifact.Format != "kairo.texture2d.v1")
            throw std::invalid_argument("Derived artifact is not a supported Kairo texture.");
        return ParseTextureArtifactData(artifact.Payload);
    }
}
