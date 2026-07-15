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
#include <type_traits>
#include <utility>
#include <vector>

export module Kairo.Assets.DerivedArtifact;

import Kairo.Assets.Types;

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

        template<class Integer>
        inline void AppendLittleEndian(std::vector<std::byte>& output, Integer value)
        {
            static_assert(std::is_unsigned_v<Integer>);
            for (std::size_t index = 0u; index < sizeof(Integer); ++index)
                output.push_back(std::byte{ static_cast<unsigned char>(value >> (index * 8u)) });
        }

        template<class Integer>
        [[nodiscard]] inline Integer ReadLittleEndian(std::span<const std::byte> input, std::size_t& cursor)
        {
            static_assert(std::is_unsigned_v<Integer>);
            if (cursor > input.size() || input.size() - cursor < sizeof(Integer))
                throw std::invalid_argument("Derived artifact is truncated.");
            Integer result = 0u;
            for (std::size_t index = 0u; index < sizeof(Integer); ++index)
                result |= static_cast<Integer>(std::to_integer<unsigned char>(input[cursor++])) << (index * 8u);
            return result;
        }

        [[nodiscard]] inline std::string ReadText(std::span<const std::byte> input,
            std::size_t& cursor, std::size_t length)
        {
            if (cursor > input.size() || length > input.size() - cursor)
                throw std::invalid_argument("Derived artifact is truncated.");
            std::string result(length, '\0');
            for (std::size_t index = 0u; index < length; ++index)
                result[index] = static_cast<char>(std::to_integer<unsigned char>(input[cursor++]));
            return result;
        }
    }

    /// Output: deterministic, endian-independent cache bytes. Asset types are
    /// serialized by stable names rather than enum ordinals.
    [[nodiscard]] inline std::vector<std::byte> SerializeDerivedArtifact(const DerivedArtifact& artifact)
    {
        using namespace artifact_detail;
        ValidateDerivedArtifact(artifact);
        const std::string_view type = NameOfAssetType(artifact.Type);
        std::vector<std::byte> output;
        output.reserve(Magic.size() + 24u + type.size() + artifact.Format.size() + artifact.Payload.size());
        output.insert(output.end(), Magic.begin(), Magic.end());
        AppendLittleEndian(output, EnvelopeVersion);
        AppendLittleEndian(output, static_cast<std::uint32_t>(type.size()));
        AppendLittleEndian(output, artifact.FormatVersion);
        AppendLittleEndian(output, static_cast<std::uint32_t>(artifact.Format.size()));
        AppendLittleEndian(output, static_cast<std::uint64_t>(artifact.Payload.size()));
        for (const char character : type) output.push_back(std::byte{ static_cast<unsigned char>(character) });
        for (const char character : artifact.Format) output.push_back(std::byte{ static_cast<unsigned char>(character) });
        output.insert(output.end(), artifact.Payload.begin(), artifact.Payload.end());
        return output;
    }

    /// Input: one complete serialized artifact. Output: validated envelope and
    /// payload. Unknown versions/types, truncation, and trailing bytes fail.
    [[nodiscard]] inline DerivedArtifact ParseDerivedArtifact(std::span<const std::byte> input)
    {
        using namespace artifact_detail;
        if (input.size() < Magic.size() || !std::equal(Magic.begin(), Magic.end(), input.begin()))
            throw std::invalid_argument("Derived artifact magic is invalid.");
        std::size_t cursor = Magic.size();
        if (ReadLittleEndian<std::uint32_t>(input, cursor) != EnvelopeVersion)
            throw std::invalid_argument("Derived artifact envelope version is unsupported.");
        const std::uint32_t typeLength = ReadLittleEndian<std::uint32_t>(input, cursor);
        const std::uint32_t formatVersion = ReadLittleEndian<std::uint32_t>(input, cursor);
        const std::uint32_t formatLength = ReadLittleEndian<std::uint32_t>(input, cursor);
        const std::uint64_t payloadLength = ReadLittleEndian<std::uint64_t>(input, cursor);
        if (typeLength == 0u || typeLength > 32u || formatLength == 0u || formatLength > 128u ||
            payloadLength > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("Derived artifact field exceeds its safety limit.");
        const std::string typeName = ReadText(input, cursor, typeLength);
        const auto type = ParseAssetType(typeName);
        if (!type) throw std::invalid_argument("Derived artifact asset type is unknown.");
        DerivedArtifact result;
        result.Type = *type;
        result.FormatVersion = formatVersion;
        result.Format = ReadText(input, cursor, formatLength);
        if (cursor > input.size() || payloadLength != static_cast<std::uint64_t>(input.size() - cursor))
            throw std::invalid_argument("Derived artifact payload length does not match the complete input.");
        result.Payload.assign(input.begin() + static_cast<std::ptrdiff_t>(cursor), input.end());
        ValidateDerivedArtifact(result);
        return result;
    }
}
