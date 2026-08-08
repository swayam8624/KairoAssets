module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.Assets.SculptProduction;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.Sculpting;

export namespace kairo::assets
{
    struct SculptViewportVertexUpdate final
    {
        EditableVertexID Vertex{};
        std::array<double, 3u> Position{};
    };

    struct SculptViewportUpdate final
    {
        std::vector<SculptViewportVertexUpdate> Vertices;
        bool TopologyChanged = false;
    };

    [[nodiscard]] inline SculptViewportUpdate MakeSculptViewportUpdate(
        const SculptStroke& stroke)
    {
        SculptViewportUpdate result;
        result.Vertices.reserve(stroke.Deltas.size());
        for (const auto& delta : stroke.Deltas)
            result.Vertices.push_back({ delta.Vertex, delta.After });
        std::sort(result.Vertices.begin(), result.Vertices.end(),
            [](const auto& a, const auto& b) { return a.Vertex < b.Vertex; });
        result.Vertices.erase(std::unique(result.Vertices.begin(), result.Vertices.end(),
            [](const auto& a, const auto& b) { return a.Vertex == b.Vertex; }),
            result.Vertices.end());
        return result;
    }

    struct SculptSessionBudget final
    {
        std::size_t MaximumUndoStrokes = 128u;
        std::size_t MaximumUndoBytes = 256u * 1024u * 1024u;
        std::size_t MaximumVerticesPerStroke = 1'000'000u;

        void Validate() const
        {
            if (MaximumUndoStrokes == 0u || MaximumUndoStrokes > 4096u)
                throw std::invalid_argument("Sculpt undo stroke budget must be in [1, 4096].");
            if (MaximumUndoBytes < 1024u || MaximumUndoBytes > 4ull * 1024ull * 1024ull * 1024ull)
                throw std::invalid_argument("Sculpt undo byte budget must be between 1 KiB and 4 GiB.");
            if (MaximumVerticesPerStroke == 0u || MaximumVerticesPerStroke > EditableMesh::MaximumVertices)
                throw std::invalid_argument("Sculpt per-stroke vertex budget is invalid.");
        }
    };

    class ProductionSculptSession final
    {
    public:
        explicit ProductionSculptSession(EditableMesh& mesh, SculptSessionBudget budget = {})
            : m_Mesh(mesh), m_Session(mesh), m_Budget(budget)
        {
            m_Budget.Validate();
        }

        void SetMask(EditableVertexID vertex, double mask) { m_Session.SetMask(vertex, mask); }
        [[nodiscard]] double Mask(EditableVertexID vertex) const { return m_Session.Mask(vertex); }

        [[nodiscard]] SculptViewportUpdate Apply(const SculptBrush& brush)
        {
            const std::size_t upperBound = AffectedVertexUpperBound(brush);
            if (upperBound > m_Budget.MaximumVerticesPerStroke)
                throw std::length_error("Sculpt stroke exceeds its per-stroke vertex budget.");
            if (m_Session.UndoDepth() >= m_Budget.MaximumUndoStrokes)
                throw std::length_error("Sculpt undo stroke budget is exhausted; commit or reopen the session.");
            constexpr std::size_t EstimatedDeltaBytes = sizeof(EditableVertexID) + 6u * sizeof(double);
            if (upperBound > (m_Budget.MaximumUndoBytes - std::min(m_UsedUndoBytes, m_Budget.MaximumUndoBytes)) /
                EstimatedDeltaBytes)
                throw std::length_error("Sculpt undo byte budget would be exceeded by this stroke.");

            const SculptStroke stroke = m_Session.Apply(brush);
            m_UsedUndoBytes += stroke.Deltas.size() * EstimatedDeltaBytes;
            m_StrokeBytes.push_back(stroke.Deltas.size() * EstimatedDeltaBytes);
            m_RedoBytes.clear();
            return MakeSculptViewportUpdate(stroke);
        }

        bool Undo()
        {
            if (m_StrokeBytes.empty()) return false;
            const std::size_t bytes = m_StrokeBytes.back();
            if (!m_Session.Undo()) return false;
            m_StrokeBytes.pop_back();
            m_RedoBytes.push_back(bytes);
            m_UsedUndoBytes -= bytes;
            return true;
        }

        bool Redo()
        {
            if (m_RedoBytes.empty()) return false;
            const std::size_t bytes = m_RedoBytes.back();
            if (m_UsedUndoBytes > m_Budget.MaximumUndoBytes - bytes)
                throw std::length_error("Redo would exceed the sculpt undo byte budget.");
            if (!m_Session.Redo()) return false;
            m_RedoBytes.pop_back();
            m_StrokeBytes.push_back(bytes);
            m_UsedUndoBytes += bytes;
            return true;
        }

        [[nodiscard]] std::size_t UndoBytes() const noexcept { return m_UsedUndoBytes; }
        [[nodiscard]] std::size_t UndoDepth() const noexcept { return m_StrokeBytes.size(); }
        [[nodiscard]] const SculptSessionBudget& Budget() const noexcept { return m_Budget; }

    private:
        EditableMesh& m_Mesh;
        SculptSession m_Session;
        SculptSessionBudget m_Budget;
        std::vector<std::size_t> m_StrokeBytes;
        std::vector<std::size_t> m_RedoBytes;
        std::size_t m_UsedUndoBytes = 0u;

        [[nodiscard]] std::size_t AffectedVertexUpperBound(const SculptBrush& brush) const
        {
            if (!std::isfinite(brush.Radius) || brush.Radius <= 0.0)
                throw std::invalid_argument("Sculpt brush radius must be finite and positive.");
            auto countAt = [&](std::array<double, 3u> center) {
                std::size_t count = 0u;
                const double radiusSquared = brush.Radius * brush.Radius;
                for (const auto& [id, vertex] : m_Mesh.Vertices())
                {
                    (void)id;
                    const double dx = vertex.Position[0] - center[0];
                    const double dy = vertex.Position[1] - center[1];
                    const double dz = vertex.Position[2] - center[2];
                    if (dx*dx + dy*dy + dz*dz <= radiusSquared) ++count;
                }
                return count;
            };
            std::size_t result = countAt(brush.Center);
            if (brush.Symmetry != SculptSymmetryAxis::None)
            {
                auto mirrored = brush.Center;
                const std::size_t axis = static_cast<std::size_t>(brush.Symmetry) - 1u;
                mirrored[axis] = -mirrored[axis];
                result = std::min(m_Mesh.Vertices().size(), result + countAt(mirrored));
            }
            return result;
        }
    };

    struct SculptRemeshSettings final
    {
        std::size_t MinimumFaces = 10'000u;
        std::size_t MaximumFaces = 1'000'000u;
        std::uint32_t MaximumSubdivisionPasses = 4u;

        void Validate() const
        {
            if (MinimumFaces == 0u || MaximumFaces < MinimumFaces ||
                MaximumFaces > EditableMesh::MaximumFaces)
                throw std::invalid_argument("Sculpt remesh face budget is invalid.");
            if (MaximumSubdivisionPasses > 8u)
                throw std::invalid_argument("Sculpt remesh subdivision pass count exceeds eight.");
        }
    };

    /// Uniform deterministic remeshing for the current topology kernel. It
    /// raises sparse meshes toward a requested detail floor without crossing a
    /// hard face budget. Future adaptive topology can replace this strategy
    /// behind the same budget contract.
    [[nodiscard]] inline SculptViewportUpdate RemeshForSculpt(
        EditableMesh& mesh,
        const SculptRemeshSettings& settings)
    {
        settings.Validate();
        bool changed = false;
        for (std::uint32_t pass = 0u;
            pass < settings.MaximumSubdivisionPasses && mesh.Faces().size() < settings.MinimumFaces;
            ++pass)
        {
            const std::size_t current = mesh.Faces().size();
            if (current == 0u) throw std::invalid_argument("Cannot remesh an empty editable mesh.");
            if (current > settings.MaximumFaces / 4u) break;
            SubdivideEditableMesh(mesh);
            if (mesh.Faces().size() > settings.MaximumFaces)
                throw std::logic_error("Sculpt remesh exceeded the validated face budget.");
            changed = true;
        }
        SculptViewportUpdate update;
        update.TopologyChanged = changed;
        if (changed)
        {
            update.Vertices.reserve(mesh.Vertices().size());
            for (const auto& [id, vertex] : mesh.Vertices())
                update.Vertices.push_back({ id, vertex.Position });
        }
        return update;
    }

    class SculptMultiresolution final
    {
    public:
        static constexpr std::size_t MaximumLevels = 6u;

        explicit SculptMultiresolution(EditableMesh base)
        {
            const auto validation = base.Validate();
            if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());
            m_Levels.push_back(std::move(base));
        }

        void BuildNextLevel(std::size_t maximumFaces = 1'000'000u)
        {
            if (m_Levels.size() >= MaximumLevels)
                throw std::length_error("Sculpt multiresolution exceeds six levels.");
            if (m_Levels.back().Faces().size() > maximumFaces / 4u)
                throw std::length_error("Next sculpt multiresolution level would exceed its face budget.");
            EditableMesh next = m_Levels.back();
            SubdivideEditableMesh(next);
            if (next.Faces().size() > maximumFaces)
                throw std::length_error("Sculpt multiresolution level exceeds its face budget.");
            m_Levels.push_back(std::move(next));
        }

        void SetActiveLevel(std::size_t level)
        {
            if (level >= m_Levels.size()) throw std::out_of_range("Sculpt multiresolution level is out of range.");
            m_Active = level;
        }

        [[nodiscard]] EditableMesh& ActiveMesh() noexcept { return m_Levels[m_Active]; }
        [[nodiscard]] const EditableMesh& ActiveMesh() const noexcept { return m_Levels[m_Active]; }
        [[nodiscard]] std::size_t ActiveLevel() const noexcept { return m_Active; }
        [[nodiscard]] std::size_t LevelCount() const noexcept { return m_Levels.size(); }

    private:
        std::vector<EditableMesh> m_Levels;
        std::size_t m_Active = 0u;
    };
}
