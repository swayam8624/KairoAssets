module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.Assets.EditableMeshOperations;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.Sculpting;

export namespace kairo::assets
{
    [[nodiscard]] inline EditableVertexID SplitEdge(EditableMesh& mesh,
        EditableEdgeKey edge, double t = 0.5)
    {
        edge = EditableEdgeKey::Canonical(edge.A, edge.B);
        if (!std::isfinite(t) || t <= 0.0 || t >= 1.0)
            throw std::invalid_argument("Edge split parameter must be in (0, 1).");
        const auto a = mesh.Vertex(edge.A).Position;
        const auto b = mesh.Vertex(edge.B).Position;
        bool used = false;
        for (const auto& [faceID, face] : mesh.Faces())
        {
            (void)faceID;
            for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
            {
                const auto x = face.Vertices[i];
                const auto y = face.Vertices[(i + 1u) % face.Vertices.size()];
                if (EditableEdgeKey::Canonical(x, y) == edge) used = true;
            }
        }
        if (!used) throw std::out_of_range("Edge split target does not exist.");

        EditableMeshTransaction transaction(mesh);
        const EditableVertexID created = mesh.AddVertex({
            a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t });
        for (auto& [faceID, face] : const_cast<std::map<EditableFaceID, EditableMeshFace>&>(mesh.Faces()))
        {
            (void)faceID;
            for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
            {
                const auto x = face.Vertices[i];
                const auto y = face.Vertices[(i + 1u) % face.Vertices.size()];
                if (x == created || y == created) continue;
                if (EditableEdgeKey::Canonical(x, y) == edge)
                {
                    face.Vertices.insert(face.Vertices.begin() + static_cast<std::ptrdiff_t>(i + 1u), created);
                    ++i;
                }
            }
        }
        transaction.Commit();
        return created;
    }

    [[nodiscard]] inline std::vector<EditableFaceID> TriangulateFace(
        EditableMesh& mesh, EditableFaceID faceID)
    {
        const EditableMeshFace face = mesh.Face(faceID);
        if (face.Vertices.size() == 3u) return { faceID };
        EditableMeshTransaction transaction(mesh);
        mesh.RemoveFace(faceID);
        std::vector<EditableFaceID> created;
        created.reserve(face.Vertices.size() - 2u);
        for (std::size_t i = 1u; i + 1u < face.Vertices.size(); ++i)
            created.push_back(mesh.AddFace({ face.Vertices[0], face.Vertices[i], face.Vertices[i + 1u] },
                face.MaterialSlot));
        transaction.Commit();
        return created;
    }

    [[nodiscard]] inline std::vector<EditableFaceID> DuplicateFaces(
        EditableMesh& mesh, const std::vector<EditableFaceID>& selection,
        std::array<double, 3u> offset = {})
    {
        std::set<EditableFaceID> uniqueFaces(selection.begin(), selection.end());
        if (uniqueFaces.empty()) return {};
        const EditableMesh snapshot = mesh;
        std::set<EditableVertexID> sourceVertices;
        for (const auto faceID : uniqueFaces)
            for (const auto vertex : snapshot.Face(faceID).Vertices) sourceVertices.insert(vertex);

        EditableMeshTransaction transaction(mesh);
        std::map<EditableVertexID, EditableVertexID> copies;
        for (const auto source : sourceVertices)
        {
            auto position = snapshot.Vertex(source).Position;
            for (std::size_t axis = 0u; axis < 3u; ++axis) position[axis] += offset[axis];
            copies.emplace(source, mesh.AddVertex(position));
        }
        std::vector<EditableFaceID> result;
        for (const auto faceID : uniqueFaces)
        {
            const auto& source = snapshot.Face(faceID);
            std::vector<EditableVertexID> vertices;
            vertices.reserve(source.Vertices.size());
            for (const auto vertex : source.Vertices) vertices.push_back(copies.at(vertex));
            result.push_back(mesh.AddFace(std::move(vertices), source.MaterialSlot));
        }
        transaction.Commit();
        return result;
    }

    [[nodiscard]] inline EditableFaceID FillBoundary(
        EditableMesh& mesh, std::vector<EditableVertexID> orderedLoop,
        std::uint32_t materialSlot = 0u)
    {
        if (orderedLoop.size() < 3u)
            throw std::invalid_argument("Boundary fill requires at least three ordered vertices.");
        return mesh.AddFace(std::move(orderedLoop), materialSlot);
    }

    [[nodiscard]] inline std::vector<EditableFaceID> BridgeLoops(
        EditableMesh& mesh,
        const std::vector<EditableVertexID>& a,
        const std::vector<EditableVertexID>& b,
        std::uint32_t materialSlot = 0u)
    {
        if (a.size() < 2u || a.size() != b.size())
            throw std::invalid_argument("Bridge loops must contain equal numbers of at least two vertices.");
        EditableMeshTransaction transaction(mesh);
        std::vector<EditableFaceID> created;
        created.reserve(a.size());
        for (std::size_t i = 0u; i < a.size(); ++i)
        {
            const std::size_t next = (i + 1u) % a.size();
            created.push_back(mesh.AddFace({ a[i], a[next], b[next], b[i] }, materialSlot));
        }
        transaction.Commit();
        return created;
    }

    [[nodiscard]] inline std::array<double, 3u> FaceNormal(
        const EditableMesh& mesh, EditableFaceID faceID)
    {
        const auto& face = mesh.Face(faceID);
        if (face.Vertices.size() < 3u) throw std::invalid_argument("Face needs at least three vertices.");
        const auto& p0 = mesh.Vertex(face.Vertices[0]).Position;
        const auto& p1 = mesh.Vertex(face.Vertices[1]).Position;
        const auto& p2 = mesh.Vertex(face.Vertices[2]).Position;
        const std::array<double, 3u> a{ p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        const std::array<double, 3u> b{ p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        std::array<double, 3u> n{
            a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0] };
        const double length = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (length <= 1.0e-18) throw std::invalid_argument("Cannot calculate a normal for a degenerate face.");
        for (double& value : n) value /= length;
        return n;
    }

    inline void FlipFaceNormal(EditableMesh& mesh, EditableFaceID faceID)
    {
        auto& face = mesh.Face(faceID);
        std::reverse(face.Vertices.begin(), face.Vertices.end());
    }

    /// Splits one polygon by a knife segment whose endpoints lie on two of its edges.
    [[nodiscard]] inline std::pair<EditableFaceID, EditableFaceID> KnifeFace(
        EditableMesh& mesh,
        EditableFaceID faceID,
        EditableEdgeKey firstEdge,
        EditableEdgeKey secondEdge,
        double firstT = 0.5,
        double secondT = 0.5)
    {
        if (EditableEdgeKey::Canonical(firstEdge.A, firstEdge.B) ==
            EditableEdgeKey::Canonical(secondEdge.A, secondEdge.B))
            throw std::invalid_argument("Knife cut requires two distinct boundary edges.");
        EditableMeshTransaction transaction(mesh);
        const EditableVertexID first = SplitEdge(mesh, firstEdge, firstT);
        const EditableVertexID second = SplitEdge(mesh, secondEdge, secondT);
        const EditableMeshFace source = mesh.Face(faceID);
        auto firstIt = std::find(source.Vertices.begin(), source.Vertices.end(), first);
        auto secondIt = std::find(source.Vertices.begin(), source.Vertices.end(), second);
        if (firstIt == source.Vertices.end() || secondIt == source.Vertices.end())
            throw std::invalid_argument("Knife edges do not belong to the requested face.");
        const std::size_t i0 = static_cast<std::size_t>(firstIt - source.Vertices.begin());
        const std::size_t i1 = static_cast<std::size_t>(secondIt - source.Vertices.begin());
        if (i0 == i1) throw std::invalid_argument("Knife cut collapsed to one vertex.");

        std::vector<EditableVertexID> a;
        std::vector<EditableVertexID> b;
        for (std::size_t i = i0;; i = (i + 1u) % source.Vertices.size())
        {
            a.push_back(source.Vertices[i]);
            if (i == i1) break;
        }
        for (std::size_t i = i1;; i = (i + 1u) % source.Vertices.size())
        {
            b.push_back(source.Vertices[i]);
            if (i == i0) break;
        }
        if (a.size() < 3u || b.size() < 3u)
            throw std::invalid_argument("Knife cut would create a degenerate polygon.");
        mesh.RemoveFace(faceID);
        const EditableFaceID fa = mesh.AddFace(std::move(a), source.MaterialSlot);
        const EditableFaceID fb = mesh.AddFace(std::move(b), source.MaterialSlot);
        transaction.Commit();
        return { fa, fb };
    }

    /// Dissolves an edge shared by exactly two faces, replacing both polygons
    /// with their combined boundary. The operation rejects non-manifold or
    /// self-intersecting ID layouts through EditableMesh::Validate().
    [[nodiscard]] inline EditableFaceID DissolveEdge(EditableMesh& mesh, EditableEdgeKey edge)
    {
        edge = EditableEdgeKey::Canonical(edge.A, edge.B);
        std::vector<EditableFaceID> adjacent;
        for (const auto& half : mesh.HalfEdges())
            if (EditableEdgeKey::Canonical(half.From, half.To) == edge)
                adjacent.push_back(half.Face);
        std::sort(adjacent.begin(), adjacent.end());
        adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
        if (adjacent.size() != 2u)
            throw std::invalid_argument("Dissolve edge requires exactly two adjacent faces.");

        const EditableMeshFace first = mesh.Face(adjacent[0]);
        const EditableMeshFace second = mesh.Face(adjacent[1]);
        if (first.MaterialSlot != second.MaterialSlot)
            throw std::invalid_argument("Cannot dissolve an edge between different material slots.");

        auto pathWithoutEdge = [&](const EditableMeshFace& face,
            EditableVertexID start, EditableVertexID end) {
            std::vector<EditableVertexID> path;
            const auto startIt = std::find(face.Vertices.begin(), face.Vertices.end(), start);
            if (startIt == face.Vertices.end()) return path;
            std::size_t index = static_cast<std::size_t>(startIt - face.Vertices.begin());
            path.push_back(start);
            for (std::size_t guard = 0u; guard <= face.Vertices.size(); ++guard)
            {
                const std::size_t next = (index + 1u) % face.Vertices.size();
                const auto a = face.Vertices[index];
                const auto b = face.Vertices[next];
                if (EditableEdgeKey::Canonical(a, b) == edge)
                {
                    index = (index + face.Vertices.size() - 1u) % face.Vertices.size();
                    path.clear();
                    path.push_back(start);
                    for (std::size_t reverseGuard = 0u; reverseGuard <= face.Vertices.size(); ++reverseGuard)
                    {
                        if (face.Vertices[index] == end) break;
                        path.push_back(face.Vertices[index]);
                        index = (index + face.Vertices.size() - 1u) % face.Vertices.size();
                    }
                    if (path.back() != end) path.push_back(end);
                    return path;
                }
                index = next;
                if (face.Vertices[index] == end) { path.push_back(end); return path; }
                path.push_back(face.Vertices[index]);
            }
            return std::vector<EditableVertexID>{};
        };

        auto p0 = pathWithoutEdge(first, edge.A, edge.B);
        auto p1 = pathWithoutEdge(second, edge.B, edge.A);
        if (p0.size() < 2u || p1.size() < 2u)
            throw std::invalid_argument("Failed to derive dissolve boundary.");
        std::vector<EditableVertexID> combined = p0;
        combined.insert(combined.end(), p1.begin() + 1, p1.end() - 1);
        if (combined.front() == combined.back()) combined.pop_back();

        EditableMeshTransaction transaction(mesh);
        mesh.RemoveFace(adjacent[0]);
        mesh.RemoveFace(adjacent[1]);
        const EditableFaceID created = mesh.AddFace(std::move(combined), first.MaterialSlot);
        transaction.Commit();
        return created;
    }

    struct TranslateModifier final { std::array<double, 3u> Offset{}; };
    struct TriangulateModifier final {};
    struct SubdivideModifier final { std::uint32_t Levels = 1u; };
    using EditableMeshModifier = std::variant<TranslateModifier, TriangulateModifier, SubdivideModifier>;

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
        [[nodiscard]] const std::vector<EditableMeshModifier>& Modifiers() const noexcept { return m_Modifiers; }

        [[nodiscard]] EditableMesh Evaluate(const EditableMesh& source) const
        {
            EditableMesh result = source;
            for (const auto& modifier : m_Modifiers)
            {
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, TranslateModifier>)
                    {
                        std::vector<EditableVertexID> all;
                        for (const auto& [id, vertex] : result.Vertices()) { (void)vertex; all.push_back(id); }
                        result.Translate(all, value.Offset);
                    }
                    else if constexpr (std::is_same_v<T, TriangulateModifier>)
                    {
                        std::vector<EditableFaceID> faces;
                        for (const auto& [id, face] : result.Faces())
                            if (face.Vertices.size() > 3u) faces.push_back(id);
                        for (const auto face : faces) (void)TriangulateFace(result, face);
                    }
                    else if constexpr (std::is_same_v<T, SubdivideModifier>)
                    {
                        if (value.Levels > 6u) throw std::invalid_argument("Subdivision modifier is limited to six levels.");
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
