module;

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Assets.EditableMesh;

import Kairo.Assets.MeshArtifact;

export namespace kairo::assets
{
    struct EditableVertexID final
    {
        std::uint64_t Value = 0u;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0u; }
        friend constexpr auto operator<=>(EditableVertexID, EditableVertexID) noexcept = default;
    };

    struct EditableFaceID final
    {
        std::uint64_t Value = 0u;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0u; }
        friend constexpr auto operator<=>(EditableFaceID, EditableFaceID) noexcept = default;
    };

    struct EditableEdgeKey final
    {
        EditableVertexID A{};
        EditableVertexID B{};

        [[nodiscard]] static EditableEdgeKey Canonical(EditableVertexID a, EditableVertexID b)
        {
            if (!a.IsValid() || !b.IsValid() || a == b)
                throw std::invalid_argument("Editable edge requires two distinct valid vertices.");
            return a < b ? EditableEdgeKey{ a, b } : EditableEdgeKey{ b, a };
        }

        friend constexpr auto operator<=>(const EditableEdgeKey&, const EditableEdgeKey&) noexcept = default;
    };

    struct EditableMeshVertex final
    {
        EditableVertexID ID{};
        std::array<double, 3u> Position{};
    };

    struct EditableMeshFace final
    {
        EditableFaceID ID{};
        std::vector<EditableVertexID> Vertices;
        std::uint32_t MaterialSlot = 0u;
    };

    struct EditableHalfEdge final
    {
        EditableVertexID From{};
        EditableVertexID To{};
        EditableFaceID Face{};
        std::size_t Corner = 0u;
        std::optional<std::size_t> Twin;
    };

    struct EditableMeshValidation final
    {
        bool Valid = true;
        std::vector<std::string> Errors;
    };

    class EditableMesh final
    {
    public:
        static constexpr std::size_t MaximumVertices = 4'000'000u;
        static constexpr std::size_t MaximumFaces = 4'000'000u;

        [[nodiscard]] const std::map<EditableVertexID, EditableMeshVertex>& Vertices() const noexcept
        { return m_Vertices; }
        [[nodiscard]] const std::map<EditableFaceID, EditableMeshFace>& Faces() const noexcept
        { return m_Faces; }

        [[nodiscard]] EditableVertexID AddVertex(std::array<double, 3u> position)
        {
            if (m_Vertices.size() >= MaximumVertices)
                throw std::length_error("Editable mesh exceeds its vertex safety limit.");
            if (!Finite(position)) throw std::invalid_argument("Editable vertex position must be finite.");
            const EditableVertexID id{ m_NextVertex++ };
            if (!id.IsValid()) throw std::overflow_error("Editable vertex ID space is exhausted.");
            m_Vertices.emplace(id, EditableMeshVertex{ id, position });
            return id;
        }

        [[nodiscard]] EditableFaceID AddFace(std::vector<EditableVertexID> vertices,
            std::uint32_t materialSlot = 0u)
        {
            if (m_Faces.size() >= MaximumFaces)
                throw std::length_error("Editable mesh exceeds its face safety limit.");
            ValidateFaceVertices(vertices);
            const EditableFaceID id{ m_NextFace++ };
            if (!id.IsValid()) throw std::overflow_error("Editable face ID space is exhausted.");
            m_Faces.emplace(id, EditableMeshFace{ id, std::move(vertices), materialSlot });
            const auto report = Validate();
            if (!report.Valid)
            {
                m_Faces.erase(id);
                throw std::invalid_argument(report.Errors.front());
            }
            return id;
        }

        void RemoveFace(EditableFaceID face)
        {
            if (m_Faces.erase(face) == 0u) throw std::out_of_range("Editable face does not exist.");
        }

        [[nodiscard]] EditableMeshVertex& Vertex(EditableVertexID id)
        {
            const auto found = m_Vertices.find(id);
            if (found == m_Vertices.end()) throw std::out_of_range("Editable vertex does not exist.");
            return found->second;
        }
        [[nodiscard]] const EditableMeshVertex& Vertex(EditableVertexID id) const
        {
            const auto found = m_Vertices.find(id);
            if (found == m_Vertices.end()) throw std::out_of_range("Editable vertex does not exist.");
            return found->second;
        }
        [[nodiscard]] EditableMeshFace& Face(EditableFaceID id)
        {
            const auto found = m_Faces.find(id);
            if (found == m_Faces.end()) throw std::out_of_range("Editable face does not exist.");
            return found->second;
        }
        [[nodiscard]] const EditableMeshFace& Face(EditableFaceID id) const
        {
            const auto found = m_Faces.find(id);
            if (found == m_Faces.end()) throw std::out_of_range("Editable face does not exist.");
            return found->second;
        }

        void Translate(const std::vector<EditableVertexID>& selection,
            std::array<double, 3u> delta)
        {
            if (!Finite(delta)) throw std::invalid_argument("Editable translation must be finite.");
            std::set<EditableVertexID> unique(selection.begin(), selection.end());
            for (const auto id : unique)
            {
                auto& p = Vertex(id).Position;
                for (std::size_t axis = 0u; axis < 3u; ++axis) p[axis] += delta[axis];
            }
        }

        [[nodiscard]] EditableFaceID ExtrudeFace(EditableFaceID faceID,
            std::array<double, 3u> offset)
        {
            if (!Finite(offset)) throw std::invalid_argument("Extrusion offset must be finite.");
            const EditableMesh backup = *this;
            try
            {
                const EditableMeshFace source = Face(faceID);
                std::vector<EditableVertexID> top;
                top.reserve(source.Vertices.size());
                for (const auto id : source.Vertices)
                {
                    auto p = Vertex(id).Position;
                    for (std::size_t axis = 0u; axis < 3u; ++axis) p[axis] += offset[axis];
                    top.push_back(AddVertex(p));
                }
                RemoveFace(faceID);
                const EditableFaceID topFace = AddFace(top, source.MaterialSlot);
                for (std::size_t i = 0u; i < source.Vertices.size(); ++i)
                {
                    const std::size_t next = (i + 1u) % source.Vertices.size();
                    (void)AddFace({ source.Vertices[i], source.Vertices[next], top[next], top[i] },
                        source.MaterialSlot);
                }
                return topFace;
            }
            catch (...)
            {
                *this = backup;
                throw;
            }
        }

        [[nodiscard]] EditableFaceID InsetFace(EditableFaceID faceID, double amount)
        {
            if (!std::isfinite(amount) || amount <= 0.0 || amount >= 1.0)
                throw std::invalid_argument("Inset amount must be in the open interval (0, 1).");
            const EditableMesh backup = *this;
            try
            {
                const EditableMeshFace source = Face(faceID);
                std::array<double, 3u> center{};
                for (const auto id : source.Vertices)
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        center[axis] += Vertex(id).Position[axis];
                for (double& value : center) value /= static_cast<double>(source.Vertices.size());

                std::vector<EditableVertexID> inner;
                inner.reserve(source.Vertices.size());
                for (const auto id : source.Vertices)
                {
                    auto p = Vertex(id).Position;
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        p[axis] = p[axis] + (center[axis] - p[axis]) * amount;
                    inner.push_back(AddVertex(p));
                }
                RemoveFace(faceID);
                const EditableFaceID innerFace = AddFace(inner, source.MaterialSlot);
                for (std::size_t i = 0u; i < source.Vertices.size(); ++i)
                {
                    const std::size_t next = (i + 1u) % source.Vertices.size();
                    (void)AddFace({ source.Vertices[i], source.Vertices[next], inner[next], inner[i] },
                        source.MaterialSlot);
                }
                return innerFace;
            }
            catch (...)
            {
                *this = backup;
                throw;
            }
        }

        void MergeVertices(EditableVertexID keep, EditableVertexID remove)
        {
            if (keep == remove) return;
            (void)Vertex(keep); (void)Vertex(remove);
            const EditableMesh backup = *this;
            try
            {
                std::vector<EditableFaceID> erase;
                for (auto& [id, face] : m_Faces)
                {
                    for (auto& vertex : face.Vertices) if (vertex == remove) vertex = keep;
                    std::set<EditableVertexID> unique(face.Vertices.begin(), face.Vertices.end());
                    if (unique.size() < 3u) erase.push_back(id);
                }
                for (const auto id : erase) m_Faces.erase(id);
                m_Vertices.erase(remove);
                const auto report = Validate();
                if (!report.Valid) throw std::invalid_argument(report.Errors.front());
            }
            catch (...)
            {
                *this = backup;
                throw;
            }
        }

        [[nodiscard]] std::vector<EditableHalfEdge> HalfEdges() const
        {
            std::vector<EditableHalfEdge> result;
            std::map<std::pair<EditableVertexID, EditableVertexID>, std::size_t> oriented;
            for (const auto& [faceID, face] : m_Faces)
            {
                for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
                {
                    const auto from = face.Vertices[i];
                    const auto to = face.Vertices[(i + 1u) % face.Vertices.size()];
                    const std::size_t index = result.size();
                    result.push_back({ from, to, faceID, i, std::nullopt });
                    oriented.emplace(std::pair{ from, to }, index);
                }
            }
            for (std::size_t i = 0u; i < result.size(); ++i)
            {
                const auto found = oriented.find({ result[i].To, result[i].From });
                if (found != oriented.end()) result[i].Twin = found->second;
            }
            return result;
        }

        [[nodiscard]] EditableMeshValidation Validate() const
        {
            EditableMeshValidation report;
            std::map<EditableEdgeKey, std::size_t> edgeUse;
            for (const auto& [id, vertex] : m_Vertices)
            {
                if (!id.IsValid() || id != vertex.ID || !Finite(vertex.Position))
                    report.Errors.push_back("Editable mesh contains an invalid vertex record.");
            }
            for (const auto& [id, face] : m_Faces)
            {
                if (!id.IsValid() || id != face.ID || face.Vertices.size() < 3u)
                {
                    report.Errors.push_back("Editable mesh contains an invalid face record.");
                    continue;
                }
                std::set<EditableVertexID> unique;
                for (const auto vertex : face.Vertices)
                {
                    if (!m_Vertices.contains(vertex))
                        report.Errors.push_back("Editable face references a missing vertex.");
                    unique.insert(vertex);
                }
                if (unique.size() != face.Vertices.size())
                    report.Errors.push_back("Editable face contains repeated vertices.");
                for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
                {
                    const auto a = face.Vertices[i];
                    const auto b = face.Vertices[(i + 1u) % face.Vertices.size()];
                    if (a == b) continue;
                    const auto edge = EditableEdgeKey::Canonical(a, b);
                    if (++edgeUse[edge] > 2u)
                        report.Errors.push_back("Editable mesh contains a non-manifold edge.");
                }
            }
            report.Valid = report.Errors.empty();
            return report;
        }

    private:
        std::map<EditableVertexID, EditableMeshVertex> m_Vertices;
        std::map<EditableFaceID, EditableMeshFace> m_Faces;
        std::uint64_t m_NextVertex = 1u;
        std::uint64_t m_NextFace = 1u;

        [[nodiscard]] static bool Finite(const std::array<double, 3u>& value) noexcept
        { return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); }

        void ValidateFaceVertices(const std::vector<EditableVertexID>& vertices) const
        {
            if (vertices.size() < 3u) throw std::invalid_argument("Editable face needs at least three vertices.");
            std::set<EditableVertexID> unique;
            for (const auto vertex : vertices)
            {
                if (!m_Vertices.contains(vertex)) throw std::out_of_range("Editable face references a missing vertex.");
                unique.insert(vertex);
            }
            if (unique.size() != vertices.size()) throw std::invalid_argument("Editable face cannot repeat a vertex.");
        }
    };

    class EditableMeshTransaction final
    {
    public:
        explicit EditableMeshTransaction(EditableMesh& mesh) : m_Target(mesh), m_Backup(mesh) {}
        EditableMeshTransaction(const EditableMeshTransaction&) = delete;
        EditableMeshTransaction& operator=(const EditableMeshTransaction&) = delete;
        ~EditableMeshTransaction() { if (!m_Committed) m_Target = std::move(m_Backup); }
        void Commit()
        {
            const auto report = m_Target.Validate();
            if (!report.Valid) throw std::invalid_argument(report.Errors.front());
            m_Committed = true;
        }
        void Rollback() noexcept { if (!m_Committed) { m_Target = std::move(m_Backup); m_Committed = true; } }
    private:
        EditableMesh& m_Target;
        EditableMesh m_Backup;
        bool m_Committed = false;
    };

    [[nodiscard]] inline MeshArtifactData CookEditableMesh(const EditableMesh& source)
    {
        const auto report = source.Validate();
        if (!report.Valid) throw std::invalid_argument(report.Errors.front());
        if (source.Faces().empty()) throw std::invalid_argument("Editable mesh has no faces to cook.");

        MeshArtifactData result;
        std::map<EditableVertexID, std::uint32_t> indices;
        result.Vertices.reserve(source.Vertices().size());
        for (const auto& [id, vertex] : source.Vertices())
        {
            if (result.Vertices.size() >= std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Editable mesh exceeds MeshArtifact indexing.");
            indices.emplace(id, static_cast<std::uint32_t>(result.Vertices.size()));
            result.Vertices.push_back({
                { static_cast<float>(vertex.Position[0]), static_cast<float>(vertex.Position[1]), static_cast<float>(vertex.Position[2]) },
                {}, {} });
        }
        for (const auto& [id, face] : source.Faces())
        {
            (void)id;
            const std::uint32_t first = indices.at(face.Vertices.front());
            for (std::size_t i = 1u; i + 1u < face.Vertices.size(); ++i)
            {
                result.Indices.push_back(first);
                result.Indices.push_back(indices.at(face.Vertices[i]));
                result.Indices.push_back(indices.at(face.Vertices[i + 1u]));
            }
        }
        result.HasNormals = false;
        result.HasTexCoords = false;
        ValidateMeshArtifactData(result);
        return result;
    }
}
