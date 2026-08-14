module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Assets.PipelinePublish;

import Kairo.Assets.AssetID;
import Kairo.Assets.Fingerprint;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    enum class PipelinePublishKind { Asset, Cache, Render };

    struct PipelinePublishFile final
    {
        std::filesystem::path Path;
        std::string Role;
        AssetFingerprint Fingerprint;
        std::string MediaType;
    };

    struct PipelinePublishManifest final
    {
        PipelinePublishKind Kind = PipelinePublishKind::Asset;
        std::string Project;
        std::string Name;
        std::uint32_t Version = 0u;
        std::string SourceHost;
        std::filesystem::path SourcePath;
        AssetFingerprint SourceFingerprint;
        std::vector<PipelinePublishFile> Outputs;
        std::vector<PipelinePublishFile> Dependencies;
        std::map<std::string, std::string> Metadata;
    };

    /// Input: a `kairo.publish.v1` JSON file produced by a DCC adapter.
    /// Output: a bounded, strict C++ representation of its provenance and files.
    /// Task: reject unknown schema fields, malformed types, unsafe paths, and
    /// duplicate payload paths before engine-side import decisions are made.
    [[nodiscard]] PipelinePublishManifest LoadPipelinePublishManifest(
        const std::filesystem::path& manifestPath);

    /// Input: a parsed manifest and the path it was loaded from.
    /// Task: verify every output/dependency is a regular non-symlink file under
    /// the bundle directory and still has the declared SHA-256 fingerprint.
    void ValidatePipelinePublishBundle(const std::filesystem::path& manifestPath,
        const PipelinePublishManifest& manifest);

    /// Input: engine project root and a verified Blender asset publish manifest.
    /// Output: persistent ID for the registered glTF scene source.
    /// Task: close the artist-to-engine handoff by registering the immutable
    /// published glTF using Kairo's production scene importer. This function
    /// does not mutate the bundle or silently replace an existing registry path.
    [[nodiscard]] AssetID RegisterBlenderPublish(const std::filesystem::path& projectRoot,
        const std::filesystem::path& manifestPath, AssetRegistry& registry);
}

namespace kairo::assets::pipeline_publish_detail
{
    using Json = nlohmann::json;
    constexpr std::uintmax_t MaximumManifestBytes = 8u * 1024u * 1024u;
    constexpr std::size_t MaximumFiles = 100'000u;

    void RequireKeys(const Json& value, const std::set<std::string>& expected, std::string_view label)
    {
        if (!value.is_object()) throw std::invalid_argument(std::string(label) + " must be an object.");
        std::set<std::string> actual;
        for (const auto& [key, ignored] : value.items())
        {
            (void)ignored;
            actual.insert(key);
        }
        if (actual != expected) throw std::invalid_argument(std::string(label) + " has missing or unknown fields.");
    }

    [[nodiscard]] const std::string& Text(const Json& value, std::string_view key, std::size_t maximum = 4096u)
    {
        const Json& item = value.at(key);
        if (!item.is_string()) throw std::invalid_argument(std::string(key) + " must be a string.");
        const std::string& result = item.get_ref<const std::string&>();
        if (result.empty() || result.size() > maximum || result.find('\0') != std::string::npos)
            throw std::invalid_argument(std::string(key) + " has invalid text.");
        return result;
    }

    [[nodiscard]] AssetFingerprint ParseFingerprint(const Json& value)
    {
        RequireKeys(value, { "sha256", "size" }, "publish fingerprint");
        if (!value.at("size").is_number_unsigned())
            throw std::invalid_argument("publish fingerprint size must be unsigned.");
        return AssetFingerprint::Parse(Text(value, "sha256", 64u), value.at("size").get<std::uint64_t>());
    }

    [[nodiscard]] PipelinePublishFile ParseFile(const Json& value)
    {
        RequireKeys(value, { "fingerprint", "media_type", "path", "role" }, "publish file");
        return {
            NormalizeAssetPath(Text(value, "path")),
            Text(value, "role", 128u),
            ParseFingerprint(value.at("fingerprint")),
            Text(value, "media_type", 255u)
        };
    }

    [[nodiscard]] std::vector<PipelinePublishFile> ParseFiles(const Json& value, std::string_view label)
    {
        if (!value.is_array() || value.size() > MaximumFiles)
            throw std::invalid_argument(std::string(label) + " must be a bounded array.");
        std::vector<PipelinePublishFile> result;
        result.reserve(value.size());
        for (const Json& item : value) result.push_back(ParseFile(item));
        return result;
    }

    [[nodiscard]] PipelinePublishKind ParseKind(std::string_view kind)
    {
        if (kind == "asset") return PipelinePublishKind::Asset;
        if (kind == "cache") return PipelinePublishKind::Cache;
        if (kind == "render") return PipelinePublishKind::Render;
        throw std::invalid_argument("publish kind is unsupported.");
    }

    [[nodiscard]] bool DescendsFrom(const std::filesystem::path& path,
        const std::filesystem::path& root)
    {
        const auto relative = path.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    }
}

namespace kairo::assets
{
    PipelinePublishManifest LoadPipelinePublishManifest(const std::filesystem::path& manifestPath)
    {
        namespace detail = pipeline_publish_detail;
        if (!std::filesystem::is_regular_file(manifestPath) || std::filesystem::is_symlink(manifestPath))
            throw std::invalid_argument("Pipeline publish manifest must be a regular non-symlink file.");
        if (std::filesystem::file_size(manifestPath) > detail::MaximumManifestBytes)
            throw std::length_error("Pipeline publish manifest exceeds 8 MiB.");
        std::ifstream stream(manifestPath, std::ios::binary);
        if (!stream) throw std::runtime_error("Pipeline publish manifest could not be opened.");
        detail::Json value;
        try { stream >> value; }
        catch (const detail::Json::exception& error)
        {
            throw std::invalid_argument(std::string("Pipeline publish JSON is invalid: ") + error.what());
        }
        detail::RequireKeys(value, { "dependencies", "kind", "metadata", "name", "outputs", "project",
            "schema", "source_fingerprint", "source_host", "source_path", "version" }, "publish manifest");
        if (detail::Text(value, "schema", 64u) != "kairo.publish.v1")
            throw std::invalid_argument("Pipeline publish schema is unsupported.");
        if (!value.at("version").is_number_unsigned())
            throw std::invalid_argument("Pipeline publish version must be unsigned.");
        const std::uint64_t version = value.at("version").get<std::uint64_t>();
        if (version == 0u || version > 999'999u)
            throw std::invalid_argument("Pipeline publish version is out of range.");
        if (!value.at("metadata").is_object() || value.at("metadata").size() > 256u)
            throw std::invalid_argument("Pipeline publish metadata must be a bounded object.");
        PipelinePublishManifest result{
            detail::ParseKind(detail::Text(value, "kind", 16u)),
            detail::Text(value, "project", 128u), detail::Text(value, "name", 128u),
            static_cast<std::uint32_t>(version), detail::Text(value, "source_host", 128u),
            NormalizeAssetPath(detail::Text(value, "source_path")),
            detail::ParseFingerprint(value.at("source_fingerprint")),
            detail::ParseFiles(value.at("outputs"), "publish outputs"),
            detail::ParseFiles(value.at("dependencies"), "publish dependencies"), {}
        };
        if (result.Outputs.empty()) throw std::invalid_argument("Pipeline publish requires an output.");
        if (result.Outputs.size() + result.Dependencies.size() > detail::MaximumFiles)
            throw std::length_error("Pipeline publish exceeds the combined file limit.");
        std::set<std::string> paths;
        for (const PipelinePublishFile& file : result.Outputs)
            if (!paths.insert(PortableAssetPathKey(file.Path)).second)
                throw std::invalid_argument("Pipeline publish contains duplicate payload paths.");
        for (const PipelinePublishFile& file : result.Dependencies)
            if (!paths.insert(PortableAssetPathKey(file.Path)).second)
                throw std::invalid_argument("Pipeline publish contains duplicate payload paths.");
        for (const auto& [key, item] : value.at("metadata").items())
        {
            if (!item.is_string() || key.empty() || key.size() > 128u)
                throw std::invalid_argument("Pipeline publish metadata entry is invalid.");
            const std::string text = item.get<std::string>();
            if (text.size() > 4096u || text.find('\0') != std::string::npos)
                throw std::invalid_argument("Pipeline publish metadata value is invalid.");
            result.Metadata.emplace(key, text);
        }
        return result;
    }

    void ValidatePipelinePublishBundle(const std::filesystem::path& manifestPath,
        const PipelinePublishManifest& manifest)
    {
        namespace detail = pipeline_publish_detail;
        const std::filesystem::path bundle = std::filesystem::weakly_canonical(manifestPath).parent_path();
        const auto validate = [&bundle](const PipelinePublishFile& file)
        {
            const std::filesystem::path candidate = bundle / file.Path;
            if (std::filesystem::is_symlink(candidate) || !std::filesystem::is_regular_file(candidate))
                throw std::invalid_argument("Pipeline publish payload is missing or is a symlink: " + file.Path.generic_string());
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate);
            if (!detail::DescendsFrom(canonical, bundle))
                throw std::invalid_argument("Pipeline publish payload escapes its bundle.");
            if (FingerprintFile(canonical) != file.Fingerprint)
                throw std::invalid_argument("Pipeline publish payload fingerprint changed: " + file.Path.generic_string());
        };
        for (const PipelinePublishFile& file : manifest.Outputs) validate(file);
        for (const PipelinePublishFile& file : manifest.Dependencies) validate(file);
    }

    AssetID RegisterBlenderPublish(const std::filesystem::path& projectRoot,
        const std::filesystem::path& manifestPath, AssetRegistry& registry)
    {
        namespace detail = pipeline_publish_detail;
        const std::filesystem::path root = std::filesystem::canonical(projectRoot);
        const std::filesystem::path manifestFile = std::filesystem::weakly_canonical(manifestPath);
        if (!detail::DescendsFrom(manifestFile, root))
            throw std::invalid_argument("Pipeline publish manifest must be inside the engine project.");
        const PipelinePublishManifest manifest = LoadPipelinePublishManifest(manifestFile);
        ValidatePipelinePublishBundle(manifestFile, manifest);
        if (manifest.Kind != PipelinePublishKind::Asset || manifest.SourceHost != "blender")
            throw std::invalid_argument("Engine registration requires a Blender asset publish.");
        const PipelinePublishFile* scene = nullptr;
        for (const PipelinePublishFile& output : manifest.Outputs)
        {
            if (output.Role != "scene" || output.MediaType != "model/gltf+json") continue;
            if (scene != nullptr) throw std::invalid_argument("Blender publish contains multiple glTF scene outputs.");
            scene = &output;
        }
        if (scene == nullptr || scene->Path.extension() != ".gltf")
            throw std::invalid_argument("Blender publish has no supported glTF scene output.");
        const std::filesystem::path publishedScene = manifestFile.parent_path() / scene->Path;
        const std::filesystem::path projectPath = publishedScene.lexically_relative(root);
        return registry.Create({ AssetType::Scene, AssetOrigin::SourceFile,
            NormalizeAssetPath(projectPath), "kairo.gltf.scene", {} });
    }
}
