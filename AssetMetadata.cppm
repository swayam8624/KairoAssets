module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

export module Kairo.Assets.Metadata;

import Kairo.Assets.AssetID;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Type-safe persistent reference used by scene and material components.
    /// It carries no loaded object pointer and remains valid across process runs.
    template<AssetType ExpectedType>
    struct AssetHandle final
    {
        AssetID ID;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return ID.IsValid(); }
        friend constexpr bool operator==(const AssetHandle&, const AssetHandle&) noexcept = default;
    };

    using MeshAssetHandle = AssetHandle<AssetType::Mesh>;
    using MaterialAssetHandle = AssetHandle<AssetType::Material>;
    using TextureAssetHandle = AssetHandle<AssetType::Texture2D>;
    using SceneAssetHandle = AssetHandle<AssetType::Scene>;

    /// Untyped reference used by dependency graphs and generic asset tooling.
    struct AssetReference final
    {
        AssetID ID;
        AssetType Type = AssetType::Other;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return ID.IsValid(); }
        friend constexpr bool operator==(const AssetReference&, const AssetReference&) noexcept = default;
    };

    /// Input description for registering a newly discovered or authored asset.
    /// AssetRegistry assigns the persistent ID and initial revision.
    struct AssetRegistration final
    {
        AssetType Type = AssetType::Other;
        AssetOrigin Origin = AssetOrigin::SourceFile;
        std::filesystem::path Path;
        std::string Importer;
        std::vector<AssetReference> Dependencies;
    };

    /// Persisted registry record. Revision increases whenever path, importer,
    /// origin, or dependency metadata changes; it is not a source-file hash.
    struct AssetMetadata final
    {
        AssetID ID;
        AssetType Type = AssetType::Other;
        AssetOrigin Origin = AssetOrigin::SourceFile;
        std::filesystem::path Path;
        std::string Importer;
        std::uint64_t Revision = 1u;
        std::vector<AssetReference> Dependencies;
    };

    /// Input: a project-relative portable logical path.
    /// Output: lexically normalized path using no current/parent components.
    /// Task: make manifest paths stable across macOS, Linux, and Windows.
    /// Absolute paths, drive/UNC roots, backslashes, colons, control bytes,
    /// empty paths, and traversal outside the project are rejected.
    [[nodiscard]] inline std::filesystem::path NormalizeAssetPath(const std::filesystem::path& input)
    {
        if (input.empty() || input.is_absolute() || input.has_root_name() || input.has_root_directory())
            throw std::invalid_argument("Asset path must be non-empty and project-relative.");

        const std::string original = input.generic_string();
        for (const char character : original)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (byte == '\\' || byte == ':' || byte < 0x20u || byte == 0x7fu)
                throw std::invalid_argument("Asset path contains a non-portable character.");
        }

        const std::filesystem::path normalized = input.lexically_normal();
        if (normalized.empty() || normalized == ".")
            throw std::invalid_argument("Asset path must identify a project item.");
        for (const auto& component : normalized)
            if (component == "..") throw std::invalid_argument("Asset path cannot escape the project root.");
        return normalized;
    }

    /// Output: case-folded lookup key while preserving the authored path in
    /// metadata. This prevents ambiguous manifests on case-insensitive hosts.
    [[nodiscard]] inline std::string PortableAssetPathKey(const std::filesystem::path& path)
    {
        std::string key = NormalizeAssetPath(path).generic_string();
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return key;
    }

    /// Task: validate one metadata record independently of registry topology.
    inline void ValidateAssetMetadata(const AssetMetadata& metadata)
    {
        if (!metadata.ID.IsValid()) throw std::invalid_argument("Asset metadata requires a valid ID.");
        (void)NormalizeAssetPath(metadata.Path);
        if (metadata.Importer.empty() || metadata.Importer.size() > 128u)
            throw std::invalid_argument("Asset importer identifier must contain 1 to 128 characters.");
        for (const char character : metadata.Importer)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (!std::isalnum(byte) && byte != '.' && byte != '_' && byte != '-')
                throw std::invalid_argument("Asset importer identifier contains an unsupported character.");
        }
        if (metadata.Revision == 0u) throw std::invalid_argument("Asset metadata revision must be positive.");

        std::unordered_set<AssetID, AssetIDHash> unique;
        for (const AssetReference& dependency : metadata.Dependencies)
        {
            if (!dependency.IsValid()) throw std::invalid_argument("Asset dependency requires a valid ID.");
            if (dependency.ID == metadata.ID) throw std::invalid_argument("An asset cannot depend on itself.");
            if (!unique.insert(dependency.ID).second) throw std::invalid_argument("Asset dependencies cannot contain duplicates.");
        }
    }
}
