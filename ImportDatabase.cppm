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

export module Kairo.Assets.ImportDatabase;

import Kairo.Assets.AssetID;
import Kairo.Assets.Fingerprint;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Persistent provenance for one imported source asset. `Source` is a
    /// project-relative input path; it is deliberately distinct from a cache
    /// path because cache layout is an A4 concern. Settings must be serialized
    /// canonically by the importer so equivalent requests compare byte-for-byte.
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
        if (record.Importer.empty() || record.Importer.size() > 128u)
            throw std::invalid_argument("Import record importer must contain 1 to 128 characters.");
        for (const char character : record.Importer)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (!std::isalnum(byte) && byte != '.' && byte != '_' && byte != '-')
                throw std::invalid_argument("Import record importer contains an unsupported character.");
        }
        if (record.ImporterVersion.empty() || record.ImporterVersion.size() > 128u)
            throw std::invalid_argument("Import record importer version must contain 1 to 128 characters.");
        if (record.CanonicalSettings.size() > 64u * 1024u)
            throw std::length_error("Import record settings exceed the 64 KiB safety limit.");
        if (record.CanonicalSettings.find('\0') != std::string::npos)
            throw std::invalid_argument("Import record settings cannot contain NUL bytes.");
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
    class ImportDatabase final
    {
    public:
        /// Input: a fully formed successful import record. Output: atomically
        /// replaces the record for its asset after registry validation.
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
