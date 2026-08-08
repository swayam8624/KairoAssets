#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets.EditableMesh;
import Kairo.Assets.UVAuthoring;
import Kairo.Assets.Sculpting;

TEST_CASE("Editable mesh exposes stable loop topology and deterministic cook")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ -1.0, 0.0, -1.0 });
    const auto b = mesh.AddVertex({  1.0, 0.0, -1.0 });
    const auto c = mesh.AddVertex({  1.0, 0.0,  1.0 });
    const auto d = mesh.AddVertex({ -1.0, 0.0,  1.0 });
    const auto face = mesh.AddFace({ a, b, c, d }, 2u);

    REQUIRE(mesh.Validate().Valid);
    const auto halfEdges = mesh.HalfEdges();
    REQUIRE(halfEdges.size() == 4u);
    CHECK_FALSE(halfEdges.front().Twin.has_value());

    const auto cooked = CookEditableMesh(mesh);
    CHECK(cooked.Vertices.size() == 4u);
    CHECK(cooked.Indices.size() == 6u);

    const auto top = mesh.ExtrudeFace(face, { 0.0, 1.0, 0.0 });
    CHECK(mesh.Face(top).Vertices.size() == 4u);
    CHECK(mesh.Faces().size() == 5u);
    REQUIRE(mesh.Validate().Valid);
}

TEST_CASE("Editable mesh transactions rollback invalid authoring")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    (void)mesh.AddFace({ a, b, c });
    const auto before = mesh.Vertices().size();
    {
        EditableMeshTransaction transaction(mesh);
        (void)mesh.AddVertex({ 2.0, 2.0, 2.0 });
    }
    CHECK(mesh.Vertices().size() == before);
}

TEST_CASE("UV authoring unwraps packs and cooks per-corner coordinates")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    const auto face = mesh.AddFace({ a, b, c, d });

    UVLayout uv;
    uv.MarkSeam(EditableEdgeKey::Canonical(a, b));
    PlanarUnwrap(mesh, uv, UVProjectionAxis::Z);
    NormalizeAndPackUVs(uv, 0.02);
    REQUIRE(uv.Contains({ face, 0u }));
    CHECK(uv.IsSeam(EditableEdgeKey::Canonical(a, b)));
    CHECK(EstimateUVTexelDensity(mesh, uv, face, 1024u) > 0.0);

    const auto cooked = CookEditableMeshWithUV(mesh, uv);
    CHECK(cooked.HasTexCoords);
    CHECK(cooked.Vertices.size() == 4u);
    CHECK(cooked.Indices.size() == 6u);
}

TEST_CASE("Sculpt strokes support masks symmetry undo and multires subdivision")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ -1.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({  1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({  0.0, 1.0, 0.0 });
    (void)mesh.AddFace({ a, b, c });

    SculptSession sculpt(mesh);
    sculpt.SetMask(c, 1.0);
    SculptBrush brush;
    brush.Mode = SculptBrushMode::Grab;
    brush.Center = { -1.0, 0.0, 0.0 };
    brush.Delta = { 0.0, 0.0, 1.0 };
    brush.Radius = 0.6;
    brush.Strength = 1.0;
    brush.Symmetry = SculptSymmetryAxis::X;
    const auto stroke = sculpt.Apply(brush);
    REQUIRE_FALSE(stroke.Deltas.empty());
    CHECK(mesh.Vertex(c).Position[2] == 0.0);
    REQUIRE(sculpt.Undo());
    CHECK(mesh.Vertex(a).Position[2] == 0.0);
    REQUIRE(sculpt.Redo());

    const auto faceCount = mesh.Faces().size();
    SubdivideEditableMesh(mesh);
    CHECK(mesh.Faces().size() > faceCount);
    REQUIRE(mesh.Validate().Valid);
}
