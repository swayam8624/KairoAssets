module;

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Assets.ImporterRegistry;

import Kairo.Assets.Importer;
import Kairo.Assets.ImportDatabase;

export namespace kairo::assets
{
    /// Stable public identity for diagnostics, project migration tools, and
    /// deterministic registry snapshots. Identifier plus version is the key;
    /// several versions may coexist when an old project needs reproducibility.
    struct ImporterDescriptor final
    {
        std::string Identifier;
        std::string Version;

        friend bool operator==(const ImporterDescriptor&, const ImporterDescriptor&) = default;
    };

    /// Thread-safe owner of immutable importer plugins.
    ///
    /// Task: decouple import orchestration from concrete decoder classes while
    /// preserving exact version selection. Registration is explicit and never
    /// loads dynamic libraries; a future plugin loader can validate a library
    /// and then publish its AssetImporter instance through this same boundary.
    class ImporterRegistry final
    {
    public:
        /// Input: non-null plugin with a valid portable identifier and version.
        /// Duplicate exact identities are rejected rather than silently replaced.
        void Register(std::shared_ptr<const AssetImporter> importer)
        {
            if (!importer) throw std::invalid_argument("Cannot register a null asset importer.");
            ImporterDescriptor descriptor{ importer->Identifier(), importer->Version() };
            ValidateImporterIdentifier(descriptor.Identifier);
            ValidateImporterVersion(descriptor.Version);

            std::unique_lock lock(m_Mutex);
            const Key key{ descriptor.Identifier, descriptor.Version };
            if (!m_Importers.emplace(key, std::move(importer)).second)
                throw std::invalid_argument("Asset importer identity is already registered.");
        }

        [[nodiscard]] bool Contains(std::string_view identifier, std::string_view version) const
        {
            ValidateImporterIdentifier(identifier);
            ValidateImporterVersion(version);
            std::shared_lock lock(m_Mutex);
            return m_Importers.contains(Key{ identifier, version });
        }

        /// Output: shared immutable plugin ownership, or null when no exact
        /// identifier/version pair is registered.
        [[nodiscard]] std::shared_ptr<const AssetImporter> Find(
            std::string_view identifier, std::string_view version) const
        {
            ValidateImporterIdentifier(identifier);
            ValidateImporterVersion(version);
            std::shared_lock lock(m_Mutex);
            const auto found = m_Importers.find(Key{ identifier, version });
            return found == m_Importers.end() ? nullptr : found->second;
        }

        /// Output: exact plugin or a descriptive lookup failure. No fallback to
        /// another version is allowed because it would invalidate reproducibility.
        [[nodiscard]] std::shared_ptr<const AssetImporter> Resolve(
            std::string_view identifier, std::string_view version) const
        {
            auto importer = Find(identifier, version);
            if (!importer)
                throw std::out_of_range("Asset importer version is not registered: " +
                    std::string(identifier) + "@" + std::string(version));
            return importer;
        }

        /// Output: identifier/version sorted inventory for diagnostics and UI.
        [[nodiscard]] std::vector<ImporterDescriptor> Snapshot() const
        {
            std::shared_lock lock(m_Mutex);
            std::vector<ImporterDescriptor> result;
            result.reserve(m_Importers.size());
            for (const auto& [key, importer] : m_Importers)
            {
                (void)importer;
                result.push_back({ key.first, key.second });
            }
            return result;
        }

        [[nodiscard]] std::size_t Size() const
        {
            std::shared_lock lock(m_Mutex);
            return m_Importers.size();
        }

    private:
        using Key = std::pair<std::string, std::string>;
        mutable std::shared_mutex m_Mutex;
        std::map<Key, std::shared_ptr<const AssetImporter>> m_Importers;
    };
}
