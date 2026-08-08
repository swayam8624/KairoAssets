module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.Assets.ModelingOperations;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.Sculpting;

export namespace kairo::assets
{
    namespace modeling_detail
    {
        [[nodiscard]] inline bool FaceUsesEdge(
            const EditableMeshFace& face, EditableEdgeKey edge)
        {
            edge = EditableEdgeKey::Canonical(edge.A, edge.B);
            for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
            {
                const auto a = face.Vertices[i];
                const auto b = face.Vertices[(i + 1u) % face.Vertices.size()];
                if (EditableEdgeKey::Canonical(a, b) == edge) return true;
            }
            return false;
        }

        [[nodiscard]] inline std::vector<EditableVertexID> BoundaryPathWithoutEdge(
            const EditableMeshFace& face,
            EditableVertexID start,
            EditableVertexID end,
            EditableEdgeKey shared)
        {
            shared = EditableEdgeKey::Canonical(shared.A, shared.B);
            const auto startIt = std::find(face.Vertices.begin(), face.Vertices.end(), start);
            if (startIt == face.Vertices.end()) return {};
            const std::size_t startIndex = static_cast<std::size_t>(startIt - face.Vertices.begin());

            for (int direction : { 1, -1 })
            {
                std::vector<EditableVertexID> path{ start };
                std::size_t index = startIndex;
                bool crossedShared = false;
                for (std::size_t guard = 0u; guard < face.Vertices.size(); ++guard)
                {
                    const std::size_t next = direction > 0
                        ? (index + 1u) % face.Vertices.size()
                        : (index + face.Vertices.size() - 1u) % face.Vertices.size();
                    if (EditableEdgeKey::Canonical(face.Vertices[index], face.Vertices[next]) == shared)
                        crossedShared = true;
                    index = next;
                    path.push_back(face.Vertices[index]);
                    if (face.Vertices[index] == end)
                    {
                        if (!crossedShared) return path;
                        break;
                    }
                }
            }
            return {};
        }
    }

    [[nodiscard]] inline EditableVertexID SplitEdge(
        EditableMesh& mesh, EditableEdgeKey edge, double t = 0.5)
    {
        edge = EditableEdgeKey::Canonical(edge.A, edge.B);
        if (!std::isfinite(t) || t <= 0.0 || t >= 1.0)
            throw std::invalid_argument("Edge split parameter must be in (0, 1).");

        std::vector<EditableFaceID> affected;
        for (const auto& [faceID, face] : mesh.Faces())
            if (modeling_detail::FaceUsesEdge(face, edge)) affected.push_back(faceID);
        if (affected.empty()) throw std::out_of_range("Edge split target does not exist.");

        const auto a = mesh.Vertex(edge.A).Position;
        const auto b = mesh.Vertex(edge.B).Position;
        EditableMeshTransaction transaction(mesh);
        const EditableVertexID created = mesh.AddVertex({
            a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t });

        for (const EditableFaceID faceID : affected)
        {
            auto& vertices = mesh.Face(faceID).Vertices;
            bool inserted = false;
            for (std::size_t i = 0u; i < vertices.size(); ++i)
            {
                const auto x = vertices[i];
                const auto y = vertices[(i + 1u) % vertices.size()];
                if (EditableEdgeKey::Canonical(x, y) != edge) continue;
                vertices.insert(vertices.begin() + static_cast<std::ptrdiff_t>(i + 1u), created);
                inserted = true;
                break;
            }
            if (!inserted) throw std::logic_error("Edge disappeared during split transaction.");
        }
        transaction.Commit();
        return created;
    }

    [[nodiscard]] inline std::vector<EditableFaceID> TriangulateFace(
        EditableMesh& mesh, EditableFaceID faceID)
    {
        const EditableMeshFace source = mesh.Face(faceID);
        if (source.Vertices.size() == 3u) return { faceID };
        EditableMeshTransaction transaction(mesh);
        mesh.RemoveFace(faceID);
        std::vector<EditableFaceID> result;
        result.reserve(source.Vertices.size() - 2u);
        for (std::size_t i = 1u; i + 1u < source.Vertices.size(); ++i)
            result.push_back(mesh.AddFace(
                { source.Vertices[0], source.Vertices[i], source.Vertices[i + 1u] },
                source.MaterialSlot));
        transaction.Commit();
        return result;
    }

    [[nodiscard]] inline std::vector<EditableFaceID> DuplicateFaces(
        EditableMesh& mesh,
        const std::vector<EditableFaceID>& selection,
        std::array<double, 3u> offset = {})
    {
        for (double value : offset)
            if (!std::isfinite(value)) throw std::invalid_argument("Duplicate offset must be finite.");
        const std::set<EditableFaceID> unique(selection.begin(), selection.end());
        if (unique.empty()) return {};
        const EditableMesh snapshot = mesh;
        std::set<EditableVertexID> sourceVertices;
        for (const EditableFaceID faceID : unique)
            for (const EditableVertexID vertex : snapshot.Face(faceID).Vertices)
                sourceVertices.insert(vertex);

        EditableMeshTransaction transaction(mesh);
        std::vector<std::pair<EditableVertexID, EditableVertexID>> copies;
        copies.reserve(sourceVertices.size());
        for (const EditableVertexID sourceID : sourceVertices)
        {
            auto position = snapshot.Vertex(sourceID).Position;
            for (std::size_t axis = 0u; axis < 3u; ++axis) position[axis] += offset[axis];
            copies.emplace_back(sourceID, mesh.AddVertex(position));
        }
        auto copied = [&](EditableVertexID source) {
            const auto found = std::find_if(copies.begin(), copies.end(),
                [&](const auto& pair) { return pair.first == source; });
            if (found == copies.end()) throw std::logic_error("Missing duplicated vertex mapping.");
            return found->second;
        };

        std::vector<EditableFaceID> result;
        result.reserve(unique.size());
        for (const EditableFaceID faceID : unique)
        {
            const auto& source = snapshot.Face(faceID);
            std::vector<EditableVertexID> vertices;
            vertices.reserve(source.Vertices.size());
            for (const auto vertex : source.Vertices) vertices.push_back(copied(vertex));
            result.push_back(mesh.AddFace(std::move(vertices), source.MaterialSlot));
        }
        transaction.Commit();
        return result;
    }

    [[nodiscard]] inline EditableFaceID FillBoundary(
        EditableMesh& mesh,
        std::vector<EditableVertexID> orderedLoop,
        std::uint32_t materialSlot = 0u)
    {
        if (orderedLoop.size() < 3u)
            throw std::invalid_argument("Boundary fill requires at least three ordered vertices.");
        return mesh.AddFace(std::move(orderedLoop), materialSlot);
    }

    [[nodiscard]] inline std::vector<EditableFaceID> BridgeLoops(
        EditableMesh& mesh,
        const std::vector<EditableVertexID>& first,
        const std::vector<EditableVertexID>& second,
        std::uint32_t materialSlot = 0u)
    {
        if (first.size() < 3u || first.size() != second.size())
            throw std::invalid_argument("Bridge loops require equal loops with at least three vertices.");
        EditableMeshTransaction transaction(mesh);
        std::vector<EditableFaceID> result;
        result.reserve(first.size());
        for (std::size_t i = 0u; i < first.size(); ++i)
        {
            const std::size_t next = (i + 1u) % first.size();
            result.push_back(mesh.AddFace(
                { first[i], first[next], second[next], second[i] }, materialSlot));
        }
        transaction.Commit();
        return result;
    }

    [[nodiscard]] inline std::array<double, 3u> FaceNormal(
        const EditableMesh& mesh, EditableFaceID faceID)
    {
        const auto& face = mesh.Face(faceID);
        const auto& p0 = mesh.Vertex(face.Vertices.at(0)).Position;
        const auto& p1 = mesh.Vertex(face.Vertices.at(1)).Position;
        const auto& p2 = mesh.Vertex(face.Vertices.at(2)).Position;
        const std::array<double, 3u> a{ p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        const std::array<double, 3u> b{ p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        std::array<double, 3u> normal{
            a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0] };
        const double length = std::sqrt(
            normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
        if (length <= 1.0e-18)
            throw std::invalid_argument("Cannot calculate a normal for a degenerate face.");
        for (double& value : normal) value /= length;
        return normal;
    }

    inline void FlipFaceNormal(EditableMesh& mesh, EditableFaceID faceID)
    {
        auto& vertices = mesh.Face(faceID).Vertices;
        std::reverse(vertices.begin(), vertices.end());
    }

    [[nodiscard]] inline std::pair<EditableFaceID, EditableFaceID> KnifeFace(
        EditableMesh& mesh,
        EditableFaceID faceID,
        EditableEdgeKey firstEdge,
        EditableEdgeKey secondEdge,
        double firstT = 0.5,
        double secondT = 0.5)
    {
        firstEdge = EditableEdgeKey::Canonical(firstEdge.A, firstEdge.B);
        secondEdge = EditableEdgeKey::Canonical(secondEdge.A, secondEdge.B);
        if (firstEdge == secondEdge)
            throw std::invalid_argument("Knife cut requires two distinct boundary edges.");
        const EditableMeshFace original = mesh.Face(faceID);
        if (!modeling_detail::FaceUsesEdge(original, firstEdge) ||
            !modeling_detail::FaceUsesEdge(original, secondEdge))
            throw std::invalid_argument("Knife edges must both belong to the requested face.");

        EditableMeshTransaction transaction(mesh);
        const EditableVertexID first = SplitEdge(mesh, firstEdge, firstT);
        const EditableVertexID second = SplitEdge(mesh, secondEdge, secondT);
        const EditableMeshFace source = mesh.Face(faceID);
        const auto firstIt = std::find(source.Vertices.begin(), source.Vertices.end(), first);
        const auto secondIt = std::find(source.Vertices.begin(), source.Vertices.end(), second);
        if (firstIt == source.Vertices.end() || secondIt == source.Vertices.end())
            throw std::logic_error("Knife split vertices were not inserted into the target face.");
        const std::size_t firstIndex = static_cast<std::size_t>(firstIt - source.Vertices.begin());
        const std::size_t secondIndex = static_cast<std::size_t>(secondIt - source.Vertices.begin());

        std::vector<EditableVertexID> a;
        std::vector<EditableVertexID> b;
        for (std::size_t i = firstIndex;; i = (i + 1u) % source.Vertices.size())
        {
            a.push_back(source.Vertices[i]);
            if (i == secondIndex) break;
        }
        for (std::size_t i = secondIndex;; i = (i + 1u) % source.Vertices.size())
        {
            b.push_back(source.Vertices[i]);
            if (i == firstIndex) break;
        }
        if (a.size() < 3u || b.size() < 3u)
            throw std::invalid_argument("Knife cut would create a degenerate polygon.");

        mesh.RemoveFace(faceID);
        const EditableFaceID firstFace = mesh.AddFace(std::move(a), source.MaterialSlot);
        const EditableFaceID secondFace = mesh.AddFace(std::move(b), source.MaterialSlot);
        transaction.Commit();
        return { firstFace, secondFace };
    }

    [[nodiscard]] inline EditableFaceID DissolveEdge(
        EditableMesh& mesh, EditableEdgeKey edge)
    {
        edge = EditableEdgeKey::Canonical(edge.A, edge.B);
        std::vector<EditableFaceID> adjacent;
        for (const auto& [faceID, face] : mesh.Faces())
            if (modeling_detail::FaceUsesEdge(face, edge)) adjacent.push_back(faceID);
        if (adjacent.size() != 2u)
            throw std::invalid_argument("Dissolve edge requires exactly two adjacent faces.");

        const EditableMeshFace first = mesh.Face(adjacent[0]);
        const EditableMeshFace second = mesh.Face(adjacent[1]);
        if (first.MaterialSlot != second.MaterialSlot)
            throw std::invalid_argument("Cannot dissolve an edge between different material slots.");
        auto firstPath = modeling_detail::BoundaryPathWithoutEdge(first, edge.A, edge.B, edge);
        auto secondPath = modeling_detail::BoundaryPathWithoutEdge(second, edge.B, edge.A, edge);
        if (firstPath.size() < 2u || secondPath.size() < 2u)
            throw std::invalid_argument("Failed to derive a valid dissolve boundary.");

        std::vector<EditableVertexID> boundary = std::move(firstPath);
        boundary.insert(boundary.end(), secondPath.begin() + 1, secondPath.end() - 1);
        if (boundary.size() < 3u)
            throw std::invalid_argument("Dissolve edge would create a degenerate polygon.");

        EditableMeshTransaction transaction(mesh);
        mesh.RemoveFace(adjacent[0]);
        mesh.RemoveFace(adjacent[1]);
        const EditableFaceID result = mesh.AddFace(std::move(boundary), first.MaterialSlot);
        transaction.Commit();
        return result;
    }

    struct TranslateModifier final { std::array<double, 3u> Offset{}; };
    struct TriangulateModifier final {};
    struct SubdivideModifier final { std::uint32_t Levels = 1u; };
    using EditableMeshModifier = std::variant<
        TranslateModifier,
        TriangulateModifier,
        SubdivideModifier>;

    class EditableMeshModifierStack final
    {
    public:
        static constexpr std::size_t MaximumModifiers = 64u;

        void Add(EditableMeshModifier modifier)
        {
            if (m_Modifiers.size() >= MaximumModifiers)
                throw std::length_error("Editable mesh modifier stack exceeds 64 entries.");
            m_Modifiers.push_back(std::move(modifier));
        }

        void Clear() noexcept { m_Modifiers.clear(); }
        [[nodiscard]] const std::vector<EditableMeshModifier>& Modifiers() const noexcept
        { return m_Modifiers; }

        [[nodiscard]] EditableMesh Evaluate(const EditableMesh& source) const
        {
            EditableMesh result = source;
            for (const auto& modifier : m_Modifiers)
            {
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, TranslateModifier>)
                    {
                        std::vector<EditableVertexID> vertices;
                        vertices.reserve(result.Vertices().size());
                        for (const auto& [id, vertex] : result.Vertices())
                        {
                            (void)vertex;
                            vertices.push_back(id);
                        }
                        result.Translate(vertices, value.Offset);
                    }
                    else if constexpr (std::is_same_v<T, TriangulateModifier>)
                    {
                        std::vector<EditableFaceID> faces;
                        for (const auto& [id, face] : result.Faces())
                            if (face.Vertices.size() > 3u) faces.push_back(id);
                        for (const EditableFaceID face : faces)
                            (void)TriangulateFace(result, face);
                    }
                    else if constexpr (std::is_same_v<T, SubdivideModifier>)
                    {
                        if (value.Levels > 6u)
                            throw std::invalid_argument("Subdivision modifier is limited to six levels.");
                        for (std::uint32_t level = 0u; level < value.Levels; ++level)
                            SubdivideEditableMesh(result);
                    }
                }, modifier);
            }
            return result;
        }

    private:
        std::vector<EditableMeshModifier> m_Modifiers;
    };
}
