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
            // Each process starts its local counter at zero. A plain
            // "key.tmp-0" file can therefore be truncated by *another*
            // process also publishing this key. create_directory() is an
            // atomic exclusive claim, so even colliding names cannot clobber
            // someone else's in-progress staging file.
            static std::atomic_uint64_t sequence = 0u;
            std::filesystem::path staging;
            bool claimed = false;
            for (unsigned attempt = 0u; attempt < 1024u; ++attempt)
            {
                staging = target.string() + ".stage-" +
                    std::to_string(sequence.fetch_add(1u));
                error.clear();
                if (std::filesystem::create_directory(staging, error))
                {
                    claimed = true;
                    break;
                }
                if (error && error != std::errc::file_exists)
                    throw std::runtime_error(
                        "Unable to claim derived cache staging directory: " +
                        error.message());
            }
            if (!claimed)
                throw std::runtime_error(
                    "Derived cache staging sequence exhausted its retry budget.");

            const std::filesystem::path temporary = staging / "payload.kdd";
            try
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output || (!bytes.empty() && !output.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))))
                    throw std::runtime_error(
                        "Unable to write derived cache staging file: " + temporary.string());
                output.close();
                if (!output)
                    throw std::runtime_error(
                        "Unable to close derived cache staging file: " + temporary.string());

                // std::filesystem::rename() silently REPLACES the destination
                // on POSIX: a second writer can overwrite an immutable key
                // between Contains() and rename(). Hard-link creation is
                // atomic and fails if the target already exists on POSIX,
                // APFS and NTFS, while staging and target share a directory
                // (and therefore a filesystem).
                error.clear();
                std::filesystem::create_hard_link(temporary, target, error);
                if (error)
                {
                    if (Contains(key))
                    {
                        if (std::ranges::equal(Load(key), bytes))
                        {
                            std::filesystem::remove_all(staging);
                            return;
                        }
                        throw std::logic_error(
                            "Derived cache key already contains different bytes.");
                    }
                    throw std::runtime_error(
                        "Unable to publish immutable derived cache entry: " +
                        error.message());
                }
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove_all(staging, ignored);
                throw;
            }
            std::filesystem::remove_all(staging, error);
            if (error)
                throw std::runtime_error(
                    "Unable to clean derived cache staging directory: " + error.message());
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
