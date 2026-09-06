from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


# Add exact v1 serialization for legacy importer/cache reproducibility.
artifact = Path("GltfSceneArtifact.cppm")
text = artifact.read_text()
marker = '''    [[nodiscard]] inline std::vector<std::byte> SerializeGltfSceneArtifactData(
        const GltfSceneArtifactData& scene)
    {
'''
legacy = r'''    [[nodiscard]] inline std::vector<std::byte> SerializeGltfSceneArtifactDataV1(
        const GltfSceneArtifactData& scene)
    {
        using namespace gltf_scene_artifact_detail;
        ValidateGltfSceneArtifactData(scene);
        if (!scene.Skins.empty() || !scene.Animations.empty())
            throw std::invalid_argument(
                "glTF scene artifact v1 cannot represent skins or animations.");
        for (const GltfPrimitiveData& primitive : scene.Primitives)
            if (!primitive.Skinning.empty())
                throw std::invalid_argument(
                    "glTF scene artifact v1 cannot represent vertex skinning.");
        for (const GltfNodeData& node : scene.Nodes)
            if (node.SkinIndex != MissingIndex)
                throw std::invalid_argument(
                    "glTF scene artifact v1 cannot represent node skin bindings.");

        BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(LegacyPayloadVersion);
        writer.WriteU32(static_cast<std::uint32_t>(scene.Materials.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.Primitives.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.Nodes.size()));
        writer.WriteU32(static_cast<std::uint32_t>(scene.RootNodes.size()));

        for (const GltfMaterialData& material : scene.Materials)
        {
            WriteString(writer, material.Name, MaximumNameBytes, "glTF material name");
            for (float value : material.BaseColorFactor) writer.WriteF32(value);
            writer.WriteF32(material.MetallicFactor);
            writer.WriteF32(material.RoughnessFactor);
            for (float value : material.EmissiveFactor) writer.WriteF32(value);
            writer.WriteU8(static_cast<std::uint8_t>(material.AlphaMode));
            writer.WriteF32(material.AlphaCutoff);
            writer.WriteU8(material.DoubleSided ? 1u : 0u);
            WriteTextureBinding(writer, material.BaseColorTexture);
            WriteTextureBinding(writer, material.MetallicRoughnessTexture);
            WriteTextureBinding(writer, material.NormalTexture);
            WriteTextureBinding(writer, material.OcclusionTexture);
            WriteTextureBinding(writer, material.EmissiveTexture);
        }

        for (const GltfPrimitiveData& primitive : scene.Primitives)
        {
            const std::vector<std::byte> mesh = SerializeMeshArtifactData(primitive.Mesh);
            writer.WriteU64(static_cast<std::uint64_t>(mesh.size()));
            writer.WriteBytes(mesh);
            writer.WriteU32(static_cast<std::uint32_t>(primitive.Tangents.size()));
            for (const auto& tangent : primitive.Tangents)
                for (float value : tangent) writer.WriteF32(value);
            writer.WriteU32(primitive.MaterialIndex);
        }

        for (const GltfNodeData& node : scene.Nodes)
        {
            WriteString(writer, node.Name, MaximumNameBytes, "glTF node name");
            writer.WriteU32(std::bit_cast<std::uint32_t>(node.Parent));
            for (float value : node.LocalTransform) writer.WriteF32(value);
            writer.WriteU32(static_cast<std::uint32_t>(node.PrimitiveIndices.size()));
            for (const std::uint32_t primitive : node.PrimitiveIndices) writer.WriteU32(primitive);
        }
        for (const std::uint32_t root : scene.RootNodes) writer.WriteU32(root);

        if (writer.Bytes().size() > MaximumDerivedArtifactPayloadBytes)
            throw std::length_error("glTF scene artifact exceeds its payload safety limit.");
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline DerivedArtifact MakeGltfSceneDerivedArtifactV1(
        const GltfSceneArtifactData& scene)
    {
        return { AssetType::Scene, 1u, "kairo.gltf-scene.v1",
            SerializeGltfSceneArtifactDataV1(scene) };
    }

'''
if legacy not in text:
    if marker not in text:
        raise SystemExit("v2 serializer marker not found")
    text = text.replace(marker, legacy + marker, 1)
artifact.write_text(text)

# Add a v1 compatibility importer that delegates source decoding to current
# parsing, then refuses any semantics the old artifact could not represent.
importer = Path("GltfImporter.cppm")
text = importer.read_text()
marker = '''    class GltfSceneImporter final : public AssetImporter
    {
'''
# Leave current class as latest v2. Add the legacy wrapper after it, immediately
# before the namespace closes.
end_marker = '''    };
}
'''
legacy_importer = r'''

    /// Frozen compatibility identity for projects/import records authored before
    /// skinning/animation artifact v2. Static source decoding reuses the current
    /// validated parser, but publication is forced back through the exact v1
    /// binary schema. Any v2-only semantics fail instead of being discarded.
    class GltfSceneImporterV1 final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.gltf.scene"; }
        [[nodiscard]] std::string Version() const override { return "1"; }

        [[nodiscard]] DerivedArtifact Import(const ImportRequest& request) const override
        {
            GltfSceneImporter latest;
            const GltfSceneArtifactData scene =
                ParseGltfSceneDerivedArtifact(latest.Import(request));
            if (!scene.Skins.empty() || !scene.Animations.empty())
                throw std::invalid_argument(
                    "kairo.gltf.scene@1 supports static glTF scenes only.");
            for (const GltfPrimitiveData& primitive : scene.Primitives)
                if (!primitive.Skinning.empty())
                    throw std::invalid_argument(
                        "kairo.gltf.scene@1 cannot preserve vertex skinning.");
            for (const GltfNodeData& node : scene.Nodes)
                if (node.SkinIndex != GltfMissingIndex)
                    throw std::invalid_argument(
                        "kairo.gltf.scene@1 cannot preserve node skin bindings.");
            return MakeGltfSceneDerivedArtifactV1(scene);
        }
    };
'''
if legacy_importer not in text:
    pos = text.rfind(end_marker)
    if pos < 0:
        raise SystemExit("gltf importer namespace end marker not found")
    text = text[:pos + len('    };\n')] + legacy_importer + text[pos + len('    };\n'):]
importer.write_text(text)

# Register both immutable versions.
builtin = Path("BuiltinImporters.cppm")
text = builtin.read_text()
text = replace_once(
    text,
    '        registry.Register(std::make_shared<GltfSceneImporter>());\n',
    '        registry.Register(std::make_shared<GltfSceneImporterV1>());\n'
    '        registry.Register(std::make_shared<GltfSceneImporter>());\n',
    "builtin glTF versions",
)
builtin.write_text(text)

# Update the reproducibility test to assert coexistence, not replacement.
tests = Path("tests/Phase2AssetTests.cpp")
text = tests.read_text()
text = replace_once(
    text,
    '''    CHECK(registry.Size() == 4u);
    CHECK(registry.Contains("kairo.obj", "1"));
    CHECK(registry.Contains("kairo.texture.stb", "1"));
    CHECK(registry.Contains("kairo.gltf.scene", "1"));
''',
    '''    CHECK(registry.Size() == 5u);
    CHECK(registry.Contains("kairo.obj", "1"));
    CHECK(registry.Contains("kairo.texture.stb", "1"));
    CHECK(registry.Contains("kairo.gltf.scene", "1"));
    CHECK(registry.Contains("kairo.gltf.scene", "2"));
''',
    "phase2 importer registry expectations",
)
tests.write_text(text)

# Add direct v1 compatibility publication coverage.
focused = Path("tests/GltfSceneArtifactTests.cpp")
text = focused.read_text()
addition = r'''

TEST_CASE("legacy glTF v1 serializer publishes exact static schema")
{
    auto scene = AnimatedScene();
    scene.Skins.clear();
    scene.Animations.clear();
    scene.Primitives[0].Skinning.clear();
    scene.Nodes[1].SkinIndex = GltfMissingIndex;
    scene.Nodes.erase(scene.Nodes.begin());
    scene.Nodes[0].Parent = -1;
    scene.RootNodes = { 0u };

    const auto payload = SerializeGltfSceneArtifactDataV1(scene);
    const auto parsed = ParseGltfSceneArtifactData(payload);
    CHECK(parsed == scene);
    const auto artifact = MakeGltfSceneDerivedArtifactV1(scene);
    CHECK(artifact.FormatVersion == 1u);
    CHECK(artifact.Format == "kairo.gltf-scene.v1");
    CHECK(ParseGltfSceneDerivedArtifact(artifact) == scene);
}
'''
if addition not in text:
    text += addition
focused.write_text(text)

Path(".github/workflows/restore-gltf-importer-v1.yml").unlink(missing_ok=True)
Path(".github/scripts/restore_gltf_importer_v1.py").unlink(missing_ok=True)
