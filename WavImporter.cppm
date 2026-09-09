module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module Kairo.Assets.WavImporter;

import Kairo.Assets.AudioArtifact;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Importer;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    struct WavImportSettings final
    {
        bool NormalizePeak = false;
    };

    [[nodiscard]] inline WavImportSettings ParseWavImportSettings(
        std::string_view canonicalSettings)
    {
        WavImportSettings settings;
        if (canonicalSettings.empty() || canonicalSettings == "normalize=0")
            return settings;
        if (canonicalSettings == "normalize=1")
        {
            settings.NormalizePeak = true;
            return settings;
        }
        throw std::invalid_argument(
            "WAV import settings must be empty, normalize=0, or normalize=1.");
    }

    [[nodiscard]] inline std::string CanonicalWavImportSettings(
        const WavImportSettings& settings)
    {
        return settings.NormalizePeak ? "normalize=1" : "normalize=0";
    }

    namespace wav_import_detail
    {
        [[nodiscard]] inline std::uint16_t ReadU16(
            std::span<const std::byte> bytes, std::size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 2u)
                throw std::invalid_argument("WAV input is truncated.");
            return static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes[offset])) |
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[offset + 1u]) << 8u);
        }

        [[nodiscard]] inline std::uint32_t ReadU32(
            std::span<const std::byte> bytes, std::size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
                throw std::invalid_argument("WAV input is truncated.");
            std::uint32_t result = 0u;
            for (std::size_t index = 0u; index < 4u; ++index)
                result |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[offset + index])) <<
                    static_cast<unsigned>(index * 8u);
            return result;
        }

        [[nodiscard]] inline bool FourCC(std::span<const std::byte> bytes,
            std::size_t offset, char a, char b, char c, char d)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u) return false;
            return std::to_integer<unsigned char>(bytes[offset]) ==
                    static_cast<unsigned char>(a) &&
                std::to_integer<unsigned char>(bytes[offset + 1u]) ==
                    static_cast<unsigned char>(b) &&
                std::to_integer<unsigned char>(bytes[offset + 2u]) ==
                    static_cast<unsigned char>(c) &&
                std::to_integer<unsigned char>(bytes[offset + 3u]) ==
                    static_cast<unsigned char>(d);
        }

        [[nodiscard]] inline float DecodePcm(
            std::span<const std::byte> bytes, std::uint16_t bits)
        {
            if (bits == 8u)
            {
                const auto value = std::to_integer<std::uint8_t>(bytes[0]);
                return static_cast<float>(
                    (static_cast<int>(value) - 128) / 128.0);
            }
            if (bits == 16u)
            {
                const std::uint16_t raw = static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                    static_cast<std::uint16_t>(
                        std::to_integer<std::uint8_t>(bytes[1]) << 8u);
                const auto signedValue = static_cast<std::int16_t>(raw);
                return static_cast<float>(static_cast<double>(signedValue) / 32768.0);
            }
            if (bits == 24u)
            {
                std::int32_t value =
                    static_cast<std::int32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
                    (static_cast<std::int32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8) |
                    (static_cast<std::int32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16);
                if ((value & 0x0080'0000) != 0) value |= ~0x00ff'ffff;
                return static_cast<float>(static_cast<double>(value) / 8'388'608.0);
            }
            if (bits == 32u)
            {
                const std::uint32_t raw = ReadU32(bytes, 0u);
                const auto signedValue = static_cast<std::int32_t>(raw);
                return static_cast<float>(static_cast<double>(signedValue) /
                    2'147'483'648.0);
            }
            throw std::invalid_argument("WAV PCM bit depth is unsupported.");
        }

        [[nodiscard]] inline float DecodeFloat32(std::span<const std::byte> bytes)
        {
            const float value = std::bit_cast<float>(ReadU32(bytes, 0u));
            if (!std::isfinite(value))
                throw std::invalid_argument("WAV floating-point samples must be finite.");
            return std::clamp(value, -1.0f, 1.0f);
        }
    }

    class WavAudioImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override
        {
            return "kairo.audio.wav";
        }

        [[nodiscard]] std::string Version() const override { return "1"; }

        [[nodiscard]] DerivedArtifact Import(
            const ImportRequest& request) const override
        {
            using namespace wav_import_detail;
            if (request.ExpectedType != AssetType::Audio)
                throw std::invalid_argument("WAV importer requires an audio asset.");
            const auto bytes = request.SourceBytes;
            if (bytes.size() < 44u)
                throw std::invalid_argument("WAV source is too small to contain RIFF audio.");
            if (!FourCC(bytes, 0u, 'R', 'I', 'F', 'F') ||
                !FourCC(bytes, 8u, 'W', 'A', 'V', 'E'))
                throw std::invalid_argument("WAV source requires little-endian RIFF/WAVE framing.");

            const std::uint64_t declaredBytes =
                static_cast<std::uint64_t>(ReadU32(bytes, 4u)) + 8u;
            if (declaredBytes != bytes.size())
                throw std::invalid_argument("WAV RIFF size does not match the complete source.");

            bool foundFormat = false;
            bool foundData = false;
            std::uint16_t format = 0u;
            std::uint16_t channels = 0u;
            std::uint16_t bitsPerSample = 0u;
            std::uint16_t blockAlign = 0u;
            std::uint32_t sampleRate = 0u;
            std::span<const std::byte> data;

            std::size_t cursor = 12u;
            while (cursor < bytes.size())
            {
                if (bytes.size() - cursor < 8u)
                    throw std::invalid_argument("WAV chunk header is truncated.");
                const std::uint32_t chunkBytes = ReadU32(bytes, cursor + 4u);
                const std::size_t payload = cursor + 8u;
                if (payload > bytes.size() ||
                    static_cast<std::uint64_t>(chunkBytes) > bytes.size() - payload)
                    throw std::invalid_argument("WAV chunk exceeds the RIFF container.");

                if (FourCC(bytes, cursor, 'f', 'm', 't', ' '))
                {
                    if (foundFormat)
                        throw std::invalid_argument("WAV contains more than one fmt chunk.");
                    if (chunkBytes < 16u)
                        throw std::invalid_argument("WAV fmt chunk is truncated.");
                    format = ReadU16(bytes, payload);
                    channels = ReadU16(bytes, payload + 2u);
                    sampleRate = ReadU32(bytes, payload + 4u);
                    const std::uint32_t byteRate = ReadU32(bytes, payload + 8u);
                    blockAlign = ReadU16(bytes, payload + 12u);
                    bitsPerSample = ReadU16(bytes, payload + 14u);
                    if (channels != 1u && channels != 2u)
                        throw std::invalid_argument("WAV importer supports mono or stereo sources.");
                    if (sampleRate < 8'000u || sampleRate > 384'000u)
                        throw std::invalid_argument("WAV sample rate is outside the supported range.");
                    const std::uint32_t bytesPerSample = (bitsPerSample + 7u) / 8u;
                    if (bitsPerSample == 0u || bytesPerSample == 0u ||
                        blockAlign != channels * bytesPerSample ||
                        byteRate != sampleRate * blockAlign)
                        throw std::invalid_argument("WAV format alignment/rate fields are inconsistent.");
                    if (format == 1u)
                    {
                        if (bitsPerSample != 8u && bitsPerSample != 16u &&
                            bitsPerSample != 24u && bitsPerSample != 32u)
                            throw std::invalid_argument("WAV PCM bit depth is unsupported.");
                    }
                    else if (format == 3u)
                    {
                        if (bitsPerSample != 32u)
                            throw std::invalid_argument("WAV IEEE-float input must use binary32 samples.");
                    }
                    else
                        throw std::invalid_argument("Compressed or extensible WAV formats are unsupported.");
                    foundFormat = true;
                }
                else if (FourCC(bytes, cursor, 'd', 'a', 't', 'a'))
                {
                    if (foundData)
                        throw std::invalid_argument("WAV contains more than one data chunk.");
                    data = bytes.subspan(payload, chunkBytes);
                    foundData = true;
                }

                const std::size_t padded = static_cast<std::size_t>(chunkBytes) +
                    static_cast<std::size_t>(chunkBytes & 1u);
                if (padded > bytes.size() - payload)
                    throw std::invalid_argument("WAV padded chunk exceeds the RIFF container.");
                cursor = payload + padded;
            }

            if (!foundFormat || !foundData)
                throw std::invalid_argument("WAV requires both fmt and data chunks.");
            if (data.empty() || blockAlign == 0u || data.size() % blockAlign != 0u)
                throw std::invalid_argument("WAV data byte count does not contain complete frames.");

            const WavImportSettings settings =
                ParseWavImportSettings(request.Record.CanonicalSettings);
            const std::size_t bytesPerSample = bitsPerSample / 8u;
            const std::size_t sampleCount = data.size() / bytesPerSample;
            if (sampleCount > (MaximumDerivedArtifactPayloadBytes / sizeof(float)))
                throw std::length_error("Decoded WAV exceeds the audio artifact safety limit.");

            AudioArtifactData audio;
            audio.SampleRate = sampleRate;
            audio.Channels = channels;
            audio.Samples.reserve(sampleCount);
            for (std::size_t offset = 0u; offset < data.size(); offset += bytesPerSample)
            {
                const auto sampleBytes = data.subspan(offset, bytesPerSample);
                const float sample = format == 1u
                    ? DecodePcm(sampleBytes, bitsPerSample)
                    : DecodeFloat32(sampleBytes);
                audio.Samples.push_back(std::clamp(sample, -1.0f, 1.0f));
            }

            if (settings.NormalizePeak)
            {
                float peak = 0.0f;
                for (const float sample : audio.Samples)
                    peak = std::max(peak, std::abs(sample));
                if (peak > std::numeric_limits<float>::epsilon())
                    for (float& sample : audio.Samples)
                        sample = std::clamp(sample / peak, -1.0f, 1.0f);
            }

            ValidateAudioArtifactData(audio);
            return MakeAudioDerivedArtifact(audio);
        }
    };
}
