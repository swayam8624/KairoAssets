#include <catch2/catch_test_macros.hpp>

import Kairo.Assets.EditableMesh;
import Kairo.Assets.Sculpting;
import Kairo.Assets.SculptProduction;

namespace
{
    kairo::assets::EditableMesh MakeSculptQuad()
    {
        using namespace kairo::assets;
        EditableMesh mesh;
        const auto a = mesh.AddVertex({ -1.0, -1.0, 0.0 });
        const auto b = mesh.AddVertex({ 1.0, -1.0, 0.0 });
        const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
        const auto d = mesh.AddVertex({ -1.0, 1.0, 0.0 });
        (void)mesh.AddFace({ a, b, c, d });
        return mesh;
    }
}

TEST_CASE("production sculpt strokes return incremental viewport updates and bounded undo")
{
    using namespace kairo::assets;
    EditableMesh mesh = MakeSculptQuad();
    SculptSessionBudget budget;
    budget.MaximumUndoStrokes = 4u;
    budget.MaximumUndoBytes = 1024u * 1024u;
    budget.MaximumVerticesPerStroke = 16u;
    ProductionSculptSession session(mesh, budget);

    SculptBrush brush;
    brush.Mode = SculptBrushMode::Inflate;
    brush.Center = { 0.0, 0.0, 0.0 };
    brush.Radius = 2.0;
    brush.Strength = 0.25;
    const auto update = session.Apply(brush);
    CHECK_FALSE(update.TopologyChanged);
    CHECK(update.Vertices.size() == 4u);
    CHECK(session.UndoDepth() == 1u);
    CHECK(session.UndoBytes() > 0u);
    CHECK(session.Undo());
    CHECK(session.UndoDepth() == 0u);
    CHECK(session.Redo());
    CHECK(session.UndoDepth() == 1u);
}

TEST_CASE("production sculpt rejects strokes before exceeding the vertex budget")
{
    using namespace kairo::assets;
    EditableMesh mesh = MakeSculptQuad();
    SculptSessionBudget budget;
    budget.MaximumVerticesPerStroke = 2u;
    ProductionSculptSession session(mesh, budget);
    SculptBrush brush;
    brush.Center = { 0.0, 0.0, 0.0 };
    brush.Radius = 2.0;
    CHECK_THROWS_AS(session.Apply(brush), std::length_error);
    CHECK(session.UndoDepth() == 0u);
}

TEST_CASE("sculpt remesh raises topology detail within a hard face budget")
{
    using namespace kairo::assets;
    EditableMesh mesh = MakeSculptQuad();
    SculptRemeshSettings settings;
    settings.MinimumFaces = 4u;
    settings.MaximumFaces = 16u;
    settings.MaximumSubdivisionPasses = 2u;
    const auto update = RemeshForSculpt(mesh, settings);
    CHECK(update.TopologyChanged);
    CHECK(mesh.Faces().size() >= 4u);
    CHECK(mesh.Faces().size() <= 16u);
    CHECK(update.Vertices.size() == mesh.Vertices().size());
    CHECK(mesh.Validate().Valid);
}

TEST_CASE("sculpt multiresolution keeps bounded independent topology levels")
{
    using namespace kairo::assets;
    SculptMultiresolution levels(MakeSculptQuad());
    REQUIRE(levels.LevelCount() == 1u);
    levels.BuildNextLevel(32u);
    REQUIRE(levels.LevelCount() == 2u);
    CHECK(levels.ActiveLevel() == 0u);
    levels.SetActiveLevel(1u);
    CHECK(levels.ActiveLevel() == 1u);
    CHECK(levels.ActiveMesh().Faces().size() > 1u);
    CHECK(levels.ActiveMesh().Validate().Valid);
}
