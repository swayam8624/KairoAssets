#include <algorithm>
#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets.EditableMesh;
import Kairo.Assets.ModelingOperations;

namespace
{
    struct QuadFixture final
    {
        kairo::assets::EditableMesh Mesh;
        kairo::assets::EditableVertexID A;
        kairo::assets::EditableVertexID B;
        kairo::assets::EditableVertexID C;
        kairo::assets::EditableVertexID D;
        kairo::assets::EditableFaceID Face;
    };

    QuadFixture MakeQuad()
    {
        using namespace kairo::assets;
        QuadFixture result;
        result.A = result.Mesh.AddVertex({ 0.0, 0.0, 0.0 });
        result.B = result.Mesh.AddVertex({ 1.0, 0.0, 0.0 });
        result.C = result.Mesh.AddVertex({ 1.0, 1.0, 0.0 });
        result.D = result.Mesh.AddVertex({ 0.0, 1.0, 0.0 });
        result.Face = result.Mesh.AddFace({ result.A, result.B, result.C, result.D }, 3u);
        return result;
    }
}

TEST_CASE("edge split preserves every adjacent polygon and topology")
{
    using namespace kairo::assets;
    auto fixture = MakeQuad();
    const auto split = SplitEdge(fixture.Mesh,
        EditableEdgeKey::Canonical(fixture.A, fixture.B), 0.25);
    REQUIRE(split.IsValid());
    CHECK(fixture.Mesh.Face(fixture.Face).Vertices.size() == 5u);
    CHECK(fixture.Mesh.Vertex(split).Position[0] == 0.25);
    CHECK(fixture.Mesh.Validate().Valid);
}

TEST_CASE("knife splits one polygon into two validated polygons")
{
    using namespace kairo::assets;
    auto fixture = MakeQuad();
    const auto [first, second] = KnifeFace(
        fixture.Mesh,
        fixture.Face,
        EditableEdgeKey::Canonical(fixture.A, fixture.B),
        EditableEdgeKey::Canonical(fixture.C, fixture.D));
    CHECK_FALSE(fixture.Mesh.Faces().contains(fixture.Face));
    CHECK(fixture.Mesh.Face(first).Vertices.size() == 4u);
    CHECK(fixture.Mesh.Face(second).Vertices.size() == 4u);
    CHECK(fixture.Mesh.Validate().Valid);
}

TEST_CASE("triangulate duplicate and normal tools retain deterministic material slots")
{
    using namespace kairo::assets;
    auto fixture = MakeQuad();
    const auto triangles = TriangulateFace(fixture.Mesh, fixture.Face);
    REQUIRE(triangles.size() == 2u);
    CHECK(fixture.Mesh.Face(triangles[0]).MaterialSlot == 3u);
    const auto before = FaceNormal(fixture.Mesh, triangles[0]);
    FlipFaceNormal(fixture.Mesh, triangles[0]);
    const auto after = FaceNormal(fixture.Mesh, triangles[0]);
    CHECK(after[2] == -before[2]);

    const auto copies = DuplicateFaces(fixture.Mesh, { triangles[0] }, { 0.0, 0.0, 1.0 });
    REQUIRE(copies.size() == 1u);
    CHECK(fixture.Mesh.Face(copies[0]).MaterialSlot == 3u);
    CHECK(fixture.Mesh.Validate().Valid);
}

TEST_CASE("bridge and fill construct a closed prism shell without non-manifold edges")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    std::vector<EditableVertexID> bottom{
        mesh.AddVertex({ 0.0, 0.0, 0.0 }),
        mesh.AddVertex({ 1.0, 0.0, 0.0 }),
        mesh.AddVertex({ 1.0, 1.0, 0.0 }),
        mesh.AddVertex({ 0.0, 1.0, 0.0 }) };
    std::vector<EditableVertexID> top{
        mesh.AddVertex({ 0.0, 0.0, 1.0 }),
        mesh.AddVertex({ 1.0, 0.0, 1.0 }),
        mesh.AddVertex({ 1.0, 1.0, 1.0 }),
        mesh.AddVertex({ 0.0, 1.0, 1.0 }) };
    const auto sides = BridgeLoops(mesh, bottom, top, 2u);
    CHECK(sides.size() == 4u);
    (void)FillBoundary(mesh, bottom, 2u);
    std::reverse(top.begin(), top.end());
    (void)FillBoundary(mesh, top, 2u);
    CHECK(mesh.Faces().size() == 6u);
    CHECK(mesh.Validate().Valid);
}

TEST_CASE("dissolve removes a shared diagonal and restores a quad")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    (void)mesh.AddFace({ a, b, c }, 5u);
    (void)mesh.AddFace({ a, c, d }, 5u);
    const auto result = DissolveEdge(mesh, EditableEdgeKey::Canonical(a, c));
    CHECK(mesh.Faces().size() == 1u);
    CHECK(mesh.Face(result).Vertices.size() == 4u);
    CHECK(mesh.Face(result).MaterialSlot == 5u);
    CHECK(mesh.Validate().Valid);
}

TEST_CASE("modifier stack evaluates without mutating the authored mesh")
{
    using namespace kairo::assets;
    auto fixture = MakeQuad();
    const auto original = fixture.Mesh.Vertex(fixture.A).Position;
    EditableMeshModifierStack stack;
    stack.Add(TranslateModifier{ { 0.0, 0.0, 2.0 } });
    stack.Add(TriangulateModifier{});
    const EditableMesh evaluated = stack.Evaluate(fixture.Mesh);
    CHECK(fixture.Mesh.Vertex(fixture.A).Position == original);
    CHECK(fixture.Mesh.Faces().size() == 1u);
    CHECK(evaluated.Vertex(fixture.A).Position[2] == 2.0);
    CHECK(evaluated.Faces().size() == 2u);
    CHECK(evaluated.Validate().Valid);
}
