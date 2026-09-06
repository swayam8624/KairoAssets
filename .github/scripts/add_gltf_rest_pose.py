from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


artifact = Path("GltfSceneArtifact.cppm")
text = artifact.read_text()

text = replace_once(
    text,
    '''        std::vector<std::uint32_t> PrimitiveIndices;
        std::uint32_t SkinIndex = GltfMissingIndex;

        friend bool operator==(const GltfNodeData&, const GltfNodeData&) = default;
''',
    '''        std::vector<std::uint32_t> PrimitiveIndices;
        std::uint32_t SkinIndex = GltfMissingIndex;
        bool HasRestTRS = false;
        std::array<float, 3u> RestTranslation{};
        std::array<float, 4u> RestRotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        std::array<float, 3u> RestScale{ 1.0f, 1.0f, 1.0f };

        friend bool operator==(const GltfNodeData&, const GltfNodeData&) = default;
''',
    "node rest pose fields",
)

text = replace_once(
    text,
    '''            if (node.SkinIndex != MissingIndex)
            {
                if (node.SkinIndex >= scene.Skins.size())
                    throw std::out_of_range("glTF node skin index is invalid.");
                if (node.PrimitiveIndices.empty())
                    throw std::invalid_argument("A glTF skin binding requires a mesh-bearing node.");
            }
        }
''',
    '''            if (node.SkinIndex != MissingIndex)
            {
                if (node.SkinIndex >= scene.Skins.size())
                    throw std::out_of_range("glTF node skin index is invalid.");
                if (node.PrimitiveIndices.empty())
                    throw std::invalid_argument("A glTF skin binding requires a mesh-bearing node.");
            }
            if (node.HasRestTRS)
            {
                if (!Finite(node.RestTranslation) || !Finite(node.RestRotation) ||
                    !Finite(node.RestScale))
                    throw std::invalid_argument("glTF node rest TRS values must be finite.");
                float rotationLengthSquared = 0.0f;
                for (float value : node.RestRotation)
                    rotationLengthSquared += value * value;
                if (std::abs(rotationLengthSquared - 1.0f) > 1.0e-3f)
                    throw std::invalid_argument(
                        "glTF node rest rotation must contain a unit quaternion.");
            }
            else if (node.RestTranslation != std::array<float, 3u>{} ||
                node.RestRotation != std::array<float, 4u>{ 0.0f, 0.0f, 0.0f, 1.0f } ||
                node.RestScale != std::array<float, 3u>{ 1.0f, 1.0f, 1.0f })
            {
                throw std::invalid_argument(
                    "Matrix-authored glTF nodes must keep canonical unused rest TRS fields.");
            }
        }
''',
    "node rest pose validation",
)

text = replace_once(
    text,
    '''                const GltfAnimationChannelData& channel = clip.Channels[channelIndex];
                if (channel.TargetNode >= scene.Nodes.size())
                    throw std::out_of_range("glTF animation target node index is invalid.");
                switch (channel.Path)
''',
    '''                const GltfAnimationChannelData& channel = clip.Channels[channelIndex];
                if (channel.TargetNode >= scene.Nodes.size())
                    throw std::out_of_range("glTF animation target node index is invalid.");
                if (!scene.Nodes[channel.TargetNode].HasRestTRS)
                    throw std::invalid_argument(
                        "glTF animation targets require a preserved rest TRS pose.");
                switch (channel.Path)
''',
    "animated target rest pose validation",
)

text = replace_once(
    text,
    '''            for (const std::uint32_t primitive : node.PrimitiveIndices) writer.WriteU32(primitive);
            writer.WriteU32(node.SkinIndex);
        }
''',
    '''            for (const std::uint32_t primitive : node.PrimitiveIndices) writer.WriteU32(primitive);
            writer.WriteU32(node.SkinIndex);
            writer.WriteU8(node.HasRestTRS ? 1u : 0u);
            for (float value : node.RestTranslation) writer.WriteF32(value);
            for (float value : node.RestRotation) writer.WriteF32(value);
            for (float value : node.RestScale) writer.WriteF32(value);
        }
''',
    "rest pose serialization",
)

text = replace_once(
    text,
    '''            for (std::uint32_t& primitive : node.PrimitiveIndices)
                primitive = reader.ReadU32();
            if (payloadVersion >= PayloadVersion) node.SkinIndex = reader.ReadU32();
            scene.Nodes.push_back(std::move(node));
''',
    '''            for (std::uint32_t& primitive : node.PrimitiveIndices)
                primitive = reader.ReadU32();
            if (payloadVersion >= PayloadVersion)
            {
                node.SkinIndex = reader.ReadU32();
                const std::uint8_t hasRestTRS = reader.ReadU8();
                if (hasRestTRS > 1u)
                    throw std::invalid_argument("glTF node rest TRS flag is invalid.");
                node.HasRestTRS = hasRestTRS != 0u;
                for (float& value : node.RestTranslation) value = reader.ReadF32();
                for (float& value : node.RestRotation) value = reader.ReadF32();
                for (float& value : node.RestScale) value = reader.ReadF32();
            }
            scene.Nodes.push_back(std::move(node));
''',
    "rest pose parse",
)

artifact.write_text(text)

# Capture source TRS before it is collapsed into LocalTransform.
importer = Path("GltfImporter.cppm")
text = importer.read_text()
text = replace_once(
    text,
    '''                cgltf_node_transform_local(&source, node.LocalTransform.data());
                if (source.mesh != nullptr)
''',
    '''                cgltf_node_transform_local(&source, node.LocalTransform.data());
                if (source.has_matrix == 0)
                {
                    node.HasRestTRS = true;
                    if (source.has_translation != 0)
                        for (std::size_t axis = 0u; axis < 3u; ++axis)
                            node.RestTranslation[axis] = source.translation[axis];
                    if (source.has_rotation != 0)
                    {
                        for (std::size_t component = 0u; component < 4u; ++component)
                            node.RestRotation[component] = source.rotation[component];
                        NormalizeQuaternion(node.RestRotation);
                    }
                    if (source.has_scale != 0)
                        for (std::size_t axis = 0u; axis < 3u; ++axis)
                            node.RestScale[axis] = source.scale[axis];
                }
                if (source.mesh != nullptr)
''',
    "source rest pose capture",
)
importer.write_text(text)

# Update focused artifact fixture so its animated target has an explicit rest pose,
# and clear that v2-only metadata in tests that intentionally publish v1.
tests = Path("tests/GltfSceneArtifactTests.cpp")
text = tests.read_text()
text = replace_once(
    text,
    '''        GltfNodeData root;
        root.Name = "RootJoint";
        scene.Nodes.push_back(root);

        GltfNodeData meshNode;
        meshNode.Name = "Character";
''',
    '''        GltfNodeData root;
        root.Name = "RootJoint";
        root.HasRestTRS = true;
        scene.Nodes.push_back(root);

        GltfNodeData meshNode;
        meshNode.Name = "Character";
        meshNode.HasRestTRS = true;
''',
    "animated fixture rest pose",
)
# Two legacy-static conversion sites currently set parent/root after erasing root.
text = text.replace(
    '''    source.Nodes[0].Parent = -1;
    source.RootNodes = { 0u };
''',
    '''    source.Nodes[0].Parent = -1;
    source.Nodes[0].HasRestTRS = false;
    source.Nodes[0].RestTranslation = {};
    source.Nodes[0].RestRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    source.Nodes[0].RestScale = { 1.0f, 1.0f, 1.0f };
    source.RootNodes = { 0u };
''')
text = text.replace(
    '''    staticScene.Nodes[0].Parent = -1;
    staticScene.RootNodes = { 0u };
''',
    '''    staticScene.Nodes[0].Parent = -1;
    staticScene.Nodes[0].HasRestTRS = false;
    staticScene.Nodes[0].RestTranslation = {};
    staticScene.Nodes[0].RestRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    staticScene.Nodes[0].RestScale = { 1.0f, 1.0f, 1.0f };
    staticScene.RootNodes = { 0u };
''')
tests.write_text(text)

# End-to-end importer assertions: the source has default TRS and the animated
# target must preserve it even though its current local matrix is also stored.
tests = Path("tests/GltfAnimationImporterTests.cpp")
text = tests.read_text()
text = replace_once(
    text,
    '''    CHECK(scene.Animations[0].Channels[0].TargetNode == 1u);
    CHECK(scene.Animations[0].Channels[0].Path == GltfAnimationPath::Translation);
''',
    '''    CHECK(scene.Animations[0].Channels[0].TargetNode == 1u);
    REQUIRE(scene.Nodes[1].HasRestTRS);
    CHECK(scene.Nodes[1].RestTranslation == std::array<float, 3u>{ 0.0f, 0.0f, 0.0f });
    CHECK(scene.Nodes[1].RestRotation == std::array<float, 4u>{ 0.0f, 0.0f, 0.0f, 1.0f });
    CHECK(scene.Nodes[1].RestScale == std::array<float, 3u>{ 1.0f, 1.0f, 1.0f });
    CHECK(scene.Animations[0].Channels[0].Path == GltfAnimationPath::Translation);
''',
    "importer rest pose assertions",
)
# Explicit array usage should not rely on imported module headers.
text = replace_once(text, '#include <bit>\n', '#include <array>\n#include <bit>\n', "array header")
tests.write_text(text)

# Document why rest pose belongs in the asset schema rather than runtime matrix decomposition.
doc = Path("docs/GLTF_ANIMATION_V2.md")
text = doc.read_text()
needle = '''`GltfAnimationClipData` contains node-targeted channels. V2 preserves three transform paths:
'''
insert = '''For nodes authored with glTF TRS properties, v2 also preserves `RestTranslation`, `RestRotation`, and `RestScale` plus a `HasRestTRS` marker. Animated targets are required to have this rest pose. This is deliberate: a clip often animates only one of translation/rotation/scale, and reconstructing the untouched properties later from the composed matrix is lossy or ambiguous under negative scale. Matrix-authored static nodes keep `HasRestTRS == false` and canonical unused rest fields.

`GltfAnimationClipData` contains node-targeted channels. V2 preserves three transform paths:
'''
text = replace_once(text, needle, insert, "rest pose documentation")
doc.write_text(text)

Path(".github/workflows/add-gltf-rest-pose.yml").unlink(missing_ok=True)
Path(".github/scripts/add_gltf_rest_pose.py").unlink(missing_ok=True)
