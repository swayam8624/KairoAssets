module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

export module Kairo.Assets.Registry;

import Kairo.Assets.AssetID;
import Kairo.Assets.Metadata;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Thread-safe owner of project asset metadata and dependency topology.
    ///
    /// Task: provide stable identity/path lookup with strong validation while
    /// leaving decoding, GPU upload, audio creation, and file watching to
    /// importer/runtime layers. Read APIs return copies so callers never retain
    /// references across concurrent registry mutation.
    class AssetRegistry final
    {
    public:
        /// Input: validated registration whose dependencies already exist.
        /// Output: newly generated persistent identity.
        AssetID Create(AssetRegistration registration)
        {
            AssetMetadata metadata;
            metadata.Type = registration.Type;
            metadata.Origin = registration.Origin;
            metadata.Path = NormalizeAssetPath(registration.Path);
            metadata.Importer = std::move(registration.Importer);
            metadata.Dependencies = std::move(registration.Dependencies);
            const std::string pathKey = PortableAssetPathKey(metadata.Path);

            std::unique_lock lock(m_Mutex);
            if (m_Paths.contains(pathKey)) throw std::invalid_argument("Asset path is already registered.");
            for (std::uint32_t attempt = 0u; attempt < 128u; ++attempt)
            {
                metadata.ID = GenerateAssetID();
                if (m_Assets.contains(metadata.ID)) continue;
                ValidateAssetMetadata(metadata);
                ValidateDependencies(metadata, m_Assets);
                const AssetID id = metadata.ID;
                m_Paths.emplace(pathKey, id);
                m_Assets.emplace(id, std::move(metadata));
                return id;
            }
            throw std::runtime_error("Asset ID generation exhausted its collision retry budget.");
        }

        /// Input: complete persisted metadata, typically from a manifest.
        /// Task: insert exactly the supplied identity. Duplicate IDs/portable
        /// paths, missing dependencies, type mismatches, and cycles are rejected.
        void Insert(AssetMetadata metadata)
        {
            metadata.Path = NormalizeAssetPath(metadata.Path);
            ValidateAssetMetadata(metadata);
            std::unique_lock lock(m_Mutex);
            if (m_Assets.contains(metadata.ID)) throw std::invalid_argument("Asset ID is already registered.");
            const std::string pathKey = PortableAssetPathKey(metadata.Path);
            if (m_Paths.contains(pathKey)) throw std::invalid_argument("Asset path is already registered.");
            ValidateDependencies(metadata, m_Assets);
            m_Paths.emplace(pathKey, metadata.ID);
            m_Assets.emplace(metadata.ID, std::move(metadata));
        }

        [[nodiscard]] bool Contains(AssetID id) const
        {
            std::shared_lock lock(m_Mutex);
            return m_Assets.contains(id);
        }

        [[nodiscard]] std::optional<AssetMetadata> Find(AssetID id) const
        {
            std::shared_lock lock(m_Mutex);
            const auto found = m_Assets.find(id);
            if (found == m_Assets.end()) return std::nullopt;
            return found->second;
        }

        [[nodiscard]] AssetMetadata At(AssetID id) const
        {
            const auto metadata = Find(id);
            if (!metadata.has_value()) throw std::out_of_range("Asset ID is not registered.");
            return *metadata;
        }

        [[nodiscard]] std::optional<AssetMetadata> FindByPath(const std::filesystem::path& path) const
        {
            const std::string key = PortableAssetPathKey(path);
            std::shared_lock lock(m_Mutex);
            const auto pathEntry = m_Paths.find(key);
            if (pathEntry == m_Paths.end()) return std::nullopt;
            return m_Assets.at(pathEntry->second);
        }

        template<AssetType ExpectedType>
        [[nodiscard]] AssetMetadata Resolve(AssetHandle<ExpectedType> handle) const
        {
            if (!handle.IsValid()) throw std::invalid_argument("Cannot resolve an invalid typed asset handle.");
            AssetMetadata metadata = At(handle.ID);
            if (metadata.Type != ExpectedType) throw std::invalid_argument("Typed asset handle does not match registry metadata.");
            return metadata;
        }

        /// Input: a new portable project path.
        /// Output: increments the metadata revision after an atomic path-index update.
        void Move(AssetID id, const std::filesystem::path& newPath)
        {
            const std::filesystem::path normalized = NormalizeAssetPath(newPath);
            const std::string newKey = PortableAssetPathKey(normalized);
            std::unique_lock lock(m_Mutex);
            auto found = m_Assets.find(id);
            if (found == m_Assets.end()) throw std::out_of_range("Cannot move an unknown asset.");
            const std::string oldKey = PortableAssetPathKey(found->second.Path);
            if (newKey == oldKey)
            {
                if (found->second.Path != normalized)
                {
                    EnsureRevisionCanIncrement(found->second);
                    IncrementRevision(found->second);
                }
                found->second.Path = normalized;
                return;
            }
            if (m_Paths.contains(newKey)) throw std::invalid_argument("Cannot move an asset onto a registered path.");
            EnsureRevisionCanIncrement(found->second);
            m_Paths.erase(oldKey);
            m_Paths.emplace(newKey, id);
            found->second.Path = normalized;
            IncrementRevision(found->second);
        }

        /// Task: replace dependencies atomically after checking existence,
        /// declared types, duplicates, self-reference, and graph cycles.
        void SetDependencies(AssetID id, std::vector<AssetReference> dependencies)
        {
            std::unique_lock lock(m_Mutex);
            auto found = m_Assets.find(id);
            if (found == m_Assets.end()) throw std::out_of_range("Cannot update dependencies for an unknown asset.");
            AssetMetadata candidate = found->second;
            candidate.Dependencies = std::move(dependencies);
            ValidateAssetMetadata(candidate);
            ValidateDependencies(candidate, m_Assets);
            EnsureRevisionCanIncrement(found->second);
            found->second.Dependencies = std::move(candidate.Dependencies);
            IncrementRevision(found->second);
        }

        /// Precondition: no other registered asset depends on id.
        /// Task: prevent accidental dangling references; callers must update or
        /// remove dependents explicitly before deleting the dependency.
        void Remove(AssetID id)
        {
            std::unique_lock lock(m_Mutex);
            const auto found = m_Assets.find(id);
            if (found == m_Assets.end()) throw std::out_of_range("Cannot remove an unknown asset.");
            for (const auto& [otherID, metadata] : m_Assets)
                if (otherID != id && std::ranges::any_of(metadata.Dependencies,
                    [id](const AssetReference& dependency) { return dependency.ID == id; }))
                    throw std::logic_error("Cannot remove an asset that still has dependents.");
            m_Paths.erase(PortableAssetPathKey(found->second.Path));
            m_Assets.erase(found);
        }

        /// Output: deterministic ID-sorted metadata for manifests and tools.
        [[nodiscard]] std::vector<AssetMetadata> Snapshot() const
        {
            std::shared_lock lock(m_Mutex);
            std::vector<AssetMetadata> result;
            result.reserve(m_Assets.size());
            for (const auto& [id, metadata] : m_Assets) result.push_back(metadata);
            std::ranges::sort(result, {}, &AssetMetadata::ID);
            return result;
        }

        [[nodiscard]] std::size_t Size() const
        {
            std::shared_lock lock(m_Mutex);
            return m_Assets.size();
        }

    private:
        mutable std::shared_mutex m_Mutex;
        std::unordered_map<AssetID, AssetMetadata, AssetIDHash> m_Assets;
        std::unordered_map<std::string, AssetID> m_Paths;

        using AssetMap = std::unordered_map<AssetID, AssetMetadata, AssetIDHash>;

        static void ValidateDependencies(const AssetMetadata& candidate, const AssetMap& assets)
        {
            for (const AssetReference& dependency : candidate.Dependencies)
            {
                const auto target = assets.find(dependency.ID);
                if (target == assets.end()) throw std::invalid_argument("Asset dependency is not registered.");
                if (target->second.Type != dependency.Type) throw std::invalid_argument("Asset dependency type does not match its metadata.");
                std::unordered_set<AssetID, AssetIDHash> visited;
                if (Reaches(dependency.ID, candidate.ID, assets, visited))
                    throw std::invalid_argument("Asset dependency graph cannot contain a cycle.");
            }
        }

        [[nodiscard]] static bool Reaches(AssetID current, AssetID target, const AssetMap& assets,
            std::unordered_set<AssetID, AssetIDHash>& visited)
        {
            if (current == target) return true;
            if (!visited.insert(current).second) return false;
            const auto found = assets.find(current);
            if (found == assets.end()) return false;
            for (const AssetReference& dependency : found->second.Dependencies)
                if (Reaches(dependency.ID, target, assets, visited)) return true;
            return false;
        }

        static void EnsureRevisionCanIncrement(const AssetMetadata& metadata)
        {
            if (metadata.Revision == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Asset metadata revision is exhausted.");
        }

        static void IncrementRevision(AssetMetadata& metadata) noexcept
        {
            ++metadata.Revision;
        }
    };
}
