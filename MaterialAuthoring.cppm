module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Assets.MaterialAuthoring;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.UVAuthoring;
import Kairo.Assets.MaterialArtifact;

export namespace kairo::assets
{
    enum class TextureColorSpace : std::uint8_t
    {
        Auto,
        Linear,
        SRGB
    };

    enum class TextureSemantic : std::uint8_t
    {
        Generic,
        BaseColor,
        Normal,
        MetallicRoughness,
        Emissive,
        Occlusion
    };

    enum class TextureMipPolicy : std::uint8_t
    {
        Generate,
        Preserve,
        Disabled
    };

    struct TextureAuthoringSettings final
    {
        TextureColorSpace ColorSpace = TextureColorSpace::Auto;
        TextureSemantic Semantic = TextureSemantic::Generic;
        TextureMipPolicy Mips = TextureMipPolicy::Generate;
        std::uint32_t MaximumResolution = 8192u;
        bool FlipVertical = false;

        void Validate() const
        {
            if (MaximumResolution == 0u || MaximumResolution > 32768u)
                throw std::invalid_argument("Texture authoring maximum resolution must be in [1, 32768].");
            if (Semantic == TextureSemantic::Normal && ColorSpace == TextureColorSpace::SRGB)
                throw std::invalid_argument("Normal maps must not be imported as sRGB data.");
            if ((Semantic == TextureSemantic::MetallicRoughness ||
                 Semantic == TextureSemantic::Occlusion) &&
                ColorSpace == TextureColorSpace::SRGB)
                throw std::invalid_argument("Data textures must not be imported as sRGB data.");
        }

        [[nodiscard]] TextureColorSpace ResolvedColorSpace() const noexcept
        {
            if (ColorSpace != TextureColorSpace::Auto) return ColorSpace;
            switch (Semantic)
            {
                case TextureSemantic::BaseColor:
                case TextureSemantic::Emissive: return TextureColorSpace::SRGB;
                default: return TextureColorSpace::Linear;
            }
        }
    };

    struct MaterialChannelInspection final
    {
        std::string Name;
        bool HasTexture = false;
        bool UsesSRGB = false;
    };

    [[nodiscard]] inline std::vector<MaterialChannelInspection> InspectMaterialChannels(
        const MaterialArtifactData& material)
    {
        ValidateMaterialArtifactData(material);
        return {
            { "BaseColor", material.Textures.BaseColor.has_value(), true },
            { "Normal", material.Textures.Normal.has_value(), false },
            { "MetallicRoughness", material.Textures.MetallicRoughness.has_value(), false },
            { "Emissive", material.Textures.Emissive.has_value(), true },
            { "Occlusion", material.Textures.Occlusion.has_value(), false }
        };
    }

    inline void AssignMaterialSlot(EditableMesh& mesh,
        const std::vector<EditableFaceID>& faces,
        std::uint32_t materialSlot)
    {
        const std::set<EditableFaceID> unique(faces.begin(), faces.end());
        EditableMeshTransaction transaction(mesh);
        for (const EditableFaceID face : unique)
            mesh.Face(face).MaterialSlot = materialSlot;
        transaction.Commit();
    }

    struct UVIsland final
    {
        std::vector<EditableFaceID> Faces;
    };

    [[nodiscard]] inline std::vector<UVIsland> BuildUVIslands(
        const EditableMesh& mesh,
        const UVLayout& layout)
    {
        const auto validation = mesh.Validate();
        if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());

        std::map<EditableEdgeKey, std::vector<EditableFaceID>> edgeFaces;
        for (const auto& [faceID, face] : mesh.Faces())
            for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
                edgeFaces[EditableEdgeKey::Canonical(
                    face.Vertices[i], face.Vertices[(i + 1u) % face.Vertices.size()])].push_back(faceID);

        std::map<EditableFaceID, std::set<EditableFaceID>> adjacency;
        for (const auto& [edge, faces] : edgeFaces)
        {
            if (layout.IsSeam(edge) || faces.size() != 2u) continue;
            adjacency[faces[0]].insert(faces[1]);
            adjacency[faces[1]].insert(faces[0]);
        }

        std::set<EditableFaceID> remaining;
        for (const auto& [faceID, face] : mesh.Faces())
        {
            (void)face;
            remaining.insert(faceID);
        }

        std::vector<UVIsland> result;
        while (!remaining.empty())
        {
            const EditableFaceID seed = *remaining.begin();
            remaining.erase(seed);
            UVIsland island;
            std::vector<EditableFaceID> pending{ seed };
            while (!pending.empty())
            {
                const EditableFaceID current = pending.back();
                pending.pop_back();
                island.Faces.push_back(current);
                const auto found = adjacency.find(current);
                if (found == adjacency.end()) continue;
                for (const EditableFaceID neighbour : found->second)
                    if (remaining.erase(neighbour) != 0u) pending.push_back(neighbour);
            }
            std::sort(island.Faces.begin(), island.Faces.end());
            result.push_back(std::move(island));
        }
        return result;
    }

    /// Deterministically packs seam-derived islands into a square grid. Each
    /// island keeps its authored shape/aspect while receiving an isolated cell.
    inline void PackUVIslands(const EditableMesh& mesh,
        UVLayout& layout,
        double padding = 0.01)
    {
        if (!std::isfinite(padding) || padding < 0.0 || padding >= 0.25)
            throw std::invalid_argument("UV island padding must be in [0, 0.25).");
        const auto islands = BuildUVIslands(mesh, layout);
        if (islands.empty()) return;
        for (const auto& [faceID, face] : mesh.Faces())
            for (std::size_t corner = 0u; corner < face.Vertices.size(); ++corner)
                if (!layout.Contains({ faceID, corner }))
                    throw std::invalid_argument("Every face corner must have UVs before island packing.");

        std::size_t columns = 1u;
        while (columns * columns < islands.size()) ++columns;
        const double cell = 1.0 / static_cast<double>(columns);
        const double inset = padding * cell;

        for (std::size_t islandIndex = 0u; islandIndex < islands.size(); ++islandIndex)
        {
            const std::size_t column = islandIndex % columns;
            const std::size_t row = islandIndex / columns;
            std::array<double, 2u> minimum{ 1.0e300, 1.0e300 };
            std::array<double, 2u> maximum{ -1.0e300, -1.0e300 };
            std::vector<UVCorner> corners;
            for (const EditableFaceID faceID : islands[islandIndex].Faces)
            {
                const auto& face = mesh.Face(faceID);
                for (std::size_t corner = 0u; corner < face.Vertices.size(); ++corner)
                {
                    const UVCorner key{ faceID, corner };
                    const auto& uv = layout.At(key);
                    minimum[0] = std::min(minimum[0], uv[0]);
                    minimum[1] = std::min(minimum[1], uv[1]);
                    maximum[0] = std::max(maximum[0], uv[0]);
                    maximum[1] = std::max(maximum[1], uv[1]);
                    corners.push_back(key);
                }
            }
            const double width = std::max(maximum[0] - minimum[0], 1.0e-12);
            const double height = std::max(maximum[1] - minimum[1], 1.0e-12);
            const double usable = cell - 2.0 * inset;
            const double scale = usable / std::max(width, height);
            const double originX = static_cast<double>(column) * cell + inset;
            const double originY = static_cast<double>(row) * cell + inset;
            for (const UVCorner corner : corners)
            {
                const auto uv = layout.At(corner);
                layout.Set(corner, {
                    originX + (uv[0] - minimum[0]) * scale,
                    originY + (uv[1] - minimum[1]) * scale });
            }
        }
    }

    struct TextureAuthoringBinding final
    {
        TextureAssetHandle Texture;
        TextureAuthoringSettings Settings;
    };

    class MaterialAuthoringState final
    {
    public:
        static constexpr std::size_t MaximumMaterials = 256u;
        static constexpr std::size_t MaximumTextureBindings = 1024u;

        [[nodiscard]] std::size_t AddMaterial(MaterialArtifactData material)
        {
            ValidateMaterialArtifactData(material);
            if (m_Materials.size() >= MaximumMaterials)
                throw std::length_error("Material authoring state exceeds 256 material slots.");
            m_Materials.push_back(std::move(material));
            return m_Materials.size() - 1u;
        }

        void BindTexture(TextureAssetHandle texture, TextureAuthoringSettings settings)
        {
            if (!texture.IsValid()) throw std::invalid_argument("Texture authoring binding requires a valid asset handle.");
            settings.Validate();
            for (auto& binding : m_TextureBindings)
                if (binding.Texture.ID == texture.ID)
                {
                    binding.Settings = settings;
                    return;
                }
            if (m_TextureBindings.size() >= MaximumTextureBindings)
                throw std::length_error("Texture authoring state exceeds 1024 reimport settings.");
            m_TextureBindings.push_back({ texture, settings });
        }

        [[nodiscard]] const std::vector<MaterialArtifactData>& Materials() const noexcept
        { return m_Materials; }
        [[nodiscard]] const std::vector<TextureAuthoringBinding>& TextureBindings() const noexcept
        { return m_TextureBindings; }

    private:
        std::vector<MaterialArtifactData> m_Materials;
        std::vector<TextureAuthoringBinding> m_TextureBindings;
    };
}
