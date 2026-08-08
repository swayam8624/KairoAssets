#include <cmath>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets.AdvancedModelingOperations;
import Kairo.Assets.EditableMesh;

TEST_CASE("quad strip loop cut propagates through opposite edges")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 2.0, 0.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    const auto e = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto f = mesh.AddVertex({ 2.0, 1.0, 0.0 });
    (void)mesh.AddFace({ a, b, e, d });
    (void)mesh.AddFace({ b, c, f, e });

    const auto result = LoopCutQuadStrip(
        mesh, EditableEdgeKey::Canonical(a, d), 0.5);
    CHECK(result.CutVertices.size() == 3u);
    CHECK(result.CreatedFaces.size() == 4u);
    CHECK(mesh.Faces().size() == 4u);
    CHECK(mesh.Validate().Valid);
}

TEST_CASE("face bevel creates an inset cap displaced along its normal")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    const auto face = mesh.AddFace({ a, b, c, d });
    const auto cap = BevelFace(mesh, face, 0.2, 0.25);
    REQUIRE(mesh.Face(cap).Vertices.size() == 4u);
    for (const auto vertex : mesh.Face(cap).Vertices)
        CHECK(mesh.Vertex(vertex).Position[2] == 0.25);
    CHECK(mesh.Faces().size() == 5u);
    CHECK(mesh.Validate().Valid);
}

TEST_CASE("smooth normal recalculation produces normalized shared vertex normals")
{
    using namespace kairo::assets;
    EditableMesh mesh;
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    (void)mesh.AddFace({ a, b, c, d });
    const auto normals = RecalculateSmoothNormals(mesh);
    REQUIRE(normals.size() == 4u);
    CHECK(std::abs(normals.at(a)[0]) < 1.0e-12);
    CHECK(std::abs(normals.at(a)[1]) < 1.0e-12);
    CHECK(std::abs(normals.at(a)[2] - 1.0) < 1.0e-12);
}
