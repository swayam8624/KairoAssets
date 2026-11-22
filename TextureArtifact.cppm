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

// TEACHING CROSS-REPO CONTRACT: this is the CPU-owned canonical texture
// representation consumed by KairoRenderer::VulkanTexture2D and MetalBackend.
// Pixel format, semantic, color space and canonical mip dimensions are
// validated HERE, before GPU allocation. VulkanTexture uploads these bytes
// unchanged and maps their color-space meaning to native VkFormat.
// See KairoRenderer/docs/TEXTURE_UPLOAD_AND_RENDER_TARGET_TRANSITIONS_LAB.md.
export module Kairo.Assets.TextureArtifact;

import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    // Number/precision of serialized channels, not a GPU image handle.
    // R8/RG8/RGBA8 are normalized byte encodings; RGBA16Float is 8 B/px.
    enum class TexturePixelFormat : std::uint8_t
    {
        R8 = 1u,
        RG8 = 2u,
        RGBA8 = 3u,
        RGBA16Float = 4u
    };

    // SRGB byte RGB channels require decoding to linear for lighting and
    // correct prefiltered averages; numeric data/normal/HDR remain Linear.
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

    // Owned tightly packed row-major texel bytes for ONE explicit mip.
    // The renderer does not build mipmaps during upload, so all needed levels
    // must already exist in TextureArtifactData::Mips.
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

        // Validate positive capped dimensions and safely calculate required
        // payload bytes before allocating a CPU array or emitting a mip.
        // The derived-artifact global byte budget is independent of GPU VRAM.
        [[nodiscard]] inline std::size_t CheckedPixelBytes(
            std::uint32_t width, std::uint32_t height, TexturePixelFormat format)
        {
            if (width == 0u || height == 0u || width > MaximumDimension || height > MaximumDimension)
                throw std::invalid_argument("Texture dimensions are outside the supported range.");
            const std::uint32_t bytesPerPixel = BytesPerPixel(format);
            const std::uint64_t bytes = std::uint64_t(width) * height * bytesPerPixel;
            if (bytes > MaximumDerivedArtifactPayloadBytes)
                throw std::length_error("Texture pixel payload exceeds its safety limit.");
            return static_cast<std::size_t>(bytes);
        }

        // IEC-style piecewise transfer: average color intensities in linear
        // space, not directly in sRGB 8-bit encoded values.
        [[nodiscard]] inline float SrgbToLinear(float value) noexcept
        {
            return value <= 0.04045f ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        // Re-encode the filtered linear color for storage in an SRGB artifact.
        // Clamp avoids invalid out-of-range byte values at the output.
        [[nodiscard]] inline float LinearToSrgb(float value) noexcept
        {
            value = std::clamp(value, 0.0f, 1.0f);
            return value <= 0.0031308f ? value * 12.92f
                : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        [[nodiscard]] inline std::uint8_t ToByte(float value) noexcept
        {
            return static_cast<std::uint8_t>(
                std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        /// Converts a finite IEEE-754 binary32 value to binary16 with
        /// round-to-nearest-even. Values outside binary16's finite range are
        /// rejected by the caller rather than silently becoming infinity.
        // Convert a finite Float32 bit pattern to binary16 using round-to-
        // nearest-even; the calling WriteHalf validates representability.
        // Packed binary16 bytes are later copied unchanged into Vulkan
        // VK_FORMAT_R16G16B16A16_SFLOAT or matching Metal storage.
        [[nodiscard]] inline std::uint16_t FloatToHalf(float value) noexcept
        {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
            const std::uint32_t sign = (bits >> 16u) & 0x8000u;
            const std::uint32_t magnitude = bits & 0x7fff'ffffu;
            if (magnitude < 0x3880'0000u)
            {
                if (magnitude < 0x3300'0000u) return static_cast<std::uint16_t>(sign);
                const std::uint32_t exponent = magnitude >> 23u;
                const std::uint32_t mantissa = (magnitude & 0x007f'ffffu) | 0x0080'0000u;
                const std::uint32_t shift = 113u - exponent;
                const std::uint32_t rounded = (mantissa + ((1u << (shift + 12u)) - 1u) +
                    ((mantissa >> (shift + 13u)) & 1u)) >> (shift + 13u);
                return static_cast<std::uint16_t>(sign | rounded);
            }
            const std::uint32_t rounded = magnitude + 0x0000'0fffu + ((magnitude >> 13u) & 1u);
            return static_cast<std::uint16_t>(sign | ((rounded - 0x3800'0000u) >> 13u));
        }

        // Refuse NaN, Infinity or magnitudes beyond 65504 before serializing
        // two little-endian bytes of one half float. Avoid silently encoding
        // invalid radiance in a purported finite HDR artifact.
        inline void WriteHalf(std::vector<std::byte>& bytes, float value)
        {
            if (!std::isfinite(value) || std::abs(value) > 65'504.0f)
                throw std::invalid_argument("HDR texture values must be finite and representable as RGBA16Float.");
            const std::uint16_t half = FloatToHalf(value);
            bytes.push_back(std::byte{ static_cast<std::uint8_t>(half) });
            bytes.push_back(std::byte{ static_cast<std::uint8_t>(half >> 8u) });
        }

        // Enforce semantic-color-space combinations: normal/data/HDR must
        // be linear; HDR also requires RGBA16Float. Color is allowed SRGB.
        // These rules are what make SelectFormat in KairoRenderer safe.
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

    // Validate 1..16 mips, canonical down-to-1 extents, exact bytes per
    // pixel and bounded cumulative payload. A malformed mip is rejected
    // before VulkanHostBuffer/VkImage allocations occur downstream.
    // Dimension chain uses max(1, previous/2) on both axes.
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

    /// Input: tightly packed RGBA8 base pixels and their color-space/semantic.
    /// Output: a validated canonical mip chain down to 1x1.
    /// Task: provide one deterministic CPU reference path for importers, tests,
    /// and headless asset workers. sRGB colors are averaged in linear space;
    /// normal maps are averaged and renormalized before re-encoding.
    // Seed the full-resolution RGBA8 level; box-filter 2x2 neighborhoods
    // into successively smaller mips until 1x1. SRGB colors are decoded
    // BEFORE averaging then re-encoded; normal vectors are remapped to
    // [-1,1], averaged, renormalized and re-encoded for shader sampling.
    // Pixel alpha is averaged as linear coverage/value, not SRGB color.
    [[nodiscard]] inline TextureArtifactData BuildTextureMipChainRGBA8(
        std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba,
        TextureColorSpace colorSpace = TextureColorSpace::SRGB, bool normalMap = false)
    {
        using namespace texture_artifact_detail;
        if (rgba.size() != CheckedPixelBytes(width, height, TexturePixelFormat::RGBA8))
            throw std::invalid_argument("Base texture byte count does not match its dimensions.");
        if (normalMap && colorSpace != TextureColorSpace::Linear)
            throw std::invalid_argument("Normal-map mip generation requires linear color space.");

        TextureArtifactData result;
        result.Format = TexturePixelFormat::RGBA8;
        result.ColorSpace = colorSpace;
        result.Semantic = normalMap ? TextureSemantic::Normal : TextureSemantic::Color;
        result.Mips.push_back({ width, height, { rgba.begin(), rgba.end() } });
        while (width != 1u || height != 1u)
        {
            const TextureMipLevel& source = result.Mips.back();
            const std::uint32_t nextWidth = std::max(1u, width / 2u);
            const std::uint32_t nextHeight = std::max(1u, height / 2u);
            TextureMipLevel next{ nextWidth, nextHeight,
                std::vector<std::byte>(CheckedPixelBytes(nextWidth, nextHeight,
                    TexturePixelFormat::RGBA8)) };
            for (std::uint32_t y = 0u; y < nextHeight; ++y)
                for (std::uint32_t x = 0u; x < nextWidth; ++x)
                {
                    std::array<float, 4u> sum{};
                    for (std::uint32_t oy = 0u; oy < 2u; ++oy)
                        for (std::uint32_t ox = 0u; ox < 2u; ++ox)
                        {
                            const std::uint32_t sourceX = std::min(width - 1u, x * 2u + ox);
                            const std::uint32_t sourceY = std::min(height - 1u, y * 2u + oy);
                            const std::size_t offset =
                                (static_cast<std::size_t>(sourceY) * width + sourceX) * 4u;
                            for (std::size_t channel = 0u; channel < 4u; ++channel)
                            {
                                float value = static_cast<float>(
                                    std::to_integer<std::uint8_t>(source.Pixels[offset + channel])) / 255.0f;
                                if (channel < 3u && colorSpace == TextureColorSpace::SRGB)
                                    value = SrgbToLinear(value);
                                sum[channel] += value;
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
                            nx /= length;
                            ny /= length;
                            nz /= length;
                        }
                        sum[0] = nx * 0.5f + 0.5f;
                        sum[1] = ny * 0.5f + 0.5f;
                        sum[2] = nz * 0.5f + 0.5f;
                    }
                    else if (colorSpace == TextureColorSpace::SRGB)
                        for (std::size_t channel = 0u; channel < 3u; ++channel)
                            sum[channel] = LinearToSrgb(sum[channel]);
                    const std::size_t destination =
                        (static_cast<std::size_t>(y) * nextWidth + x) * 4u;
                    for (std::size_t channel = 0u; channel < 4u; ++channel)
                        next.Pixels[destination + channel] = std::byte{ ToByte(sum[channel]) };
                }
            result.Mips.push_back(std::move(next));
            width = nextWidth;
            height = nextHeight;
        }
        ValidateTextureArtifactData(result);
        return result;
    }

    /// Input: tightly packed finite RGBA binary32 HDR pixels.
    /// Output: a linear RGBA16F mip chain using deterministic box filtering.
    /// Degeneracy: values outside the finite binary16 range are rejected so
    /// import never silently clips authored radiance or writes infinities.
    // Build a linear HDR RGBA16Float mip pyramid from Float32 radiance,
    // using Float32 intermediate box averages and finite binary16 writes.
    // The renderer receives finished half-byte payloads and only uploads them.
    [[nodiscard]] inline TextureArtifactData BuildTextureMipChainRGBA32Float(
        std::uint32_t width, std::uint32_t height, std::span<const float> rgba)
    {
        using namespace texture_artifact_detail;
        if (rgba.size() != static_cast<std::size_t>(width) * height * 4u)
            throw std::invalid_argument("Base HDR element count does not match its dimensions.");
        (void)CheckedPixelBytes(width, height, TexturePixelFormat::RGBA16Float);

        TextureArtifactData result;
        result.Format = TexturePixelFormat::RGBA16Float;
        result.ColorSpace = TextureColorSpace::Linear;
        result.Semantic = TextureSemantic::HDR;
        std::vector<float> source(rgba.begin(), rgba.end());
        while (true)
        {
            TextureMipLevel mip{ width, height, {} };
            mip.Pixels.reserve(CheckedPixelBytes(width, height, TexturePixelFormat::RGBA16Float));
            for (const float value : source) WriteHalf(mip.Pixels, value);
            result.Mips.push_back(std::move(mip));
            if (width == 1u && height == 1u) break;
            const std::uint32_t nextWidth = std::max(1u, width / 2u);
            const std::uint32_t nextHeight = std::max(1u, height / 2u);
            std::vector<float> next(static_cast<std::size_t>(nextWidth) * nextHeight * 4u);
            for (std::uint32_t y = 0u; y < nextHeight; ++y)
                for (std::uint32_t x = 0u; x < nextWidth; ++x)
                    for (std::size_t channel = 0u; channel < 4u; ++channel)
                    {
                        float sum = 0.0f;
                        for (std::uint32_t oy = 0u; oy < 2u; ++oy)
                            for (std::uint32_t ox = 0u; ox < 2u; ++ox)
                            {
                                const std::uint32_t sourceX = std::min(width - 1u, x * 2u + ox);
                                const std::uint32_t sourceY = std::min(height - 1u, y * 2u + oy);
                                sum += source[(static_cast<std::size_t>(sourceY) * width + sourceX) * 4u + channel];
                            }
                        next[(static_cast<std::size_t>(y) * nextWidth + x) * 4u + channel] = sum * 0.25f;
                    }
            source = std::move(next);
            width = nextWidth;
            height = nextHeight;
        }
        ValidateTextureArtifactData(result);
        return result;
    }

    // Versioned KTEXT0001 binary payload: magic, u32 version, format/space/
    // semantic/mipCount bytes and (width,height,payloadSize,pixels) per mip.
    // Runs ValidateTextureArtifactData first so emitted bytes are canonical.
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

    // Decode a bounded owned CPU record, require exact EOF (no trailing
    // bytes), and validate the complete semantic+mip chain after parsing.
    // Do NOT expose raw payload pointers as GPU texture storage.
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

    // Wrap the validated texture payload in a typed derived-asset envelope:
    // AssetType::Texture2D, version 1, format "kairo.texture2d.v1".
    // Asset registry identity/cache policy lives outside this CPU codec.
    [[nodiscard]] inline DerivedArtifact MakeTextureDerivedArtifact(
        const TextureArtifactData& texture)
    {
        return { AssetType::Texture2D, 1u, "kairo.texture2d.v1",
            SerializeTextureArtifactData(texture) };
    }

    // Validate the outer derived-artifact envelope and its explicit type/
    // version/format before decoding the inner KTEXT0001 bytes and pixels.
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
