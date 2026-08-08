module;

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.UVAuthoring;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.MeshArtifact;

export namespace kairo::assets
{
    struct UVCorner final
    {
        EditableFaceID Face{};
        std::size_t Corner = 0u;
        friend constexpr auto operator<=>(const UVCorner&, const UVCorner&) noexcept = default;
    };

    enum class UVProjectionAxis : std::uint8_t { X, Y, Z };

    class UVLayout final
    {
    public:
        void MarkSeam(EditableEdgeKey edge) { m_Seams.insert(edge); }
        void ClearSeam(EditableEdgeKey edge) { m_Seams.erase(edge); }
        [[nodiscard]] bool IsSeam(EditableEdgeKey edge) const { return m_Seams.contains(edge); }
        [[nodiscard]] const std::set<EditableEdgeKey>& Seams() const noexcept { return m_Seams; }

        void Set(UVCorner corner, std::array<double, 2u> uv)
        {
            if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]))
                throw std::invalid_argument("UV coordinates must be finite.");
            m_UVs.insert_or_assign(corner, uv);
        }

        [[nodiscard]] const std::array<double, 2u>& At(UVCorner corner) const
        {
            const auto found = m_UVs.find(corner);
            if (found == m_UVs.end()) throw std::out_of_range("UV corner has not been authored.");
            return found->second;
        }

        [[nodiscard]] bool Contains(UVCorner corner) const noexcept { return m_UVs.contains(corner); }
        [[nodiscard]] const std::map<UVCorner, std::array<double, 2u>>& Coordinates() const noexcept
        { return m_UVs; }

        void Clear() noexcept { m_UVs.clear(); m_Seams.clear(); }

    private:
        std::map<UVCorner, std::array<double, 2u>> m_UVs;
        std::set<EditableEdgeKey> m_Seams;
    };

    inline void PlanarUnwrap(const EditableMesh& mesh, UVLayout& layout,
        UVProjectionAxis axis = UVProjectionAxis::Z)
    {
        const auto validation = mesh.Validate();
        if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());
        std::size_t a = 0u, b = 1u;
        if (axis == UVProjectionAxis::X) { a = 1u; b = 2u; }
        else if (axis == UVProjectionAxis::Y) { a = 0u; b = 2u; }
        for (const auto& [faceID, face] : mesh.Faces())
            for (std::size_t corner = 0u; corner < face.Vertices.size(); ++corner)
            {
                const auto& p = mesh.Vertex(face.Vertices[corner]).Position;
                layout.Set({ faceID, corner }, { p[a], p[b] });
            }
    }

    inline void NormalizeAndPackUVs(UVLayout& layout, double padding = 0.01)
    {
        if (!std::isfinite(padding) || padding < 0.0 || padding >= 0.5)
            throw std::invalid_argument("UV padding must be finite and in [0, 0.5).");
        if (layout.Coordinates().empty()) return;

        std::array<double, 2u> minimum{ 1.0e300, 1.0e300 };
        std::array<double, 2u> maximum{ -1.0e300, -1.0e300 };
        for (const auto& [corner, uv] : layout.Coordinates())
        {
            (void)corner;
            for (std::size_t axis = 0u; axis < 2u; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], uv[axis]);
                maximum[axis] = std::max(maximum[axis], uv[axis]);
            }
        }
        const double width = std::max(maximum[0] - minimum[0], 1.0e-12);
        const double height = std::max(maximum[1] - minimum[1], 1.0e-12);
        const double scale = (1.0 - 2.0 * padding) / std::max(width, height);
        std::vector<std::pair<UVCorner, std::array<double, 2u>>> rewritten;
        rewritten.reserve(layout.Coordinates().size());
        for (const auto& [corner, uv] : layout.Coordinates())
            rewritten.push_back({ corner,
                { padding + (uv[0] - minimum[0]) * scale,
                  padding + (uv[1] - minimum[1]) * scale } });
        for (const auto& [corner, uv] : rewritten) layout.Set(corner, uv);
    }

    [[nodiscard]] inline double EstimateUVTexelDensity(const EditableMesh& mesh,
        const UVLayout& layout, EditableFaceID faceID, std::uint32_t textureResolution)
    {
        if (textureResolution == 0u) throw std::invalid_argument("Texture resolution cannot be zero.");
        const auto& face = mesh.Face(faceID);
        if (face.Vertices.size() < 3u) throw std::invalid_argument("Face is not polygonal.");

        double worldArea = 0.0;
        double uvArea = 0.0;
        const auto& p0 = mesh.Vertex(face.Vertices[0]).Position;
        const auto& uv0 = layout.At({ faceID, 0u });
        for (std::size_t i = 1u; i + 1u < face.Vertices.size(); ++i)
        {
            const auto& p1 = mesh.Vertex(face.Vertices[i]).Position;
            const auto& p2 = mesh.Vertex(face.Vertices[i + 1u]).Position;
            const std::array<double, 3u> a{ p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
            const std::array<double, 3u> b{ p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
            const std::array<double, 3u> cross{
                a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] };
            worldArea += 0.5 * std::sqrt(cross[0]*cross[0] + cross[1]*cross[1] + cross[2]*cross[2]);
            const auto& uv1 = layout.At({ faceID, i });
            const auto& uv2 = layout.At({ faceID, i + 1u });
            uvArea += 0.5 * std::abs((uv1[0]-uv0[0])*(uv2[1]-uv0[1]) -
                                    (uv1[1]-uv0[1])*(uv2[0]-uv0[0]));
        }
        if (worldArea <= 1.0e-18) throw std::invalid_argument("Cannot estimate UV density for zero-area face.");
        return std::sqrt(uvArea / worldArea) * static_cast<double>(textureResolution);
    }

    [[nodiscard]] inline MeshArtifactData CookEditableMeshWithUV(
        const EditableMesh& mesh, const UVLayout& layout)
    {
        const auto validation = mesh.Validate();
        if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());
        MeshArtifactData result;
        result.HasNormals = false;
        result.HasTexCoords = true;

        for (const auto& [faceID, face] : mesh.Faces())
        {
            if (face.Vertices.size() < 3u) continue;
            std::vector<std::uint32_t> corners;
            corners.reserve(face.Vertices.size());
            for (std::size_t corner = 0u; corner < face.Vertices.size(); ++corner)
            {
                const auto& position = mesh.Vertex(face.Vertices[corner]).Position;
                const auto& uv = layout.At({ faceID, corner });
                const auto index = static_cast<std::uint32_t>(result.Vertices.size());
                result.Vertices.push_back({
                    { static_cast<float>(position[0]), static_cast<float>(position[1]), static_cast<float>(position[2]) },
                    {}, { static_cast<float>(uv[0]), static_cast<float>(uv[1]) } });
                corners.push_back(index);
            }
            for (std::size_t i = 1u; i + 1u < corners.size(); ++i)
            {
                result.Indices.push_back(corners[0]);
                result.Indices.push_back(corners[i]);
                result.Indices.push_back(corners[i + 1u]);
            }
        }
        ValidateMeshArtifactData(result);
        return result;
    }
}
