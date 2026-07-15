module;

#include <optional>
#include <string_view>

export module Kairo.Assets.Types;

export namespace kairo::assets
{
    /// Stable asset categories understood by manifests and editor filters.
    ///
    /// Task: classify authored data without coupling the asset registry to a
    /// concrete importer, GPU backend, audio system, or scripting runtime.
    /// Serialization uses NameOfAssetType() rather than enum ordinals, allowing
    /// the declaration order to change without corrupting persisted projects.
    enum class AssetType
    {
        Mesh,
        Material,
        Texture2D,
        Scene,
        Shader,
        Audio,
        Script,
        Other
    };

    /// Describes where an asset's authoritative content originates.
    enum class AssetOrigin
    {
        SourceFile,
        Generated,
        Builtin
    };

    [[nodiscard]] constexpr std::string_view NameOfAssetType(AssetType type) noexcept
    {
        switch (type)
        {
            case AssetType::Mesh: return "mesh";
            case AssetType::Material: return "material";
            case AssetType::Texture2D: return "texture2d";
            case AssetType::Scene: return "scene";
            case AssetType::Shader: return "shader";
            case AssetType::Audio: return "audio";
            case AssetType::Script: return "script";
            case AssetType::Other: return "other";
        }
        return "other";
    }

    [[nodiscard]] constexpr std::optional<AssetType> ParseAssetType(std::string_view name) noexcept
    {
        if (name == "mesh") return AssetType::Mesh;
        if (name == "material") return AssetType::Material;
        if (name == "texture2d") return AssetType::Texture2D;
        if (name == "scene") return AssetType::Scene;
        if (name == "shader") return AssetType::Shader;
        if (name == "audio") return AssetType::Audio;
        if (name == "script") return AssetType::Script;
        if (name == "other") return AssetType::Other;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::string_view NameOfAssetOrigin(AssetOrigin origin) noexcept
    {
        switch (origin)
        {
            case AssetOrigin::SourceFile: return "source";
            case AssetOrigin::Generated: return "generated";
            case AssetOrigin::Builtin: return "builtin";
        }
        return "source";
    }

    [[nodiscard]] constexpr std::optional<AssetOrigin> ParseAssetOrigin(std::string_view name) noexcept
    {
        if (name == "source") return AssetOrigin::SourceFile;
        if (name == "generated") return AssetOrigin::Generated;
        if (name == "builtin") return AssetOrigin::Builtin;
        return std::nullopt;
    }
}
