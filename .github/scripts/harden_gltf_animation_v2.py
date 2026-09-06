from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


# Reject unsupported influence/morph data instead of silently dropping it.
path = Path("GltfImporter.cppm")
text = path.read_text()
text = replace_once(
    text,
    '''            if (source.type != cgltf_primitive_type_triangles)
                throw std::invalid_argument("Only glTF triangle-list primitives are supported.");

            const cgltf_accessor* positions =
''',
    '''            if (source.type != cgltf_primitive_type_triangles)
                throw std::invalid_argument("Only glTF triangle-list primitives are supported.");
            if (source.targets_count != 0u)
                throw std::invalid_argument(
                    "glTF morph targets are not supported by scene artifact v2.");

            const cgltf_accessor* positions =
''',
    "morph target rejection",
)
text = replace_once(
    text,
    '''            const cgltf_accessor* weights =
                FindAttribute(source, cgltf_attribute_type_weights, 0);
            if ((joints == nullptr) != (weights == nullptr))
''',
    '''            const cgltf_accessor* weights =
                FindAttribute(source, cgltf_attribute_type_weights, 0);
            if (FindAttribute(source, cgltf_attribute_type_joints, 1) != nullptr ||
                FindAttribute(source, cgltf_attribute_type_weights, 1) != nullptr)
                throw std::invalid_argument(
                    "glTF scene artifact v2 supports at most four skin influences per vertex.");
            if ((joints == nullptr) != (weights == nullptr))
''',
    "secondary influence rejection",
)
path.write_text(text)

# Require derived envelope and payload schema versions to agree.
path = Path("GltfSceneArtifact.cppm")
text = path.read_text()
marker = '''        const bool v2 = artifact.FormatVersion == 2u &&
            artifact.Format == "kairo.gltf-scene.v2";
        if (artifact.Type != AssetType::Scene || (!v1 && !v2))
            throw std::invalid_argument("Derived artifact is not a supported Kairo glTF scene.");
        return ParseGltfSceneArtifactData(artifact.Payload);
'''
replacement = '''        const bool v2 = artifact.FormatVersion == 2u &&
            artifact.Format == "kairo.gltf-scene.v2";
        if (artifact.Type != AssetType::Scene || (!v1 && !v2))
            throw std::invalid_argument("Derived artifact is not a supported Kairo glTF scene.");
        BinaryReader header(artifact.Payload);
        if (!std::equal(gltf_scene_artifact_detail::Magic.begin(),
            gltf_scene_artifact_detail::Magic.end(),
            header.ReadBytes(gltf_scene_artifact_detail::Magic.size()).begin()))
            throw std::invalid_argument("glTF scene artifact magic is invalid.");
        const std::uint32_t payloadVersion = header.ReadU32();
        if ((v1 && payloadVersion != gltf_scene_artifact_detail::LegacyPayloadVersion) ||
            (v2 && payloadVersion != gltf_scene_artifact_detail::PayloadVersion))
            throw std::invalid_argument(
                "glTF derived artifact envelope does not match its payload version.");
        return ParseGltfSceneArtifactData(artifact.Payload);
'''
text = replace_once(text, marker, replacement, "derived payload version agreement")
path.write_text(text)

# Add mismatch coverage to the focused artifact suite.
path = Path("tests/GltfSceneArtifactTests.cpp")
text = path.read_text()
insert = '''
TEST_CASE("glTF derived artifact rejects envelope and payload version mismatch")
{
    const auto scene = AnimatedScene();
    const auto v2Payload = SerializeGltfSceneArtifactData(scene);
    const DerivedArtifact falseV1{ AssetType::Scene, 1u, "kairo.gltf-scene.v1", v2Payload };
    REQUIRE_THROWS_AS(ParseGltfSceneDerivedArtifact(falseV1), std::invalid_argument);

    auto staticScene = scene;
    staticScene.Skins.clear();
    staticScene.Animations.clear();
    staticScene.Primitives[0].Skinning.clear();
    staticScene.Nodes[1].SkinIndex = GltfMissingIndex;
    staticScene.Nodes.erase(staticScene.Nodes.begin());
    staticScene.Nodes[0].Parent = -1;
    staticScene.RootNodes = { 0u };
    const auto v1Payload = LegacyV1Payload(staticScene);
    const DerivedArtifact falseV2{ AssetType::Scene, 2u, "kairo.gltf-scene.v2", v1Payload };
    REQUIRE_THROWS_AS(ParseGltfSceneDerivedArtifact(falseV2), std::invalid_argument);
}
'''
if insert not in text:
    text += insert
path.write_text(text)

Path(".github/workflows/harden-gltf-animation-v2.yml").unlink(missing_ok=True)
Path(".github/scripts/harden_gltf_animation_v2.py").unlink(missing_ok=True)
