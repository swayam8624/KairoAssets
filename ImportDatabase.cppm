module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// TEACHING PROVENANCE: this database owns the LAST SUCCESSFUL recipe per
// stable AssetID. It does not own source pixels, derived cache bytes or GPU
// uploads. ImportSourceAsset updates the record only after a typed artifact
// has been decoded from cache or generated/published successfully.
export module Kairo.Assets.ImportDatabase;

import Kairo.Assets.AssetID;
import Kairo.Assets.Fingerprint;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Task: keep importer identity portable across manifests, cache recipes,
    /// plugin registries, and operating systems.
    inline void ValidateImporterIdentifier(std::string_view identifier)
    {
        if (identifier.empty() || identifier.size() > 128u)
            throw std::invalid_argument("Importer identifier must contain 1 to 128 characters.");
        for (const char character : identifier)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (!std::isalnum(byte) && byte != '.' && byte != '_' && byte != '-')
                throw std::invalid_argument("Importer identifier contains an unsupported character.");
        }
    }

    /// Versions participate in derived-data keys and therefore use a compact,
    /// locale-independent alphabet suitable for semantic versions and hashes.
    inline void ValidateImporterVersion(std::string_view version)
    {
        if (version.empty() || version.size() > 128u)
            throw std::invalid_argument("Importer version must contain 1 to 128 characters.");
        for (const char character : version)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (!std::isalnum(byte) && byte != '.' && byte != '_' && byte != '-' && byte != '+')
                throw std::invalid_argument("Importer version contains an unsupported character.");
        }
    }

    /// Settings are importer-defined canonical text. NUL is forbidden because
    /// these bytes are persisted and combined with delimiter-based cache recipes.
    inline void ValidateCanonicalImporterSettings(std::string_view settings)
    {
        if (settings.size() > 64u * 1024u)
            throw std::length_error("Import settings exceed the 64 KiB safety limit.");
        if (settings.find('\0') != std::string_view::npos)
            throw std::invalid_argument("Import settings cannot contain NUL bytes.");
    }

    /// Persistent provenance for one imported source asset. `Source` is a
    /// project-relative input path; it is deliberately distinct from a cache
    /// path because cache layout is an A4 concern. Settings must be serialized
    /// canonically by the importer so equivalent requests compare byte-for-byte.
    // Persistent source identity + project-relative path + plugin version
    // + canonical settings + SHA-256 fingerprint + monotone revision.
    // DerivedDataKey is computed from content/type/recipe, not just Revision.
    struct ImportRecord final
    {
        AssetID Asset;
        std::filesystem::path Source;
        std::string Importer;
        std::string ImporterVersion;
        std::string CanonicalSettings;
        AssetFingerprint SourceFingerprint;
        std::uint64_t Revision = 1u;

        friend bool operator==(const ImportRecord&, const ImportRecord&) = default;
    };

    /// Result of comparing a source file with the last successful import.
    enum class SourceImportState { Current, Missing, Changed };

    /// Input: a record and its owning metadata registry. Task: reject records
    /// that cannot be reproduced from the authoritative project registry.
    /// Source-file metadata and importer names must agree; generated/builtin
    /// assets cannot be represented by this source-import database.
    inline void ValidateImportRecord(const ImportRecord& record, const AssetRegistry& registry)
    {
        if (!record.Asset.IsValid()) throw std::invalid_argument("Import record requires a valid asset ID.");
        (void)NormalizeAssetPath(record.Source);
        ValidateImporterIdentifier(record.Importer);
        ValidateImporterVersion(record.ImporterVersion);
        ValidateCanonicalImporterSettings(record.CanonicalSettings);
        if (record.Revision == 0u) throw std::invalid_argument("Import record revision must be positive.");

        const AssetMetadata metadata = registry.At(record.Asset);
        if (metadata.Origin != AssetOrigin::SourceFile)
            throw std::invalid_argument("Only source-file assets may have import provenance.");
        if (metadata.Importer != record.Importer)
            throw std::invalid_argument("Import record importer must match asset metadata.");
    }

    /// Thread-safe database of last-successful source imports. It does not
    /// observe the filesystem, import bytes, or create cache artifacts; those
    /// responsibilities remain explicit future services. Snapshot APIs return
    /// values, so callers never retain aliases across concurrent mutation.
    // Concurrent readers acquire shared_lock; mutation uses unique_lock.
    // Find/Snapshot return COPIES rather than borrowed references to map
    // nodes, so a second importing worker cannot invalidate a caller's view.
    class ImportDatabase final
    {
    public:
        /// Input: a fully formed successful import record. Output: atomically
        /// replaces the record for its asset after registry validation.
        // Normalize/validate before taking the exclusive lock. Replacing
        // provenance is a successful-import publication step, not an importer
        // plugin side effect. ImportSourceAsset owns the operation order.
        void Upsert(ImportRecord record, const AssetRegistry& registry)
        {
            record.Source = NormalizeAssetPath(record.Source);
            ValidateImportRecord(record, registry);
            std::unique_lock lock(m_Mutex);
            m_Records.insert_or_assign(record.Asset, std::move(record));
        }

        /// Input: an arbitrary complete snapshot. Task: validate all entries
        /// before taking the lock, giving replacement a strong exception
        /// guarantee suitable for project reload.
        // Build a fully validated detached candidate map, reject duplicate
        // AssetIDs, then atomically swap under lock. A malformed project
        // snapshot cannot half-replace live import history.
        void ReplaceAll(std::vector<ImportRecord> records, const AssetRegistry& registry)
        {
            RecordMap candidate;
            candidate.reserve(records.size());
            for (ImportRecord& record : records)
            {
                record.Source = NormalizeAssetPath(record.Source);
                ValidateImportRecord(record, registry);
                if (!candidate.emplace(record.Asset, std::move(record)).second)
                    throw std::invalid_argument("Import database contains duplicate asset records.");
            }
            std::unique_lock lock(m_Mutex);
            m_Records.swap(candidate);
        }

        [[nodiscard]] std::optional<ImportRecord> Find(AssetID asset) const
        {
            std::shared_lock lock(m_Mutex);
            const auto found = m_Records.find(asset);
            return found == m_Records.end() ? std::nullopt : std::optional<ImportRecord>{ found->second };
        }

        [[nodiscard]] ImportRecord At(AssetID asset) const
        {
            const auto record = Find(asset);
            if (!record) throw std::out_of_range("Asset has no import provenance record.");
            return *record;
        }

        [[nodiscard]] std::vector<ImportRecord> Snapshot() const
        {
            std::shared_lock lock(m_Mutex);
            std::vector<ImportRecord> result;
            result.reserve(m_Records.size());
            for (const auto& [id, record] : m_Records) result.push_back(record);
            std::ranges::sort(result, {}, &ImportRecord::Asset);
            return result;
        }

        /// Input: project root and an asset with a provenance record. Output:
        /// Current, Missing, or Changed based solely on content identity. The
        /// root/source join is normalized and checked to retain the project
        /// boundary established by NormalizeAssetPath().
        // Re-fingerprint current project-local source bytes, compare to last
        // successful ImportRecord. Current/Missing/Changed says whether the
        // input bytes moved—not whether a renderer TextureHandle is live.
        [[nodiscard]] SourceImportState Evaluate(const std::filesystem::path& projectRoot, AssetID asset) const
        {
            if (projectRoot.empty()) throw std::invalid_argument("Import evaluation requires a project root.");
            const ImportRecord record = At(asset);
            const std::filesystem::path source = projectRoot / record.Source;
            std::error_code error;
            if (!std::filesystem::is_regular_file(source, error) || error) return SourceImportState::Missing;
            return FingerprintFile(source) == record.SourceFingerprint
                ? SourceImportState::Current : SourceImportState::Changed;
        }

        [[nodiscard]] std::size_t Size() const
        {
            std::shared_lock lock(m_Mutex);
            return m_Records.size();
        }

    private:
        using RecordMap = std::unordered_map<AssetID, ImportRecord, AssetIDHash>;
        mutable std::shared_mutex m_Mutex;
        RecordMap m_Records;
    };
}
