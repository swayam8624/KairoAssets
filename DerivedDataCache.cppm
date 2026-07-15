module;

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

export module Kairo.Assets.DerivedDataCache;

import Kairo.Assets.Fingerprint;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Immutable content address for a derived importer artifact. It is the
    /// SHA-256 identity of the full canonical recipe, not a source timestamp.
    struct DerivedDataKey final
    {
        AssetFingerprint Fingerprint;

        [[nodiscard]] std::string ToString() const { return Fingerprint.ToHex(); }
        [[nodiscard]] static DerivedDataKey Parse(std::string_view value)
        {
            return { AssetFingerprint::Parse(value, 0u) };
        }
        friend constexpr bool operator==(const DerivedDataKey&, const DerivedDataKey&) noexcept = default;
    };

    /// Input: source identity plus canonical importer identity/settings.
    /// Output: portable cache key. Task: ensure every byte that can change an
    /// importer result participates in cache invalidation.
    [[nodiscard]] inline DerivedDataKey MakeDerivedDataKey(const AssetFingerprint& source,
        AssetType expectedType, std::string_view importer, std::string_view importerVersion,
        std::string_view canonicalSettings)
    {
        const auto parsedType = ParseAssetType(NameOfAssetType(expectedType));
        if (!parsedType || *parsedType != expectedType)
            throw std::invalid_argument("Derived data key requires a valid asset type.");
        std::string recipe;
        recipe.reserve(80u + importer.size() + importerVersion.size() + canonicalSettings.size());
        recipe += source.ToHex(); recipe += ':'; recipe += std::to_string(source.ByteCount); recipe += '\n';
        recipe += NameOfAssetType(expectedType); recipe += '\n';
        recipe += importer; recipe += '\n'; recipe += importerVersion; recipe += '\n'; recipe += canonicalSettings;
        const auto* bytes = reinterpret_cast<const std::byte*>(recipe.data());
        AssetFingerprint fingerprint = FingerprintBytes({ bytes, recipe.size() });
        // A derived key is a fixed digest token. Unlike a source fingerprint,
        // its recipe byte count is not part of the persisted identity.
        fingerprint.ByteCount = 0u;
        return { fingerprint };
    }

    /// Disk-backed, content-addressed derived-data cache. Cache misses are
    /// normal and never imply a source error. Writes stage beside their target
    /// before replacement so a reader observes either a previous complete
    /// artifact or a complete new artifact, never a partial byte stream.
    class DerivedDataCache final
    {
    public:
        static constexpr std::size_t MaximumEntryBytes = 1024u * 1024u * 1024u + 4096u;

        explicit DerivedDataCache(std::filesystem::path root) : m_Root(std::move(root))
        {
            if (m_Root.empty()) throw std::invalid_argument("Derived data cache requires a root directory.");
        }

        [[nodiscard]] bool Contains(const DerivedDataKey& key) const
        {
            std::error_code error;
            return std::filesystem::is_regular_file(PathFor(key), error) && !error;
        }

        [[nodiscard]] std::vector<std::byte> Load(const DerivedDataKey& key) const
        {
            const std::filesystem::path path = PathFor(key);
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error) throw std::out_of_range("Derived cache entry does not exist: " + key.ToString());
            if (size > MaximumEntryBytes) throw std::length_error("Derived cache entry exceeds its safety limit.");
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream input(path, std::ios::binary);
            if (!input || (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Unable to read derived cache entry: " + path.string());
            return bytes;
        }

        void Store(const DerivedDataKey& key, std::span<const std::byte> bytes) const
        {
            if (bytes.size() > MaximumEntryBytes)
                throw std::length_error("Derived cache entry exceeds its safety limit.");
            const std::filesystem::path target = PathFor(key);
            if (Contains(key))
            {
                const std::vector<std::byte> existing = Load(key);
                if (std::ranges::equal(existing, bytes)) return;
                throw std::logic_error("Derived cache key already contains different bytes.");
            }
            std::error_code error;
            std::filesystem::create_directories(target.parent_path(), error);
            if (error) throw std::runtime_error("Unable to create derived cache directory: " + error.message());
            static std::atomic_uint64_t sequence = 0u;
            const std::filesystem::path temporary = target.string() + ".tmp-" + std::to_string(sequence.fetch_add(1u));
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output || (!bytes.empty() && !output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))))
                    throw std::runtime_error("Unable to write derived cache staging file: " + temporary.string());
            }
            std::filesystem::rename(temporary, target, error);
            if (error)
            {
                std::filesystem::remove(temporary);
                // Another process may have won the same content-addressed
                // publication race on platforms that forbid replace-by-rename.
                if (Contains(key) && std::ranges::equal(Load(key), bytes)) return;
                throw std::runtime_error("Unable to publish derived cache entry: " + error.message());
            }
        }

        [[nodiscard]] std::filesystem::path PathFor(const DerivedDataKey& key) const
        {
            const std::string value = key.ToString();
            return m_Root / value.substr(0u, 2u) / (value + ".kdd");
        }

    private:
        std::filesystem::path m_Root;
    };
}
