module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.AudioArtifact;

import Kairo.Assets.BinaryFormat;
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Backend-neutral decoded PCM consumed by EngineCore's AudioRuntime. The
    /// artifact stores normalized interleaved binary32 samples so platform
    /// playback never needs to decode authoring formats such as WAV at runtime.
    struct AudioArtifactData final
    {
        std::uint32_t SampleRate = 48'000u;
        std::uint32_t Channels = 1u;
        std::vector<float> Samples;

        [[nodiscard]] std::size_t FrameCount() const noexcept
        {
            return Channels == 0u ? 0u : Samples.size() / Channels;
        }

        [[nodiscard]] double DurationSeconds() const noexcept
        {
            return SampleRate == 0u ? 0.0 :
                static_cast<double>(FrameCount()) / static_cast<double>(SampleRate);
        }

        friend bool operator==(const AudioArtifactData&, const AudioArtifactData&) = default;
    };

    namespace audio_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'A'}, std::byte{'U'}, std::byte{'D'},
            std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t PayloadVersion = 1u;
        constexpr std::uint64_t HeaderBytes = 8u + 4u + 4u + 4u + 8u;
    }

    inline void ValidateAudioArtifactData(const AudioArtifactData& audio)
    {
        if (audio.SampleRate < 8'000u || audio.SampleRate > 384'000u)
            throw std::invalid_argument("Audio artifact sample rate is outside the supported range.");
        if (audio.Channels != 1u && audio.Channels != 2u)
            throw std::invalid_argument("Audio artifacts support mono or stereo PCM.");
        if (audio.Samples.empty() || audio.Samples.size() % audio.Channels != 0u)
            throw std::invalid_argument("Audio artifact sample count does not match its channel count.");
        const std::uint64_t sampleBytes = static_cast<std::uint64_t>(audio.Samples.size()) * sizeof(float);
        if (sampleBytes + audio_artifact_detail::HeaderBytes > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("Audio artifact exceeds the derived-data payload safety limit.");
        for (const float sample : audio.Samples)
            if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f)
                throw std::invalid_argument("Audio artifact samples must be finite normalized PCM.");
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeAudioArtifactData(
        const AudioArtifactData& audio)
    {
        using namespace audio_artifact_detail;
        ValidateAudioArtifactData(audio);
        BinaryWriter writer(static_cast<std::size_t>(HeaderBytes) +
            audio.Samples.size() * sizeof(float));
        writer.WriteBytes(Magic);
        writer.WriteU32(PayloadVersion);
        writer.WriteU32(audio.SampleRate);
        writer.WriteU32(audio.Channels);
        writer.WriteU64(static_cast<std::uint64_t>(audio.FrameCount()));
        for (const float sample : audio.Samples) writer.WriteF32(sample);
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline AudioArtifactData ParseAudioArtifactData(
        std::span<const std::byte> payload)
    {
        using namespace audio_artifact_detail;
        BinaryReader reader(payload);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Audio artifact magic is invalid.");
        if (reader.ReadU32() != PayloadVersion)
            throw std::invalid_argument("Audio artifact payload version is unsupported.");

        AudioArtifactData audio;
        audio.SampleRate = reader.ReadU32();
        audio.Channels = reader.ReadU32();
        const std::uint64_t frames = reader.ReadU64();
        if (audio.Channels != 1u && audio.Channels != 2u)
            throw std::invalid_argument("Audio artifact channel count is invalid.");
        if (frames == 0u || frames >
            (MaximumDerivedArtifactPayloadBytes - HeaderBytes) /
                (sizeof(float) * audio.Channels))
            throw std::length_error("Audio artifact frame count is invalid.");
        const std::uint64_t sampleCount = frames * audio.Channels;
        const std::uint64_t expectedBytes = sampleCount * sizeof(float);
        if (expectedBytes != reader.Remaining())
            throw std::invalid_argument("Audio artifact PCM byte count does not match its header.");
        audio.Samples.reserve(static_cast<std::size_t>(sampleCount));
        for (std::uint64_t index = 0u; index < sampleCount; ++index)
            audio.Samples.push_back(reader.ReadF32());
        reader.RequireEnd();
        ValidateAudioArtifactData(audio);
        return audio;
    }

    [[nodiscard]] inline DerivedArtifact MakeAudioDerivedArtifact(
        const AudioArtifactData& audio)
    {
        return { AssetType::Audio, 1u, "kairo.audio.pcm.v1",
            SerializeAudioArtifactData(audio) };
    }

    [[nodiscard]] inline AudioArtifactData ParseAudioDerivedArtifact(
        const DerivedArtifact& artifact)
    {
        ValidateDerivedArtifact(artifact);
        if (artifact.Type != AssetType::Audio || artifact.FormatVersion != 1u ||
            artifact.Format != "kairo.audio.pcm.v1")
            throw std::invalid_argument("Derived artifact is not supported Kairo PCM audio.");
        return ParseAudioArtifactData(artifact.Payload);
    }
}
