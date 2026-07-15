module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Assets.Importer;

import Kairo.Assets.DerivedDataCache;
import Kairo.Assets.Fingerprint;
import Kairo.Assets.ImportDatabase;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;

export namespace kairo::assets
{
    /// Input to an importer after the service has validated project ownership.
    struct ImportRequest final { ImportRecord Record; std::span<const std::byte> SourceBytes; };

    /// Pure importer plugin contract. Implementations transform source bytes
    /// into stable derived bytes; they do not mutate registries or caches.
    class AssetImporter
    {
    public:
        virtual ~AssetImporter() = default;
        [[nodiscard]] virtual std::string Identifier() const = 0;
        [[nodiscard]] virtual std::string Version() const = 0;
        [[nodiscard]] virtual std::vector<std::byte> Import(const ImportRequest& request) const = 0;
    };

    /// Actual baseline importer for opaque documents, scripts, and source
    /// artifacts whose derived representation is their original byte stream.
    class PassthroughImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.passthrough"; }
        [[nodiscard]] std::string Version() const override { return "1"; }
        [[nodiscard]] std::vector<std::byte> Import(const ImportRequest& request) const override
        { return { request.SourceBytes.begin(), request.SourceBytes.end() }; }
    };

    struct ImportOutcome final { DerivedDataKey Key; bool CacheHit = false; };

    /// Executes one import transaction. The cache publishes first; only then
    /// does provenance change, so failed importers never mark stale content as
    /// current. Existing cache entries are reused without invoking the plugin.
    [[nodiscard]] inline ImportOutcome ImportSourceAsset(const std::filesystem::path& projectRoot,
        ImportRecord record, const AssetImporter& importer, const AssetRegistry& registry,
        ImportDatabase& imports, const DerivedDataCache& cache)
    {
        if (projectRoot.empty()) throw std::invalid_argument("Import requires a project root.");
        if (record.Importer != importer.Identifier() || record.ImporterVersion != importer.Version())
            throw std::invalid_argument("Import record does not match the selected importer implementation.");
        const std::filesystem::path source = projectRoot / NormalizeAssetPath(record.Source);
        std::ifstream input(source, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Unable to open import source: " + source.string());
        const auto end = input.tellg();
        if (end < 0 || static_cast<std::uintmax_t>(end) > 1024u * 1024u * 1024u)
            throw std::length_error("Import source exceeds the 1 GiB safety limit.");
        std::vector<std::byte> bytes(static_cast<std::size_t>(end)); input.seekg(0);
        if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Unable to read import source: " + source.string());
        record.SourceFingerprint = FingerprintBytes(bytes);
        const DerivedDataKey key = MakeDerivedDataKey(record.SourceFingerprint, record.Importer, record.ImporterVersion, record.CanonicalSettings);
        const bool hit = cache.Contains(key);
        if (!hit) cache.Store(key, importer.Import({ record, bytes }));
        if (const auto prior = imports.Find(record.Asset); prior && prior->SourceFingerprint != record.SourceFingerprint)
            record.Revision = prior->Revision + 1u;
        imports.Upsert(std::move(record), registry);
        return { key, hit };
    }
}
