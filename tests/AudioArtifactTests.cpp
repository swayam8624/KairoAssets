#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import Kairo.Assets.AudioArtifact;
import Kairo.Assets.Importer;
import Kairo.Assets.WavImporter;

namespace assets = kairo::assets;
using Catch::Approx;

namespace
{
    void PushText(std::vector<std::byte>& bytes, const char* text, std::size_t count)
    {
        for (std::size_t index = 0u; index < count; ++index)
            bytes.push_back(std::byte{ static_cast<unsigned char>(text[index]) });
    }

    void PushU16(std::vector<std::byte>& bytes, std::uint16_t value)
    {
        bytes.push_back(std::byte{ static_cast<std::uint8_t>(value) });
        bytes.push_back(std::byte{ static_cast<std::uint8_t>(value >> 8u) });
    }

    void PushU32(std::vector<std::byte>& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0u; shift < 32u; shift += 8u)
            bytes.push_back(std::byte{ static_cast<std::uint8_t>(value >> shift) });
    }

    std::vector<std::byte> MakePcm16Wav(std::uint16_t channels,
        std::uint32_t sampleRate, std::span<const std::int16_t> samples)
    {
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * 2u);
        std::vector<std::byte> bytes;
        bytes.reserve(44u + dataBytes);
        PushText(bytes, "RIFF", 4u);
        PushU32(bytes, 36u + dataBytes);
        PushText(bytes, "WAVE", 4u);
        PushText(bytes, "fmt ", 4u);
        PushU32(bytes, 16u);
        PushU16(bytes, 1u);
        PushU16(bytes, channels);
        PushU32(bytes, sampleRate);
        const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * 2u);
        PushU32(bytes, sampleRate * blockAlign);
        PushU16(bytes, blockAlign);
        PushU16(bytes, 16u);
        PushText(bytes, "data", 4u);
        PushU32(bytes, dataBytes);
        for (const std::int16_t sample : samples)
            PushU16(bytes, static_cast<std::uint16_t>(sample));
        return bytes;
    }

    assets::DerivedArtifact ImportWav(const std::vector<std::byte>& wav,
        std::string settings = "normalize=0")
    {
        assets::WavAudioImporter importer;
        assets::ImportRecord record;
        record.CanonicalSettings = std::move(settings);
        return importer.Import({
            .Record = std::move(record),
            .ExpectedType = assets::AssetType::Audio,
            .SourceBytes = wav,
            .SourcePath = "test.wav"
        });
    }
}

TEST_CASE("Audio artifact round trips normalized PCM deterministically",
    "[KairoAssets][Audio]")
{
    const assets::AudioArtifactData source{
        .SampleRate = 48'000u,
        .Channels = 2u,
        .Samples = { 0.0f, 0.25f, -0.5f, 1.0f }
    };
    const auto bytes = assets::SerializeAudioArtifactData(source);
    const auto parsed = assets::ParseAudioArtifactData(bytes);
    CHECK(parsed == source);
    CHECK(parsed.FrameCount() == 2u);
    CHECK(parsed.DurationSeconds() == Approx(2.0 / 48'000.0));

    const auto derived = assets::MakeAudioDerivedArtifact(source);
    CHECK(derived.Type == assets::AssetType::Audio);
    CHECK(assets::ParseAudioDerivedArtifact(derived) == source);
}

TEST_CASE("WAV importer converts PCM16 mono into portable float audio",
    "[KairoAssets][Audio][WAV]")
{
    const std::vector<std::int16_t> samples{ 0, 16'384, -32'768, 32'767 };
    const auto wav = MakePcm16Wav(1u, 48'000u, samples);
    const auto audio = assets::ParseAudioDerivedArtifact(ImportWav(wav));

    REQUIRE(audio.Channels == 1u);
    REQUIRE(audio.SampleRate == 48'000u);
    REQUIRE(audio.Samples.size() == 4u);
    CHECK(audio.Samples[0] == Approx(0.0f));
    CHECK(audio.Samples[1] == Approx(0.5f));
    CHECK(audio.Samples[2] == Approx(-1.0f));
    CHECK(audio.Samples[3] == Approx(32'767.0 / 32'768.0));
}

TEST_CASE("WAV peak normalization is explicit and canonical",
    "[KairoAssets][Audio][WAV]")
{
    const std::vector<std::int16_t> samples{ 8'192, -16'384 };
    const auto wav = MakePcm16Wav(1u, 44'100u, samples);
    const auto audio = assets::ParseAudioDerivedArtifact(
        ImportWav(wav, "normalize=1"));
    REQUIRE(audio.Samples.size() == 2u);
    CHECK(audio.Samples[0] == Approx(0.5f));
    CHECK(audio.Samples[1] == Approx(-1.0f));
    CHECK(assets::CanonicalWavImportSettings({ .NormalizePeak = true }) ==
        "normalize=1");
}

TEST_CASE("WAV importer rejects malformed and unsupported source contracts",
    "[KairoAssets][Audio][WAV]")
{
    std::vector<std::int16_t> samples{ 0, 0, 0 };
    auto wav = MakePcm16Wav(3u, 48'000u, samples);
    REQUIRE_THROWS_AS(ImportWav(wav), std::invalid_argument);

    wav = MakePcm16Wav(1u, 48'000u, samples);
    wav[0] = std::byte{'X'};
    REQUIRE_THROWS_AS(ImportWav(wav), std::invalid_argument);

    wav = MakePcm16Wav(1u, 48'000u, samples);
    wav.pop_back();
    REQUIRE_THROWS_AS(ImportWav(wav), std::invalid_argument);
    REQUIRE_THROWS_AS(ImportWav(MakePcm16Wav(1u, 48'000u, samples),
        "normalize=yes"), std::invalid_argument);
}

TEST_CASE("Audio artifact validation rejects non-normalized or non-finite PCM",
    "[KairoAssets][Audio][Validation]")
{
    assets::AudioArtifactData audio{
        .SampleRate = 48'000u,
        .Channels = 1u,
        .Samples = { 1.25f }
    };
    REQUIRE_THROWS_AS(assets::ValidateAudioArtifactData(audio), std::invalid_argument);

    audio.Samples = {};
    REQUIRE_THROWS_AS(assets::ValidateAudioArtifactData(audio), std::invalid_argument);
}
