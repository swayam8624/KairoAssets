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
        RGBA8 = 1u,
        RGBA32Float = 2u
    };

    enum class TextureColorSpace : std::uint8_t
    {
        Linear = 1u,
        SRGB = 2u
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
        bool NormalMap = false;
        std::vector<TextureMipLevel> Mips;

        friend bool operator==(const TextureArtifactData&, const TextureArtifactData&) = default;
    };

    namespace texture_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'T'}, std::byte{'E'}, std::byte{'X'},
            std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::uint32_t NormalMapFlag = 1u << 0u;
        constexpr std::uint32_t KnownFlags = NormalMapFlag;
        constexpr std::uint32_t MaximumDimension = 32'768u;
        constexpr std::uint32_t MaximumMipCount = 32u;

        [[nodiscard]] constexpr std::size_t BytesPerPixel(TexturePixelFormat format)
        {
            switch (format)
            {
                case TexturePixelFormat::RGBA8: return 4u;
                case TexturePixelFormat::RGBA32Float: return 16u;
            }
            throw std::invalid_argument("Texture artifact pixel format is unsupported.");
        }

        [[nodiscard]] inline std::size_t CheckedPixelBytes(
            std::uint32_t width, std::uint32_t height, TexturePixelFormat format)
        {
            if (width == 0u || height == 0u || width > MaximumDimension || height > MaximumDimension)
                throw std::invalid_argument("Texture dimensions are outside the supported range.");
            const std::size_t bpp = BytesPerPixel(format);
            if (static_cast<std::uint64_t>(width) * height >
                static_cast<std::uint64_t>(MaximumDerivedArtifactPayloadBytes / bpp))
                throw std::length_error("Texture pixel payload exceeds its safety limit.");
            return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bpp;
        }

        [[nodiscard]] inline float SrgbToLinear(float value) noexcept
        {
            return value <= 0.04045f ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        [[nodiscard]] inline float LinearToSrgb(float value) noexcept
        {
            value = std::clamp(value, 0.0f, 1.0f);
            return value <= 0.0031308f ? value * 12.92f
                : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        [[nodiscard]] inline std::uint8_t ToByte(float value) noexcept
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        [[nodiscard]] inline float ReadFloat(const std::byte* bytes) noexcept
        {
            std::uint32_t bits = 0u;
            for (std::size_t index = 0u; index < 4u; ++index)
                bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index]))
                    << (index * 8u);
            return std::bit_cast<float>(bits);
        }

        inline void WriteFloat(std::byte* bytes, float value) noexcept
        {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
            for (std::size_t index = 0u; index < 4u; ++index)
                bytes[index] = std::byte{ static_cast<std::uint8_t>(bits >> (index * 8u)) };
        }
    }

    inline void ValidateTextureArtifactData(const TextureArtifactData& texture)
    {
        using namespace texture_artifact_detail;
        if (texture.Mips.empty() || texture.Mips.size() > MaximumMipCount)
            throw std::invalid_argument("Texture artifact requires a bounded mip chain.");
        if (texture.NormalMap && texture.ColorSpace != TextureColorSpace::Linear)
            throw std::invalid_argument("Normal maps must be stored in linear colour space.");

        std::uint32_t expectedWidth = texture.Mips.front().Width;
        std::uint32_t expectedHeight = texture.Mips.front().Height;
        for (std::size_t mipIndex = 0u; mipIndex < texture.Mips.size(); ++mipIndex)
        {
            const TextureMipLevel& mip = texture.Mips[mipIndex];
            if (mip.Width != expectedWidth || mip.Height != expectedHeight)
                throw std::invalid_argument("Texture mip dimensions are not canonical.");
            if (mip.Pixels.size() != CheckedPixelBytes(mip.Width, mip.Height, texture.Format))
                throw std::invalid_argument("Texture mip byte count does not match its dimensions.");

            if (texture.Format == TexturePixelFormat::RGBA32Float)
            {
                for (std::size_t offset = 0u; offset < mip.Pixels.size(); offset += 4u)
                    if (!std::isfinite(ReadFloat(mip.Pixels.data() + offset)))
                        throw std::invalid_argument("Float texture pixels must be finite.");
            }

            expectedWidth = std::max<std::uint32_t>(1u, expectedWidth / 2u);
            expectedHeight = std::max<std::uint32_t>(1u, expectedHeight / 2u);
        }
        if (texture.Mips.size() > 1u &&
            (texture.Mips.back().Width != 1u || texture.Mips.back().Height != 1u))
            throw std::invalid_argument("Multi-level texture mip chains must terminate at 1x1.");
    }

    [[nodiscard]] inline TextureArtifactData BuildTextureMipChainRGBA8(
        std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba,
        TextureColorSpace colorSpace = TextureColorSpace::SRGB, bool normalMap = false)
    {
        using namespace texture_artifact_detail;
        if (rgba.size() != CheckedPixelBytes(width, height, TexturePixelFormat::RGBA8))
            throw std::invalid_argument("Base texture byte count does not match its dimensions.");
        if (normalMap && colorSpace != TextureColorSpace::Linear)
            throw std::invalid_argument("Normal-map mip generation requires linear colour space.");

        TextureArtifactData result;
        result.Format = TexturePixelFormat::RGBA8;
        result.ColorSpace = colorSpace;
        result.NormalMap = normalMap;
        result.Mips.push_back({ width, height, { rgba.begin(), rgba.end() } });

        while (width != 1u || height != 1u)
        {
            const TextureMipLevel& source = result.Mips.back();
            const std::uint32_t nextWidth = std::max<std::uint32_t>(1u, width / 2u);
            const std::uint32_t nextHeight = std::max<std::uint32_t>(1u, height / 2u);
            TextureMipLevel next;
            next.Width = nextWidth;
            next.Height = nextHeight;
            next.Pixels.resize(CheckedPixelBytes(nextWidth, nextHeight, TexturePixelFormat::RGBA8));

            for (std::uint32_t y = 0u; y < nextHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < nextWidth; ++x)
                {
                    std::array<float, 4u> sum{};
                    for (std::uint32_t oy = 0u; oy < 2u; ++oy)
                    {
                        const std::uint32_t sourceY = std::min(height - 1u, y * 2u + oy);
                        for (std::uint32_t ox = 0u; ox < 2u; ++ox)
                        {
                            const std::uint32_t sourceX = std::min(width - 1u, x * 2u + ox);
                            const std::size_t sourceOffset =
                                (static_cast<std::size_t>(sourceY) * width + sourceX) * 4u;
                            for (std::size_t channel = 0u; channel < 4u; ++channel)
                            {
                                float value = static_cast<float>(
                                    std::to_integer<std::uint8_t>(source.Pixels[sourceOffset + channel])) / 255.0f;
                                if (channel < 3u && colorSpace == TextureColorSpace::SRGB)
                                    value = SrgbToLinear(value);
                                sum[channel] += value;
                            }
                        }
                    }

                    for (float& value : sum) value *= 0.25f;
                    if (normalMap)
                    {
                        float nx = sum[0] * 2.0f - 1.0f;
                        float ny = sum[1] * 2.0f - 1.0f;
                        float nz = sum[2] * 2.0f - 1.0f;
                        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                        if (length > std::numeric_limits<float>::epsilon())
                        {
                            nx /= length; ny /= length; nz /= length;
                        }
                        sum[0] = nx * 0.5f + 0.5f;
                        sum[1] = ny * 0.5f + 0.5f;
                        sum[2] = nz * 0.5f + 0.5f;
                    }
                    else if (colorSpace == TextureColorSpace::SRGB)
                    {
                        for (std::size_t channel = 0u; channel < 3u; ++channel)
                            sum[channel] = LinearToSrgb(sum[channel]);
                    }

                    const std::size_t destinationOffset =
                        (static_cast<std::size_t>(y) * nextWidth + x) * 4u;
                    for (std::size_t channel = 0u; channel < 4u; ++channel)
                        next.Pixels[destinationOffset + channel] = std::byte{ ToByte(sum[channel]) };
                }
            }
            result.Mips.push_back(std::move(next));
            width = nextWidth;
            height = nextHeight;
        }

        ValidateTextureArtifactData(result);
        return result;
    }

    [[nodiscard]] inline TextureArtifactData BuildTextureMipChainRGBA32Float(
        std::uint32_t width, std::uint32_t height, std::span<const float> rgba)
    {
        using namespace texture_artifact_detail;
        const std::size_t expectedFloats =
            CheckedPixelBytes(width, height, TexturePixelFormat::RGBA32Float) / sizeof(float);
        if (rgba.size() != expectedFloats)
            throw std::invalid_argument("Base float texture element count does not match its dimensions.");

        TextureArtifactData result;
        result.Format = TexturePixelFormat::RGBA32Float;
        result.ColorSpace = TextureColorSpace::Linear;
        result.Mips.push_back({ width, height, {} });
        result.Mips.back().Pixels.resize(expectedFloats * sizeof(float));
        for (std::size_t index = 0u; index < rgba.size(); ++index)
        {
            if (!std::isfinite(rgba[index]))
                throw std::invalid_argument("Float texture pixels must be finite.");
            WriteFloat(result.Mips.back().Pixels.data() + index * sizeof(float), rgba[index]);
        }

        while (width != 1u || height != 1u)
        {
            const TextureMipLevel& source = result.Mips.back();
            const std::uint32_t nextWidth = std::max<std::uint32_t>(1u, width / 2u);
            const std::uint32_t nextHeight = std::max<std::uint32_t>(1u, height / 2u);
            TextureMipLevel next;
            next.Width = nextWidth;
            next.Height = nextHeight;
            next.Pixels.resize(CheckedPixelBytes(nextWidth, nextHeight, TexturePixelFormat::RGBA32Float));

            for (std::uint32_t y = 0u; y < nextHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < nextWidth; ++x)
                {
                    for (std::size_t channel = 0u; channel < 4u; ++channel)
                    {
                        float sum = 0.0f;
                        for (std::uint32_t oy = 0u; oy < 2u; ++oy)
                        {
                            const std::uint32_t sourceY = std::min(height - 1u, y * 2u + oy);
                            for (std::uint32_t ox = 0u; ox < 2u; ++ox)
                            {
                                const std::uint32_t sourceX = std::min(width - 1u, x * 2u + ox);
                                const std::size_t sourcePixel =
                                    static_cast<std::size_t>(sourceY) * width + sourceX;
                                sum += ReadFloat(source.Pixels.data() +
                                    (sourcePixel * 4u + channel) * sizeof(float));
                            }
                        }
                        const std::size_t destinationPixel =
                            static_cast<std::size_t>(y) * nextWidth + x;
                        WriteFloat(next.Pixels.data() +
                            (destinationPixel * 4u + channel) * sizeof(float), sum * 0.25f);
                    }
                }
            }
            result.Mips.push_back(std::move(next));
            width = nextWidth;
            height = nextHeight;
        }

        ValidateTextureArtifactData(result);
        return result;
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeTextureArtifactData(
        const TextureArtifactData& texture)
    {
        using namespace texture_artifact_detail;
        ValidateTextureArtifactData(texture);
        BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        writer.WriteU8(static_cast<std::uint8_t>(texture.Format));
        writer.WriteU8(static_cast<std::uint8_t>(texture.ColorSpace));
        writer.WriteU32(texture.NormalMap ? NormalMapFlag : 0u);
        writer.WriteU32(static_cast<std::uint32_t>(texture.Mips.size()));
        for (const TextureMipLevel& mip : texture.Mips)
        {
            writer.WriteU32(mip.Width);
            writer.WriteU32(mip.Height);
            writer.WriteU64(static_cast<std::uint64_t>(mip.Pixels.size()));
            writer.WriteBytes(mip.Pixels);
        }
        if (writer.Bytes().size() > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("Texture artifact exceeds its payload safety limit.");
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline TextureArtifactData ParseTextureArtifactData(
        std::span<const std::byte> payload)
    {
        using namespace texture_artifact_detail;
        BinaryReader reader(payload);
        if (!std::equal(Magic.begin(), Magic.end(), reader.ReadBytes(Magic.size()).begin()))
            throw std::invalid_argument("Texture artifact magic is invalid.");
        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("Texture artifact payload version is unsupported.");

        TextureArtifactData texture;
        texture.Format = static_cast<TexturePixelFormat>(reader.ReadU8());
        texture.ColorSpace = static_cast<TextureColorSpace>(reader.ReadU8());
        const std::uint32_t flags = reader.ReadU32();
        if ((flags & ~KnownFlags) != 0u)
            throw std::invalid_argument("Texture artifact flags are invalid.");
        texture.NormalMap = (flags & NormalMapFlag) != 0u;
        const std::uint32_t mipCount = reader.ReadU32();
        if (mipCount == 0u || mipCount > MaximumMipCount)
            throw std::invalid_argument("Texture artifact mip count is invalid.");
        texture.Mips.reserve(mipCount);
        for (std::uint32_t index = 0u; index < mipCount; ++index)
        {
            TextureMipLevel mip;
            mip.Width = reader.ReadU32();
            mip.Height = reader.ReadU32();
            const std::uint64_t byteCount = reader.ReadU64();
            if (byteCount > reader.Remaining())
                throw std::invalid_argument("Texture artifact mip is truncated.");
            const auto bytes = reader.ReadBytes(static_cast<std::size_t>(byteCount));
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
