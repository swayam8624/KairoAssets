module;

#include <algorithm>
#include <cstddef>
#include <string_view>
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
import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.Fingerprint;
import Kairo.Assets.ImportDatabase;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Input to an importer after the service has validated project ownership.
    struct ImportRequest final
    {
        ImportRecord Record;
        AssetType ExpectedType = AssetType::Other;
        std::span<const std::byte> SourceBytes;
        std::filesystem::path SourcePath;
    };

    /// Importer plugin contract. Implementations transform validated source
    /// bytes into a typed, versioned artifact. SourcePath is supplied for
    /// compound formats such as glTF that may resolve sibling dependencies;
    /// importers still do not mutate registries, databases, or caches.
    class AssetImporter
    {
    public:
        virtual ~AssetImporter() = default;
        [[nodiscard]] virtual std::string Identifier() const = 0;
        [[nodiscard]] virtual std::string Version() const = 0;
        // Relative to the primary source directory. Discover even on a
        // cache hit, since a sibling file can change without changing JSON.
        [[nodiscard]] virtual std::vector<std::filesystem::path> DiscoverDependencies(const ImportRequest&) const
        {return {};}
        [[nodiscard]] virtual DerivedArtifact Import(const ImportRequest& request) const = 0;
    };

    /// Actual baseline importer for opaque documents, scripts, and source
    /// artifacts whose derived representation is their original byte stream.
    class PassthroughImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.passthrough"; }
        [[nodiscard]] std::string Version() const override { return "1"; }
        [[nodiscard]] DerivedArtifact Import(const ImportRequest& request) const override
        {
            return { request.ExpectedType, 1u, "kairo.raw.v1",
                { request.SourceBytes.begin(), request.SourceBytes.end() } };
        }
    };

    struct ImportOutcome final
    {
        DerivedDataKey Key;
        DerivedArtifact Artifact;
        bool CacheHit = false;
    };

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
        ValidateImportRecord(record, registry);
        const AssetMetadata metadata = registry.At(record.Asset);
        record.Source = NormalizeAssetPath(record.Source);
        const std::filesystem::path source = projectRoot / record.Source;
        std::ifstream input(source, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Unable to open import source: " + source.string());
        const auto end = input.tellg();
        if (end < 0 || static_cast<std::uintmax_t>(end) > 1024u * 1024u * 1024u)
            throw std::length_error("Import source exceeds the 1 GiB safety limit.");
        std::vector<std::byte> bytes(static_cast<std::size_t>(end)); input.seekg(0);
        if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Unable to read import source: " + source.string());
        record.SourceFingerprint = FingerprintBytes(bytes);
        const auto discover=[&]()
        {
            auto paths=importer.DiscoverDependencies({record,metadata.Type,bytes,source});
            if(paths.size()>8192)throw std::length_error("Import dependency limit exceeded.");
            std::vector<ImportDependencyFingerprint> dependencies;
            dependencies.reserve(paths.size());
            for(auto& path:paths)
            {
                const auto relative=NormalizeAssetPath(path);
                const auto projectPath=NormalizeAssetPath(record.Source.parent_path()/relative);
                const auto absolute=projectRoot/projectPath;
                // Reject symlinks in every component within the project.
                auto cursor=projectRoot;
                for(const auto& part:projectPath)
                {
                    cursor/=part;
                    if(std::filesystem::is_symlink(cursor))
                        throw std::invalid_argument("Linked import dependency rejected.");
                }
                if(!std::filesystem::is_regular_file(absolute))
                    throw std::invalid_argument("Import dependency is not a regular file.");
                if(std::filesystem::file_size(absolute)>1024ull*1024ull*1024ull)
                    throw std::length_error("Import dependency exceeds 1 GiB.");
                dependencies.push_back({projectPath,FingerprintFile(absolute)});
            }
            std::ranges::sort(dependencies,{},[](const auto& dependency){return dependency.Path.generic_string();});
            if(std::adjacent_find(dependencies.begin(),dependencies.end(),[](const auto& a,const auto& b)
                {return PortableAssetPathKey(a.Path)==PortableAssetPathKey(b.Path);})!=dependencies.end())
                throw std::invalid_argument("Duplicate import dependency paths.");
            return dependencies;
        };
        record.Dependencies=discover();
        // Keep old keys for standalone files; length-prefix the relative
        // dependency paths and bind their content digests for compound files.
        auto recipeFingerprint=record.SourceFingerprint;
        if(!record.Dependencies.empty())
        {
            std::string recipe="kairo.compound.v1\n"+record.SourceFingerprint.ToHex()+":"+
                std::to_string(record.SourceFingerprint.ByteCount)+"\n";
            for(const auto& dependency:record.Dependencies)
            {
                const auto name=dependency.Path.lexically_relative(record.Source.parent_path().empty()?
                    std::filesystem::path("."):record.Source.parent_path()).generic_string();
                recipe+=std::to_string(name.size())+":"+name+":"+dependency.Fingerprint.ToHex()+":"+
                    std::to_string(dependency.Fingerprint.ByteCount)+"\n";
            }
            recipeFingerprint=FingerprintBytes(std::as_bytes(std::span(recipe.data(),recipe.size())));
        }
        const DerivedDataKey key = MakeDerivedDataKey(recipeFingerprint, metadata.Type,
            record.Importer, record.ImporterVersion, record.CanonicalSettings);
        if (const auto prior = imports.Find(record.Asset))
        {
            const bool changed = prior->Source != record.Source || prior->Importer != record.Importer ||
                prior->ImporterVersion != record.ImporterVersion ||
                prior->CanonicalSettings != record.CanonicalSettings ||
                prior->SourceFingerprint != record.SourceFingerprint || prior->Dependencies!=record.Dependencies;
            if (changed)
            {
                if (prior->Revision == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("Import provenance revision is exhausted.");
                record.Revision = prior->Revision + 1u;
            }
            else
            {
                record.Revision = prior->Revision;
            }
        }
        const bool hit = cache.Contains(key);
        DerivedArtifact artifact;
        if (hit)
        {
            artifact = ParseDerivedArtifact(cache.Load(key));
        }
        else
        {
            artifact = importer.Import({ record, metadata.Type, bytes, source });
            ValidateDerivedArtifact(artifact);
            if (artifact.Type != metadata.Type)
                throw std::invalid_argument("Importer artifact type does not match asset registry metadata.");
            if(!record.Dependencies.empty()&&discover()!=record.Dependencies)
                throw std::runtime_error("Import dependencies changed during decoding.");
            cache.Store(key, SerializeDerivedArtifact(artifact));
        }
        if (artifact.Type != metadata.Type)
            throw std::invalid_argument("Cached artifact type does not match asset registry metadata.");
        imports.Upsert(std::move(record), registry);
        return { key, std::move(artifact), hit };
    }
}
