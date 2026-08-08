module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.AdvancedModelingOperations;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.ModelingOperations;

export namespace kairo::assets
{
    [[nodiscard]] inline EditableFaceID BevelFace(
        EditableMesh& mesh,
        EditableFaceID faceID,
        double insetAmount,
        double depth)
    {
        if (!std::isfinite(depth))
            throw std::invalid_argument("Bevel depth must be finite.");
        const auto normal = FaceNormal(mesh, faceID);
        EditableMeshTransaction transaction(mesh);
        const EditableFaceID inner = mesh.InsetFace(faceID, insetAmount);
        const auto vertices = mesh.Face(inner).Vertices;
        mesh.Translate(vertices, {
            normal[0] * depth,
            normal[1] * depth,
            normal[2] * depth });
        transaction.Commit();
        return inner;
    }

    struct LoopCutResult final
    {
        std::vector<EditableVertexID> CutVertices;
        std::vector<EditableFaceID> CreatedFaces;
    };

    /// Propagates a cut through a connected strip of quads by crossing each
    /// face through the edge opposite the incoming edge. Branching/non-quad
    /// topology terminates the strip instead of guessing a continuation.
    [[nodiscard]] inline LoopCutResult LoopCutQuadStrip(
        EditableMesh& mesh,
        EditableEdgeKey startEdge,
        double t = 0.5)
    {
        startEdge = EditableEdgeKey::Canonical(startEdge.A, startEdge.B);
        if (!std::isfinite(t) || t <= 0.0 || t >= 1.0)
            throw std::invalid_argument("Loop-cut parameter must be in (0, 1).");
        const EditableMesh snapshot = mesh;

        struct StripFace final
        {
            EditableFaceID Face;
            EditableEdgeKey Entry;
            EditableEdgeKey Exit;
        };

        std::map<EditableEdgeKey, std::vector<EditableFaceID>> edgeFaces;
        for (const auto& [faceID, face] : snapshot.Faces())
            for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
                edgeFaces[EditableEdgeKey::Canonical(
                    face.Vertices[i], face.Vertices[(i + 1u) % face.Vertices.size()])].push_back(faceID);
        const auto start = edgeFaces.find(startEdge);
        if (start == edgeFaces.end()) throw std::out_of_range("Loop-cut start edge does not exist.");
        if (start->second.size() > 2u) throw std::invalid_argument("Loop-cut start edge is non-manifold.");

        std::queue<std::pair<EditableFaceID, EditableEdgeKey>> pending;
        for (const auto face : start->second) pending.push({ face, startEdge });
        std::set<EditableFaceID> visited;
        std::vector<StripFace> strip;
        while (!pending.empty())
        {
            const auto [faceID, entry] = pending.front();
            pending.pop();
            if (!visited.insert(faceID).second) continue;
            const auto& face = snapshot.Face(faceID);
            if (face.Vertices.size() != 4u) continue;

            std::optional<std::size_t> entryIndex;
            for (std::size_t i = 0u; i < 4u; ++i)
                if (EditableEdgeKey::Canonical(face.Vertices[i], face.Vertices[(i + 1u) % 4u]) == entry)
                { entryIndex = i; break; }
            if (!entryIndex.has_value()) continue;
            const std::size_t opposite = (*entryIndex + 2u) % 4u;
            const EditableEdgeKey exit = EditableEdgeKey::Canonical(
                face.Vertices[opposite], face.Vertices[(opposite + 1u) % 4u]);
            strip.push_back({ faceID, entry, exit });

            const auto neighbours = edgeFaces.find(exit);
            if (neighbours == edgeFaces.end() || neighbours->second.size() != 2u) continue;
            for (const auto neighbour : neighbours->second)
                if (neighbour != faceID && !visited.contains(neighbour))
                    pending.push({ neighbour, exit });
        }
        if (strip.empty())
            throw std::invalid_argument("Loop cut requires at least one quad adjacent to the selected edge.");

        std::set<EditableEdgeKey> uniqueEdges;
        for (const auto& record : strip)
        {
            uniqueEdges.insert(record.Entry);
            uniqueEdges.insert(record.Exit);
        }

        EditableMeshTransaction transaction(mesh);
        std::map<EditableEdgeKey, EditableVertexID> cutVertex;
        LoopCutResult result;
        for (const auto edge : uniqueEdges)
        {
            const auto created = SplitEdge(mesh, edge, t);
            cutVertex.emplace(edge, created);
            result.CutVertices.push_back(created);
        }

        for (const auto& record : strip)
        {
            const EditableVertexID first = cutVertex.at(record.Entry);
            const EditableVertexID second = cutVertex.at(record.Exit);
            const EditableMeshFace current = mesh.Face(record.Face);
            const auto firstIt = std::find(current.Vertices.begin(), current.Vertices.end(), first);
            const auto secondIt = std::find(current.Vertices.begin(), current.Vertices.end(), second);
            if (firstIt == current.Vertices.end() || secondIt == current.Vertices.end())
                throw std::logic_error("Loop-cut split vertices are missing from a strip face.");
            const std::size_t firstIndex = static_cast<std::size_t>(firstIt - current.Vertices.begin());
            const std::size_t secondIndex = static_cast<std::size_t>(secondIt - current.Vertices.begin());
            std::vector<EditableVertexID> a;
            std::vector<EditableVertexID> b;
            for (std::size_t i = firstIndex;; i = (i + 1u) % current.Vertices.size())
            {
                a.push_back(current.Vertices[i]);
                if (i == secondIndex) break;
            }
            for (std::size_t i = secondIndex;; i = (i + 1u) % current.Vertices.size())
            {
                b.push_back(current.Vertices[i]);
                if (i == firstIndex) break;
            }
            if (a.size() < 3u || b.size() < 3u)
                throw std::logic_error("Loop cut would create a degenerate polygon.");
            mesh.RemoveFace(record.Face);
            result.CreatedFaces.push_back(mesh.AddFace(std::move(a), current.MaterialSlot));
            result.CreatedFaces.push_back(mesh.AddFace(std::move(b), current.MaterialSlot));
        }
        transaction.Commit();
        std::sort(result.CutVertices.begin(), result.CutVertices.end());
        std::sort(result.CreatedFaces.begin(), result.CreatedFaces.end());
        return result;
    }

    using EditableVertexNormalMap = std::map<EditableVertexID, std::array<double, 3u>>;

    [[nodiscard]] inline EditableVertexNormalMap RecalculateSmoothNormals(
        const EditableMesh& mesh)
    {
        const auto validation = mesh.Validate();
        if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());
        EditableVertexNormalMap normals;
        for (const auto& [id, vertex] : mesh.Vertices())
        {
            (void)vertex;
            normals.emplace(id, std::array<double, 3u>{});
        }
        for (const auto& [faceID, face] : mesh.Faces())
        {
            const auto normal = FaceNormal(mesh, faceID);
            for (const auto vertex : face.Vertices)
                for (std::size_t axis = 0u; axis < 3u; ++axis)
                    normals.at(vertex)[axis] += normal[axis];
        }
        for (auto& [id, normal] : normals)
        {
            (void)id;
            const double length = std::sqrt(
                normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
            if (length <= 1.0e-18) continue;
            for (double& value : normal) value /= length;
        }
        return normals;
    }
}
