module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.Sculpting;

import Kairo.Assets.EditableMesh;

export namespace kairo::assets
{
    enum class SculptBrushMode : std::uint8_t { Inflate, Smooth, Grab };
    enum class SculptSymmetryAxis : std::uint8_t { None, X, Y, Z };

    struct SculptBrush final
    {
        SculptBrushMode Mode = SculptBrushMode::Inflate;
        std::array<double, 3u> Center{};
        std::array<double, 3u> Delta{};
        double Radius = 1.0;
        double Strength = 0.25;
        double FalloffPower = 2.0;
        SculptSymmetryAxis Symmetry = SculptSymmetryAxis::None;
    };

    struct SculptVertexDelta final
    {
        EditableVertexID Vertex{};
        std::array<double, 3u> Before{};
        std::array<double, 3u> After{};
    };

    struct SculptStroke final
    {
        std::vector<SculptVertexDelta> Deltas;
    };

    class SculptSession final
    {
    public:
        explicit SculptSession(EditableMesh& mesh) : m_Mesh(mesh) {}

        void SetMask(EditableVertexID vertex, double mask)
        {
            (void)m_Mesh.Vertex(vertex);
            if (!std::isfinite(mask) || mask < 0.0 || mask > 1.0)
                throw std::invalid_argument("Sculpt mask must be in [0, 1].");
            if (mask == 0.0) m_Masks.erase(vertex);
            else m_Masks.insert_or_assign(vertex, mask);
        }

        [[nodiscard]] double Mask(EditableVertexID vertex) const
        {
            const auto found = m_Masks.find(vertex);
            return found == m_Masks.end() ? 0.0 : found->second;
        }

        [[nodiscard]] SculptStroke Apply(const SculptBrush& brush)
        {
            ValidateBrush(brush);
            SculptStroke stroke;
            const auto adjacency = BuildAdjacency();
            const auto original = SnapshotPositions();
            ApplyOne(brush, brush.Center, original, adjacency, stroke);
            if (brush.Symmetry != SculptSymmetryAxis::None)
            {
                auto mirrored = brush.Center;
                const std::size_t axis = static_cast<std::size_t>(brush.Symmetry) - 1u;
                mirrored[axis] = -mirrored[axis];
                ApplyOne(brush, mirrored, original, adjacency, stroke);
            }
            Coalesce(stroke);
            m_Undo.push_back(stroke);
            m_Redo.clear();
            return stroke;
        }

        bool Undo()
        {
            if (m_Undo.empty()) return false;
            SculptStroke stroke = std::move(m_Undo.back());
            m_Undo.pop_back();
            for (const auto& delta : stroke.Deltas) m_Mesh.Vertex(delta.Vertex).Position = delta.Before;
            m_Redo.push_back(std::move(stroke));
            return true;
        }

        bool Redo()
        {
            if (m_Redo.empty()) return false;
            SculptStroke stroke = std::move(m_Redo.back());
            m_Redo.pop_back();
            for (const auto& delta : stroke.Deltas) m_Mesh.Vertex(delta.Vertex).Position = delta.After;
            m_Undo.push_back(std::move(stroke));
            return true;
        }

        [[nodiscard]] std::size_t UndoDepth() const noexcept { return m_Undo.size(); }

    private:
        EditableMesh& m_Mesh;
        std::map<EditableVertexID, double> m_Masks;
        std::vector<SculptStroke> m_Undo;
        std::vector<SculptStroke> m_Redo;

        [[nodiscard]] std::map<EditableVertexID, std::set<EditableVertexID>> BuildAdjacency() const
        {
            std::map<EditableVertexID, std::set<EditableVertexID>> result;
            for (const auto& [faceID, face] : m_Mesh.Faces())
            {
                (void)faceID;
                for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
                {
                    const auto a = face.Vertices[i];
                    const auto b = face.Vertices[(i + 1u) % face.Vertices.size()];
                    result[a].insert(b);
                    result[b].insert(a);
                }
            }
            return result;
        }

        [[nodiscard]] std::map<EditableVertexID, std::array<double, 3u>> SnapshotPositions() const
        {
            std::map<EditableVertexID, std::array<double, 3u>> result;
            for (const auto& [id, vertex] : m_Mesh.Vertices()) result.emplace(id, vertex.Position);
            return result;
        }

        void ApplyOne(const SculptBrush& brush, const std::array<double, 3u>& center,
            const std::map<EditableVertexID, std::array<double, 3u>>& original,
            const std::map<EditableVertexID, std::set<EditableVertexID>>& adjacency,
            SculptStroke& stroke)
        {
            for (const auto& [id, before] : original)
            {
                const double dx = before[0] - center[0];
                const double dy = before[1] - center[1];
                const double dz = before[2] - center[2];
                const double distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (distance > brush.Radius) continue;
                const double t = std::clamp(1.0 - distance / brush.Radius, 0.0, 1.0);
                const double weight = std::pow(t, brush.FalloffPower) * brush.Strength * (1.0 - Mask(id));
                if (weight <= 0.0) continue;

                auto after = m_Mesh.Vertex(id).Position;
                if (brush.Mode == SculptBrushMode::Grab)
                {
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        after[axis] += brush.Delta[axis] * weight;
                }
                else if (brush.Mode == SculptBrushMode::Inflate)
                {
                    const double length = std::max(distance, 1.0e-12);
                    after[0] += (dx / length) * weight;
                    after[1] += (dy / length) * weight;
                    after[2] += (dz / length) * weight;
                }
                else
                {
                    const auto found = adjacency.find(id);
                    if (found == adjacency.end() || found->second.empty()) continue;
                    std::array<double, 3u> average{};
                    for (const auto neighbour : found->second)
                    {
                        const auto& p = original.at(neighbour);
                        for (std::size_t axis = 0u; axis < 3u; ++axis) average[axis] += p[axis];
                    }
                    for (double& value : average) value /= static_cast<double>(found->second.size());
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        after[axis] += (average[axis] - before[axis]) * weight;
                }
                m_Mesh.Vertex(id).Position = after;
                stroke.Deltas.push_back({ id, before, after });
            }
        }

        static void Coalesce(SculptStroke& stroke)
        {
            std::map<EditableVertexID, SculptVertexDelta> unique;
            for (const auto& delta : stroke.Deltas)
            {
                const auto found = unique.find(delta.Vertex);
                if (found == unique.end()) unique.emplace(delta.Vertex, delta);
                else found->second.After = delta.After;
            }
            stroke.Deltas.clear();
            stroke.Deltas.reserve(unique.size());
            for (const auto& [id, delta] : unique) { (void)id; stroke.Deltas.push_back(delta); }
        }

        static void ValidateBrush(const SculptBrush& brush)
        {
            if (!std::isfinite(brush.Radius) || brush.Radius <= 0.0 ||
                !std::isfinite(brush.Strength) || brush.Strength < 0.0 ||
                !std::isfinite(brush.FalloffPower) || brush.FalloffPower <= 0.0)
                throw std::invalid_argument("Sculpt brush radius/strength/falloff is invalid.");
            for (double value : brush.Center) if (!std::isfinite(value))
                throw std::invalid_argument("Sculpt brush center must be finite.");
            for (double value : brush.Delta) if (!std::isfinite(value))
                throw std::invalid_argument("Sculpt brush delta must be finite.");
        }
    };

    inline void SubdivideEditableMesh(EditableMesh& mesh)
    {
        const EditableMesh before = mesh;
        EditableMesh rebuilt;
        std::map<EditableVertexID, EditableVertexID> copied;
        for (const auto& [id, vertex] : before.Vertices()) copied.emplace(id, rebuilt.AddVertex(vertex.Position));
        std::map<EditableEdgeKey, EditableVertexID> midpoint;
        const auto midpointFor = [&](EditableVertexID a, EditableVertexID b) mutable -> EditableVertexID {
            const auto edge = EditableEdgeKey::Canonical(a, b);
            const auto found = midpoint.find(edge);
            if (found != midpoint.end()) return found->second;
            const auto& pa = before.Vertex(a).Position;
            const auto& pb = before.Vertex(b).Position;
            const EditableVertexID created = rebuilt.AddVertex({
                (pa[0]+pb[0])*0.5, (pa[1]+pb[1])*0.5, (pa[2]+pb[2])*0.5 });
            midpoint.emplace(edge, created);
            return created;
        };

        for (const auto& [faceID, face] : before.Faces())
        {
            (void)faceID;
            if (face.Vertices.size() == 3u)
            {
                const auto a = copied.at(face.Vertices[0]);
                const auto b = copied.at(face.Vertices[1]);
                const auto c = copied.at(face.Vertices[2]);
                const auto ab = midpointFor(face.Vertices[0], face.Vertices[1]);
                const auto bc = midpointFor(face.Vertices[1], face.Vertices[2]);
                const auto ca = midpointFor(face.Vertices[2], face.Vertices[0]);
                (void)rebuilt.AddFace({ a, ab, ca }, face.MaterialSlot);
                (void)rebuilt.AddFace({ ab, b, bc }, face.MaterialSlot);
                (void)rebuilt.AddFace({ ca, bc, c }, face.MaterialSlot);
                (void)rebuilt.AddFace({ ab, bc, ca }, face.MaterialSlot);
            }
            else
            {
                std::array<double, 3u> center{};
                for (const auto source : face.Vertices)
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        center[axis] += before.Vertex(source).Position[axis];
                for (double& value : center) value /= static_cast<double>(face.Vertices.size());
                const auto centerID = rebuilt.AddVertex(center);
                for (std::size_t i = 0u; i < face.Vertices.size(); ++i)
                {
                    const auto next = (i + 1u) % face.Vertices.size();
                    (void)rebuilt.AddFace({ copied.at(face.Vertices[i]),
                        midpointFor(face.Vertices[i], face.Vertices[next]), centerID }, face.MaterialSlot);
                }
            }
        }
        mesh = std::move(rebuilt);
    }
}
