// TEACHING MAP: AssetRegistry is the authoritative metadata/dependency
// database. It indexes AssetID and normalized, case-folded portable paths;
// metadata references are returned as *copies* so a reader cannot outlive
// the lock protecting the maps. The importer/renderer own artifact bytes,
// decoded content and native GPU resources separately.
//
// Writer operations take an exclusive lock, and reads take a shared lock.
// ReplaceAll first constructs/validates new maps (including dependency
// cycles) and then swaps them under one lock: manifest parse failures
// cannot leave a partially replaced live registry. Move preserves AssetID
// while changing path/revision, and Remove rejects any remaining dependent.
// A dependency is a typed ID, never an implicit filesystem path.
//
// Downstream: AssetManifest serializes detached snapshots; ImportDatabase
// tracks source fingerprints; EngineCore scenes and Renderer's AssetAdapters
// resolve identities to artifacts and GPU handles. See tests/AssetTests.cpp.
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
        /// TEACHING METHOD: Register a new original asset with generated unique ID and validated portable path/dependencies. KairoEditor and import pipelines use the ID for stable references; rollback preserves registry consistency on failure.
        // Create: normalize portable path and acquire exclusive lock before
        // generating a unique ID. Validate metadata and typed dependencies; insert
        // both ID and folded path maps. Catch allocation failures to remove a
        // just-published path entry so readers cannot observe a ghost ID.
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
                try
                {
                    m_Assets.emplace(id, std::move(metadata));
                }
                catch (...)
                {
                    // Registry allocation failed after path registration.
                    // Never leave a ghost path for an ID that was not stored.
                    m_Paths.erase(pathKey);
                    throw;
                }
                return id;
            }
            throw std::runtime_error("Asset ID generation exhausted its collision retry budget.");
        }

        /// Input: complete persisted metadata, typically from a manifest.
        /// Task: insert exactly the supplied identity. Duplicate IDs/portable
        /// paths, missing dependencies, type mismatches, and cycles are rejected.
        /// TEACHING METHOD: Import already-identifiable metadata, rejecting duplicate IDs/paths and invalid dependency graphs. Used when building a registry from trusted manifests.
        // Insert: preserve an already-persisted AssetID (unlike Create) for manifest
        // replay. Reject duplicate ID/path and missing or mistyped dependency;
        // maintain both indices transactionally if metadata allocation throws.
        void Insert(AssetMetadata metadata)
        {
            metadata.Path = NormalizeAssetPath(metadata.Path);
            ValidateAssetMetadata(metadata);
            std::unique_lock lock(m_Mutex);
            if (m_Assets.contains(metadata.ID)) throw std::invalid_argument("Asset ID is already registered.");
            const std::string pathKey = PortableAssetPathKey(metadata.Path);
            if (m_Paths.contains(pathKey)) throw std::invalid_argument("Asset path is already registered.");
            ValidateDependencies(metadata, m_Assets);
            const AssetID id = metadata.ID;
            m_Paths.emplace(pathKey, id);
            try
            {
                m_Assets.emplace(id, std::move(metadata));
            }
            catch (...)
            {
                // Both lookup maps must describe exactly the same assets,
                // including under allocator exceptions.
                m_Paths.erase(pathKey);
                throw;
            }
        }

        /// Input: a complete manifest snapshot in arbitrary record order.
        /// Task: validate IDs, paths, dependency targets/types, and the full
        /// graph before replacing live state. Failure leaves this registry
        /// unchanged, providing the strong exception guarantee required by
        /// project reload and editor recovery workflows.
        /// TEACHING METHOD: Validate a complete candidate manifest off to the side, then atomically replace the live registry under lock. No partially loaded asset graph becomes visible.
        // Transaction boundary: normalize/validate a candidate graph in
        // isolation, then atomically swap it into the registry. No reader
        // observes partial replacement. This is used by manifest loading.
        // ReplaceAll: build and validate *both* candidate maps off to the side,
        // including a complete cycle check. Only after all records pass do we swap
        // both maps under one exclusive lock. A bad manifest must never partially
        // replace live project metadata.
        void ReplaceAll(std::vector<AssetMetadata> records)
        {
            AssetMap candidateAssets;
            std::unordered_map<std::string, AssetID> candidatePaths;
            candidateAssets.reserve(records.size());
            candidatePaths.reserve(records.size());
            for (AssetMetadata& metadata : records)
            {
                metadata.Path = NormalizeAssetPath(metadata.Path);
                ValidateAssetMetadata(metadata);
                const std::string key = PortableAssetPathKey(metadata.Path);
                if (!candidatePaths.emplace(key, metadata.ID).second)
                    throw std::invalid_argument("Asset manifest contains duplicate portable paths.");
                if (!candidateAssets.emplace(metadata.ID, std::move(metadata)).second)
                    throw std::invalid_argument("Asset manifest contains duplicate IDs.");
            }
            for (const auto& [id, metadata] : candidateAssets)
                ValidateDependencies(metadata, candidateAssets);

            std::unique_lock lock(m_Mutex);
            m_Assets.swap(candidateAssets);
            m_Paths.swap(candidatePaths);
        }

        /// TEACHING METHOD: Thread-safe membership query by stable AssetID; never infer an asset's type from existence alone.
        [[nodiscard]] bool Contains(AssetID id) const
        {
            std::shared_lock lock(m_Mutex);
            return m_Assets.contains(id);
        }

        /// TEACHING METHOD: Read-copy of metadata by ID under a shared mutex, returning nullopt when absent. Callers do not own registry internals.
        // Find: take a shared lock and return a detached copy. Holding a pointer into
        // m_Assets across Remove/ReplaceAll would be unsafe; a caller owns the
        // returned optional metadata value.
        [[nodiscard]] std::optional<AssetMetadata> Find(AssetID id) const
        {
            std::shared_lock lock(m_Mutex);
            const auto found = m_Assets.find(id);
            if (found == m_Assets.end()) return std::nullopt;
            return found->second;
        }

        /// TEACHING METHOD: Strict lookup variant that throws when the ID is missing; use Find when absence is expected.
        [[nodiscard]] AssetMetadata At(AssetID id) const
        {
            const auto metadata = Find(id);
            if (!metadata.has_value()) throw std::out_of_range("Asset ID is not registered.");
            return *metadata;
        }

        /// TEACHING METHOD: Normalize project-relative source path and query case-folded lookup; portable collision rules matter across macOS/Windows/Linux.
        // FindByPath: convert portable path to a case-folded key and use the
        // secondary index to resolve a stable ID. A path is mutable metadata, not the
        // persistent identity stored in scenes.
        [[nodiscard]] std::optional<AssetMetadata> FindByPath(const std::filesystem::path& path) const
        {
            const std::string key = PortableAssetPathKey(path);
            std::shared_lock lock(m_Mutex);
            const auto pathEntry = m_Paths.find(key);
            if (pathEntry == m_Paths.end()) return std::nullopt;
            return m_Assets.at(pathEntry->second);
        }

        template<AssetType ExpectedType>
        /// TEACHING METHOD: Checked typed-handle lookup: resolve ID then verify the declared asset type matches the handle's compile-time expectation.
        // Resolve: the template parameter encodes the expected asset category;
        // compare it against persisted metadata before handing data to a
        // renderer/importer. An ID existing does not make a wrong typed handle valid.
        [[nodiscard]] AssetMetadata Resolve(AssetHandle<ExpectedType> handle) const
        {
            if (!handle.IsValid()) throw std::invalid_argument("Cannot resolve an invalid typed asset handle.");
            AssetMetadata metadata = At(handle.ID);
            if (metadata.Type != ExpectedType) throw std::invalid_argument("Typed asset handle does not match registry metadata.");
            return metadata;
        }

        /// Input: a new portable project path.
        /// Output: increments the metadata revision after an atomic path-index update.
        /// TEACHING METHOD: Move one asset's portable path, validate uniqueness and bump revision. Stable AssetID remains the same for scene references.
        // Persistent identity does not change when a source file moves.
        // The path map is replaced only after uniqueness/allocations pass;
        // revision increments permit downstream provenance invalidation.
        // Move: preserve AssetID while updating spelling/path and revision. Check
        // folded destination uniqueness and revision overflow; install the new
        // path-map key before removing the old so exceptions cannot leave an orphaned
        // ID. Case-only renames are handled separately.
        void Move(AssetID id, const std::filesystem::path& newPath)
        {
            std::filesystem::path normalized = NormalizeAssetPath(newPath);
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
                    found->second.Path.swap(normalized);
                    IncrementRevision(found->second);
                }
                return;
            }
            if (m_Paths.contains(newKey)) throw std::invalid_argument("Cannot move an asset onto a registered path.");
            EnsureRevisionCanIncrement(found->second);
            // Inserting the new index may allocate/throw: leave the original
            // index intact until insertion has succeeded. Swapping a
            // pre-validated filesystem::path is noexcept, so no later
            // operation can strand the asset without its path index.
            m_Paths.emplace(newKey, id);
            m_Paths.erase(oldKey);
            found->second.Path.swap(normalized);
            IncrementRevision(found->second);
        }

        /// Task: replace dependencies atomically after checking existence,
        /// declared types, duplicates, self-reference, and graph cycles.
        /// TEACHING METHOD: Replace an asset's dependency list only after validating referential integrity and cycle constraints; then update revision.
        // Dependency updates validate typed targets and absence of cycles
        // before publishing a new dependency list; importer artifacts
        // should not bypass this registry graph for authored references.
        // SetDependencies: validate a copied candidate list, typed target existence
        // and absence of cycles before publishing it. This is an authored graph edge
        // update, not immediate re-import or GPU upload; increment revision only
        // after validation.
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
        /// TEACHING METHOD: Reject removal of an asset still depended on by others, preventing broken material/mesh/scene references.
        // Unlike a cascading delete, dependent references must be
        // explicitly changed or removed first. This protects scenes
        // and materials from dangling asset identifiers.
        // Remove: unlike a cascading deletion, refuse to erase an asset referenced by
        // any other registered metadata. This protects materials/scenes from silently
        // dangling stable IDs; callers must rebind or remove dependents explicitly.
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
        /// TEACHING METHOD: Return a detached deterministic metadata snapshot for manifest writing, tools and testing; does not expose internal hash maps.
        // Snapshot: return ID-sorted *copies* of unordered map contents under a
        // shared lock. Deterministic ordering matters to manifests, tests,
        // source-control diffs and reproducible hashing.
        [[nodiscard]] std::vector<AssetMetadata> Snapshot() const
        {
            std::shared_lock lock(m_Mutex);
            std::vector<AssetMetadata> result;
            result.reserve(m_Assets.size());
            for (const auto& [id, metadata] : m_Assets) result.push_back(metadata);
            std::ranges::sort(result, {}, &AssetMetadata::ID);
            return result;
        }

        /// TEACHING METHOD: Return asset count under shared lock; lightweight observable registry metric.
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

        // ValidateDependencies: inspect every typed edge and reject missing/mistyped
        // targets. Use reachability back to candidate.ID to detect a cycle when this
        // record is added or updated.
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

        // Reaches: depth-first dependency walk with a visited set. If a path from the
        // proposed target returns to candidate.ID, the new edge closes a cycle.
        // Visited tracking bounds the repeated traversal of shared subgraphs.
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

        // EnsureRevisionCanIncrement: reject uint64_t exhaustion instead of
        // wraparound. Revision is a monotonic invalidation signal to downstream
        // provenance/cache code, not an arithmetic quantity that may safely roll
        // over.
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
