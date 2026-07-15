module;

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Assets.DerivedArtifact;

import Kairo.Assets.Types;
import Kairo.Assets.BinaryFormat;

export namespace kairo::assets
{
    /// Versioned, backend-neutral importer output. Payload is intentionally
    /// opaque to KairoAssets: runtime loaders own decoding while this layer
    /// guarantees the artifact's declared asset category and format revision.
    struct DerivedArtifact final
    {
        AssetType Type = AssetType::Other;
        std::uint32_t FormatVersion = 1u;
        std::string Format;
        std::vector<std::byte> Payload;
    };

    constexpr std::size_t MaximumDerivedArtifactPayloadBytes = 1024u * 1024u * 1024u;

    inline void ValidateDerivedArtifact(const DerivedArtifact& artifact)
    {
        const auto canonicalType = ParseAssetType(NameOfAssetType(artifact.Type));
        if (!canonicalType || *canonicalType != artifact.Type)
            throw std::invalid_argument("Derived artifact type is invalid.");
        if (artifact.FormatVersion == 0u) throw std::invalid_argument("Derived artifact format version must be positive.");
        if (artifact.Format.empty() || artifact.Format.size() > 128u)
            throw std::invalid_argument("Derived artifact format must contain 1 to 128 characters.");
        for (const char character : artifact.Format)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (!std::isalnum(byte) && byte != '.' && byte != '_' && byte != '-')
                throw std::invalid_argument("Derived artifact format contains an unsupported character.");
        }
        if (artifact.Payload.size() > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("Derived artifact payload exceeds the 1 GiB safety limit.");
    }

    namespace artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{ 'K' }, std::byte{ 'A' }, std::byte{ 'I' }, std::byte{ 'R' },
            std::byte{ 'O' }, std::byte{ 'D' }, std::byte{ 'D' }, std::byte{ '1' } };
        constexpr std::uint32_t EnvelopeVersion = 1u;

    }

    /// Output: deterministic, endian-independent cache bytes. Asset types are
    /// serialized by stable names rather than enum ordinals.
    [[nodiscard]] inline std::vector<std::byte> SerializeDerivedArtifact(const DerivedArtifact& artifact)
    {
        using namespace artifact_detail;
        ValidateDerivedArtifact(artifact);
        const std::string_view type = NameOfAssetType(artifact.Type);
        BinaryWriter output(Magic.size() + 24u + type.size() + artifact.Format.size() + artifact.Payload.size());
        output.WriteBytes(Magic);
        output.WriteU32(EnvelopeVersion);
        output.WriteU32(static_cast<std::uint32_t>(type.size()));
        output.WriteU32(artifact.FormatVersion);
        output.WriteU32(static_cast<std::uint32_t>(artifact.Format.size()));
        output.WriteU64(static_cast<std::uint64_t>(artifact.Payload.size()));
        output.WriteText(type);
        output.WriteText(artifact.Format);
        output.WriteBytes(artifact.Payload);
        return std::move(output).TakeBytes();
    }

    /// Input: one complete serialized artifact. Output: validated envelope and
    /// payload. Unknown versions/types, truncation, and trailing bytes fail.
    [[nodiscard]] inline DerivedArtifact ParseDerivedArtifact(std::span<const std::byte> input)
    {
        using namespace artifact_detail;
        BinaryReader reader(input);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Derived artifact magic is invalid.");
        if (reader.ReadU32() != EnvelopeVersion)
            throw std::invalid_argument("Derived artifact envelope version is unsupported.");
        const std::uint32_t typeLength = reader.ReadU32();
        const std::uint32_t formatVersion = reader.ReadU32();
        const std::uint32_t formatLength = reader.ReadU32();
        const std::uint64_t payloadLength = reader.ReadU64();
        if (typeLength == 0u || typeLength > 32u || formatLength == 0u || formatLength > 128u ||
            payloadLength > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("Derived artifact field exceeds its safety limit.");
        const std::string typeName = reader.ReadText(typeLength);
        const auto type = ParseAssetType(typeName);
        if (!type) throw std::invalid_argument("Derived artifact asset type is unknown.");
        DerivedArtifact result;
        result.Type = *type;
        result.FormatVersion = formatVersion;
        result.Format = reader.ReadText(formatLength);
        if (payloadLength != static_cast<std::uint64_t>(reader.Remaining()))
            throw std::invalid_argument("Derived artifact payload length does not match the complete input.");
        const auto payload = reader.ReadBytes(static_cast<std::size_t>(payloadLength));
        result.Payload.assign(payload.begin(), payload.end());
        reader.RequireEnd();
        ValidateDerivedArtifact(result);
        return result;
    }
}
