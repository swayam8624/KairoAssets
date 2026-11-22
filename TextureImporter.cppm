module;

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// TEACHING IMPORT BOUNDARY: source bytes -> stb_image decoder -> owned
// canonical TextureArtifactData -> versioned DerivedArtifact. The importer's
// Identity/Version and canonical settings are part of DerivedDataKey, which
// KairoGameEngine uses before RendererRuntime creates any GPU texture.
// The STBI_NO_STDIO contract means the importer decodes supplied bytes,
// while ImportSourceAsset owns bounded project-local file reading.
export module Kairo.Assets.TextureImporter;

import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Importer;
import Kairo.Assets.TextureArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    // Semantic decode recipe, not GPU sampler state: ColorSpace, NormalMap,
    // GenerateMips and a maximum decoded dimension. These values must be
    // canonicalized and included in the derived key for reproducible reuse.
    struct TextureImportSettings final
    {
        TextureColorSpace ColorSpace = TextureColorSpace::SRGB;
        bool NormalMap = false;
        bool GenerateMips = true;
        std::uint32_t MaximumDimension = 16'384u;
    };

    // Parse strictly typed semicolon-separated key=value tokens. Reject
    // unknown keys, malformed numeric dimension and boolean encodings.
    // A normal map forces Linear even if a stale recipe said SRGB.
    [[nodiscard]] inline TextureImportSettings ParseTextureImportSettings(
        std::string_view canonicalSettings)
    {
        TextureImportSettings settings;
        std::size_t cursor = 0u;
        while (cursor < canonicalSettings.size())
        {
            const std::size_t separator = canonicalSettings.find(';', cursor);
            const std::string_view token = canonicalSettings.substr(cursor,
                separator == std::string_view::npos ? canonicalSettings.size() - cursor
                    : separator - cursor);
            if (!token.empty())
            {
                const std::size_t equals = token.find('=');
                if (equals == std::string_view::npos)
                    throw std::invalid_argument("Texture import settings require key=value tokens.");
                const std::string_view key = token.substr(0u, equals);
                const std::string_view value = token.substr(equals + 1u);
                if (key == "colorspace")
                {
                    if (value == "srgb") settings.ColorSpace = TextureColorSpace::SRGB;
                    else if (value == "linear") settings.ColorSpace = TextureColorSpace::Linear;
                    else throw std::invalid_argument("Texture colorspace must be srgb or linear.");
                }
                else if (key == "normal")
                {
                    if (value == "1" || value == "true") settings.NormalMap = true;
                    else if (value == "0" || value == "false") settings.NormalMap = false;
                    else throw std::invalid_argument("Texture normal setting must be boolean.");
                }
                else if (key == "mips")
                {
                    if (value == "1" || value == "true") settings.GenerateMips = true;
                    else if (value == "0" || value == "false") settings.GenerateMips = false;
                    else throw std::invalid_argument("Texture mips setting must be boolean.");
                }
                else if (key == "max")
                {
                    std::uint32_t parsed = 0u;
                    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                        parsed == 0u || parsed > 32'768u)
                        throw std::invalid_argument("Texture max dimension is invalid.");
                    settings.MaximumDimension = parsed;
                }
                else
                {
                    throw std::invalid_argument("Texture import setting is unknown.");
                }
            }
            if (separator == std::string_view::npos) break;
            cursor = separator + 1u;
        }
        if (settings.NormalMap) settings.ColorSpace = TextureColorSpace::Linear;
        return settings;
    }

    // Produce one stable settings string with fixed field order; recipe
    // equivalence must not depend on caller argument order or process locale.
    // The string participates in MakeDerivedDataKey and cache invalidation.
    [[nodiscard]] inline std::string CanonicalTextureImportSettings(
        const TextureImportSettings& settings)
    {
        if (settings.MaximumDimension == 0u || settings.MaximumDimension > 32'768u)
            throw std::invalid_argument("Texture max dimension is invalid.");
        const TextureColorSpace colorSpace =
            settings.NormalMap ? TextureColorSpace::Linear : settings.ColorSpace;
        return std::string("colorspace=") +
            (colorSpace == TextureColorSpace::SRGB ? "srgb" : "linear") +
            ";normal=" + (settings.NormalMap ? "1" : "0") +
            ";mips=" + (settings.GenerateMips ? "1" : "0") +
            ";max=" + std::to_string(settings.MaximumDimension);
    }

    // Concrete importer uses stb_image only for CPU decoding. Identifier()
    // and Version() are versioned contracts checked by ImportSourceAsset;
    // the importer itself does not publish a cache key or create VkImage.
    class StbTextureImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.texture.stb"; }
        [[nodiscard]] std::string Version() const override { return "1"; }

        // Validate typed texture request and decoder input byte count.
        // For HDR input use stbi_loadf and BuildTextureMipChainRGBA32Float;
        // otherwise use stbi_load and BuildTextureMipChainRGBA8. RAII Release
        // frees stb-owned decoded storage on normal return or exceptions.
        // Optional no-mips truncation occurs before derived serialization.
        [[nodiscard]] DerivedArtifact Import(const ImportRequest& request) const override
        {
            if (request.ExpectedType != AssetType::Texture2D)
                throw std::invalid_argument("STB texture importer requires a texture2d asset.");
            if (request.SourceBytes.empty())
                throw std::invalid_argument("Texture source cannot be empty.");
            if (request.SourceBytes.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::length_error("Texture source is too large for the decoder.");

            const TextureImportSettings settings =
                ParseTextureImportSettings(request.Record.CanonicalSettings);
            const auto* source = reinterpret_cast<const stbi_uc*>(request.SourceBytes.data());
            const int sourceBytes = static_cast<int>(request.SourceBytes.size());
            int width = 0;
            int height = 0;
            int channels = 0;

            // HDR and ordinary byte textures need distinct decoding paths:
            // Float32 source radiance -> finite binary16 artifact, versus
            // RGBA8 source bytes -> color-space-aware mip generation.
            if (stbi_is_hdr_from_memory(source, sourceBytes) != 0)
            {
                float* decoded = stbi_loadf_from_memory(
                    source, sourceBytes, &width, &height, &channels, STBI_rgb_alpha);
                if (decoded == nullptr)
                    throw std::invalid_argument(std::string("Unable to decode HDR texture: ") +
                        stbi_failure_reason());
                struct Release final
                {
                    float* Data;
                    ~Release() { stbi_image_free(Data); }
                } release{ decoded };

                ValidateDimensions(width, height, settings.MaximumDimension);
                const std::size_t elements = static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4u;
                TextureArtifactData texture = BuildTextureMipChainRGBA32Float(
                    static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                    std::span<const float>(decoded, elements));
                if (!settings.GenerateMips)
                    texture.Mips.resize(1u);
                return MakeTextureDerivedArtifact(texture);
            }

            // Decode to four RGBA byte channels regardless of source channel
            // count, then validate dimensions before allocating every mip.
            stbi_uc* decoded = stbi_load_from_memory(
                source, sourceBytes, &width, &height, &channels, STBI_rgb_alpha);
            if (decoded == nullptr)
                throw std::invalid_argument(std::string("Unable to decode texture: ") +
                    stbi_failure_reason());
            struct Release final
            {
                stbi_uc* Data;
                ~Release() { stbi_image_free(Data); }
            } release{ decoded };

            ValidateDimensions(width, height, settings.MaximumDimension);
            const std::size_t bytes = static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 4u;
            const auto byteSpan = std::as_bytes(std::span(decoded, bytes));
            TextureArtifactData texture = BuildTextureMipChainRGBA8(
                static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                byteSpan, settings.ColorSpace, settings.NormalMap);
            if (!settings.GenerateMips)
                texture.Mips.resize(1u);
            return MakeTextureDerivedArtifact(texture);
        }

    private:
        // Reject zero, negative, or out-of-recipe maximum dimensions before
        // casting decoder integers into unsigned size/byte calculations.
        static void ValidateDimensions(int width, int height, std::uint32_t maximumDimension)
        {
            if (width <= 0 || height <= 0 ||
                static_cast<std::uint32_t>(width) > maximumDimension ||
                static_cast<std::uint32_t>(height) > maximumDimension)
                throw std::invalid_argument("Decoded texture dimensions exceed import settings.");
        }
    };
}
