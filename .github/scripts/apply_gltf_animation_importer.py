from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


path = Path("GltfImporter.cppm")
text = path.read_text()

# Helpers for animation canonicalization.
marker = '''        inline void Normalize3(std::array<float, 3u>& value, const char* role)
        {
            const float length = std::sqrt(
                value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
            if (length <= std::numeric_limits<float>::epsilon())
                throw std::invalid_argument(std::string("glTF ") + role + " cannot be zero length.");
            for (float& component : value) component /= length;
        }

        [[nodiscard]] inline GltfPrimitiveData ConvertPrimitive(
'''
replacement = '''        inline void Normalize3(std::array<float, 3u>& value, const char* role)
        {
            const float length = std::sqrt(
                value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
            if (length <= std::numeric_limits<float>::epsilon())
                throw std::invalid_argument(std::string("glTF ") + role + " cannot be zero length.");
            for (float& component : value) component /= length;
        }

        inline void NormalizeQuaternion(std::array<float, 4u>& value)
        {
            float lengthSquared = 0.0f;
            for (float component : value)
            {
                if (!std::isfinite(component))
                    throw std::invalid_argument("glTF rotation quaternion must be finite.");
                lengthSquared += component * component;
            }
            if (lengthSquared <= std::numeric_limits<float>::epsilon())
                throw std::invalid_argument("glTF rotation quaternion cannot be zero length.");
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            for (float& component : value) component *= inverseLength;
        }

        [[nodiscard]] inline GltfAnimationPath ConvertAnimationPath(
            cgltf_animation_path_type path)
        {
            switch (path)
            {
                case cgltf_animation_path_type_translation:
                    return GltfAnimationPath::Translation;
                case cgltf_animation_path_type_rotation:
                    return GltfAnimationPath::Rotation;
                case cgltf_animation_path_type_scale:
                    return GltfAnimationPath::Scale;
                case cgltf_animation_path_type_weights:
                    throw std::invalid_argument(
                        "glTF morph-weight animation is not supported by scene artifact v2.");
                default:
                    throw std::invalid_argument("glTF animation path is unsupported.");
            }
        }

        [[nodiscard]] inline GltfAnimationInterpolation ConvertInterpolation(
            cgltf_interpolation_type interpolation)
        {
            switch (interpolation)
            {
                case cgltf_interpolation_type_linear:
                    return GltfAnimationInterpolation::Linear;
                case cgltf_interpolation_type_step:
                    return GltfAnimationInterpolation::Step;
                case cgltf_interpolation_type_cubic_spline:
                    return GltfAnimationInterpolation::CubicSpline;
                default:
                    throw std::invalid_argument("glTF animation interpolation is unsupported.");
            }
        }

        [[nodiscard]] inline GltfPrimitiveData ConvertPrimitive(
'''
text = replace_once(text, marker, replacement, "animation helper insertion")

# Discover and validate JOINTS_0 / WEIGHTS_0.
marker = '''            const cgltf_accessor* tangents =
                FindAttribute(source, cgltf_attribute_type_tangent);
            if (normals != nullptr && (normals->type != cgltf_type_vec3 ||
                normals->count != positions->count))
'''
replacement = '''            const cgltf_accessor* tangents =
                FindAttribute(source, cgltf_attribute_type_tangent);
            const cgltf_accessor* joints =
                FindAttribute(source, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* weights =
                FindAttribute(source, cgltf_attribute_type_weights, 0);
            if ((joints == nullptr) != (weights == nullptr))
                throw std::invalid_argument(
                    "glTF primitive must provide JOINTS_0 and WEIGHTS_0 together.");
            if (joints != nullptr)
            {
                if (joints->type != cgltf_type_vec4 || joints->count != positions->count ||
                    (joints->component_type != cgltf_component_type_r_8u &&
                     joints->component_type != cgltf_component_type_r_16u))
                    throw std::invalid_argument(
                        "glTF JOINTS_0 must be an UNSIGNED_BYTE/UNSIGNED_SHORT VEC4 matching POSITION.");
                const bool floatWeights = weights->component_type == cgltf_component_type_r_32f;
                const bool normalizedIntegerWeights = weights->normalized != 0 &&
                    (weights->component_type == cgltf_component_type_r_8u ||
                     weights->component_type == cgltf_component_type_r_16u);
                if (weights->type != cgltf_type_vec4 || weights->count != positions->count ||
                    (!floatWeights && !normalizedIntegerWeights))
                    throw std::invalid_argument(
                        "glTF WEIGHTS_0 must be FLOAT or normalized unsigned VEC4 matching POSITION.");
            }
            if (normals != nullptr && (normals->type != cgltf_type_vec3 ||
                normals->count != positions->count))
'''
text = replace_once(text, marker, replacement, "skin accessor validation")

marker = '''            primitive.Mesh.Vertices.resize(static_cast<std::size_t>(positions->count));
            if (tangents != nullptr)
                primitive.Tangents.resize(static_cast<std::size_t>(positions->count));

            for (cgltf_size vertexIndex = 0u; vertexIndex < positions->count; ++vertexIndex)
'''
replacement = '''            primitive.Mesh.Vertices.resize(static_cast<std::size_t>(positions->count));
            if (tangents != nullptr)
                primitive.Tangents.resize(static_cast<std::size_t>(positions->count));
            if (joints != nullptr)
                primitive.Skinning.resize(static_cast<std::size_t>(positions->count));

            for (cgltf_size vertexIndex = 0u; vertexIndex < positions->count; ++vertexIndex)
'''
text = replace_once(text, marker, replacement, "skin storage allocation")

marker = '''                    tangent[3] = tangent[3] < 0.0f ? -1.0f : 1.0f;
                }
            }

            if (source.indices != nullptr)
'''
replacement = '''                    tangent[3] = tangent[3] < 0.0f ? -1.0f : 1.0f;
                }
                if (joints != nullptr)
                {
                    auto& influence = primitive.Skinning[static_cast<std::size_t>(vertexIndex)];
                    std::array<cgltf_uint, 4u> jointValues{};
                    if (cgltf_accessor_read_uint(joints, vertexIndex,
                        jointValues.data(), jointValues.size()) == 0)
                        throw std::invalid_argument("Unable to read glTF JOINTS_0 accessor.");
                    for (std::size_t slot = 0u; slot < influence.Joints.size(); ++slot)
                        influence.Joints[slot] = static_cast<std::uint32_t>(jointValues[slot]);
                    ReadVector(*weights, vertexIndex, influence.Weights.data(), 4u, "skin weight");
                    float total = 0.0f;
                    for (float weight : influence.Weights)
                    {
                        if (weight < 0.0f)
                            throw std::invalid_argument("glTF skin weights cannot be negative.");
                        total += weight;
                    }
                    if (!std::isfinite(total) || total <= std::numeric_limits<float>::epsilon())
                        throw std::invalid_argument("glTF skin weights must contain a non-zero influence.");
                    for (float& weight : influence.Weights) weight /= total;
                }
            }

            if (source.indices != nullptr)
'''
text = replace_once(text, marker, replacement, "skin accessor decode")

# Importer version and top-level bounds.
text = replace_once(
    text,
    '        [[nodiscard]] std::string Version() const override { return "1"; }\n',
    '        [[nodiscard]] std::string Version() const override { return "2"; }\n',
    "glTF importer version",
)
text = replace_once(
    text,
    '''            if (parsed->materials_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->nodes_count > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("glTF scene exceeds 32-bit addressing.");
''',
    '''            if (parsed->materials_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->nodes_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->skins_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->animations_count > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("glTF scene exceeds 32-bit addressing.");
''',
    "top-level animation bounds",
)

# Bind nodes to skins.
marker = '''                if (source.mesh != nullptr)
                {
                    const std::ptrdiff_t meshIndex = source.mesh - parsed->meshes;
                    if (meshIndex < 0 ||
                        static_cast<cgltf_size>(meshIndex) >= parsed->meshes_count)
                        throw std::out_of_range("glTF node mesh pointer is invalid.");
                    node.PrimitiveIndices =
                        meshPrimitives[static_cast<std::size_t>(meshIndex)];
                }
            }

            const cgltf_scene* activeScene = parsed->scene;
'''
replacement = '''                if (source.mesh != nullptr)
                {
                    const std::ptrdiff_t meshIndex = source.mesh - parsed->meshes;
                    if (meshIndex < 0 ||
                        static_cast<cgltf_size>(meshIndex) >= parsed->meshes_count)
                        throw std::out_of_range("glTF node mesh pointer is invalid.");
                    node.PrimitiveIndices =
                        meshPrimitives[static_cast<std::size_t>(meshIndex)];
                }
                if (source.skin != nullptr)
                {
                    const std::ptrdiff_t skinIndex = source.skin - parsed->skins;
                    if (skinIndex < 0 ||
                        static_cast<cgltf_size>(skinIndex) >= parsed->skins_count)
                        throw std::out_of_range("glTF node skin pointer is invalid.");
                    node.SkinIndex = static_cast<std::uint32_t>(skinIndex);
                }
            }

            scene.Skins.reserve(parsed->skins_count);
            for (cgltf_size skinIndex = 0u; skinIndex < parsed->skins_count; ++skinIndex)
            {
                const cgltf_skin& source = parsed->skins[skinIndex];
                if (source.joints_count == 0u ||
                    source.joints_count > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("glTF skin joint count is outside the supported range.");
                GltfSkinData skin;
                if (source.name != nullptr) skin.Name = source.name;
                if (source.skeleton != nullptr)
                {
                    const std::ptrdiff_t skeletonIndex = source.skeleton - parsed->nodes;
                    if (skeletonIndex < 0 ||
                        static_cast<cgltf_size>(skeletonIndex) >= parsed->nodes_count)
                        throw std::out_of_range("glTF skeleton root pointer is invalid.");
                    skin.SkeletonRoot = static_cast<std::uint32_t>(skeletonIndex);
                }
                skin.Joints.resize(source.joints_count);
                skin.InverseBindMatrices.resize(source.joints_count);
                for (cgltf_size joint = 0u; joint < source.joints_count; ++joint)
                {
                    const std::ptrdiff_t nodeIndex = source.joints[joint] - parsed->nodes;
                    if (nodeIndex < 0 ||
                        static_cast<cgltf_size>(nodeIndex) >= parsed->nodes_count)
                        throw std::out_of_range("glTF skin joint pointer is invalid.");
                    skin.Joints[static_cast<std::size_t>(joint)] =
                        static_cast<std::uint32_t>(nodeIndex);
                    auto& inverse = skin.InverseBindMatrices[static_cast<std::size_t>(joint)];
                    inverse = {
                        1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f };
                }
                if (source.inverse_bind_matrices != nullptr)
                {
                    if (source.inverse_bind_matrices->type != cgltf_type_mat4 ||
                        source.inverse_bind_matrices->component_type != cgltf_component_type_r_32f ||
                        source.inverse_bind_matrices->count != source.joints_count)
                        throw std::invalid_argument(
                            "glTF inverseBindMatrices must be a FLOAT MAT4 accessor matching the joint count.");
                    for (cgltf_size joint = 0u; joint < source.joints_count; ++joint)
                        ReadVector(*source.inverse_bind_matrices, joint,
                            skin.InverseBindMatrices[static_cast<std::size_t>(joint)].data(),
                            16u, "inverse-bind matrix");
                }
                scene.Skins.push_back(std::move(skin));
            }

            scene.Animations.reserve(parsed->animations_count);
            for (cgltf_size animationIndex = 0u;
                animationIndex < parsed->animations_count; ++animationIndex)
            {
                const cgltf_animation& sourceAnimation = parsed->animations[animationIndex];
                if (sourceAnimation.channels_count == 0u ||
                    sourceAnimation.channels_count > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error(
                        "glTF animation channel count is outside the supported range.");
                GltfAnimationClipData clip;
                if (sourceAnimation.name != nullptr) clip.Name = sourceAnimation.name;
                clip.Channels.reserve(sourceAnimation.channels_count);
                for (cgltf_size channelIndex = 0u;
                    channelIndex < sourceAnimation.channels_count; ++channelIndex)
                {
                    const cgltf_animation_channel& sourceChannel =
                        sourceAnimation.channels[channelIndex];
                    if (sourceChannel.sampler == nullptr || sourceChannel.target_node == nullptr ||
                        sourceChannel.sampler->input == nullptr || sourceChannel.sampler->output == nullptr)
                        throw std::invalid_argument("glTF animation channel is incomplete.");

                    const std::ptrdiff_t targetIndex = sourceChannel.target_node - parsed->nodes;
                    if (targetIndex < 0 ||
                        static_cast<cgltf_size>(targetIndex) >= parsed->nodes_count)
                        throw std::out_of_range("glTF animation target node pointer is invalid.");

                    GltfAnimationChannelData channel;
                    channel.TargetNode = static_cast<std::uint32_t>(targetIndex);
                    channel.Path = ConvertAnimationPath(sourceChannel.target_path);
                    channel.Interpolation = ConvertInterpolation(sourceChannel.sampler->interpolation);
                    const cgltf_accessor& input = *sourceChannel.sampler->input;
                    const cgltf_accessor& output = *sourceChannel.sampler->output;
                    if (input.type != cgltf_type_scalar ||
                        input.component_type != cgltf_component_type_r_32f || input.count == 0u)
                        throw std::invalid_argument(
                            "glTF animation input must be a non-empty FLOAT SCALAR accessor.");
                    const cgltf_type expectedType = channel.Path == GltfAnimationPath::Rotation
                        ? cgltf_type_vec4 : cgltf_type_vec3;
                    if (output.type != expectedType ||
                        output.component_type != cgltf_component_type_r_32f)
                        throw std::invalid_argument(
                            "glTF animation output accessor type does not match its target path.");
                    const bool cubic = channel.Interpolation == GltfAnimationInterpolation::CubicSpline;
                    if (cubic && input.count > std::numeric_limits<cgltf_size>::max() / 3u)
                        throw std::length_error("glTF cubic animation key count overflows addressing.");
                    const cgltf_size expectedOutputCount = cubic ? input.count * 3u : input.count;
                    if (output.count != expectedOutputCount)
                        throw std::invalid_argument(
                            "glTF animation output count does not match its input sampler.");

                    channel.Keyframes.resize(input.count);
                    const cgltf_size components = channel.Path == GltfAnimationPath::Rotation ? 4u : 3u;
                    for (cgltf_size keyIndex = 0u; keyIndex < input.count; ++keyIndex)
                    {
                        GltfAnimationKeyframe& key =
                            channel.Keyframes[static_cast<std::size_t>(keyIndex)];
                        ReadVector(input, keyIndex, &key.TimeSeconds, 1u, "animation time");
                        const cgltf_size valueIndex = cubic ? keyIndex * 3u + 1u : keyIndex;
                        ReadVector(output, valueIndex, key.Value.data(), components,
                            "animation value");
                        if (cubic)
                        {
                            ReadVector(output, keyIndex * 3u, key.InTangent.data(), components,
                                "animation in tangent");
                            ReadVector(output, keyIndex * 3u + 2u, key.OutTangent.data(), components,
                                "animation out tangent");
                        }
                        if (channel.Path == GltfAnimationPath::Rotation)
                            NormalizeQuaternion(key.Value);
                    }
                    clip.Channels.push_back(std::move(channel));
                }
                scene.Animations.push_back(std::move(clip));
            }

            const cgltf_scene* activeScene = parsed->scene;
'''
text = replace_once(text, marker, replacement, "skin and animation import")

path.write_text(text)

# Focused end-to-end importer test with external BIN dependency.
test = Path("tests/GltfAnimationImporterTests.cpp")
test.write_text(r'''#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import Kairo.Assets;

using namespace kairo::assets;

namespace
{
    void AppendU16(std::vector<std::byte>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xffu));
        bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    }

    void AppendF32(std::vector<std::byte>& bytes, float value)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (std::size_t byte = 0u; byte < 4u; ++byte)
            bytes.push_back(static_cast<std::byte>((bits >> (byte * 8u)) & 0xffu));
    }

    void AppendIdentity(std::vector<std::byte>& bytes)
    {
        for (std::size_t index = 0u; index < 16u; ++index)
            AppendF32(bytes, index % 5u == 0u ? 1.0f : 0.0f);
    }
}

TEST_CASE("glTF scene importer preserves skinning and TRS animation")
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-gltf-animation-" + GenerateAssetID().ToString());
    std::filesystem::create_directories(root);
    const auto gltfPath = root / "scene.gltf";
    const auto binPath = root / "scene.bin";

    std::vector<std::byte> binary;
    // POSITION: three FLOAT VEC3 vertices, offset 0, length 36.
    for (const float value : {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f }) AppendF32(binary, value);
    // JOINTS_0: three UNSIGNED_SHORT VEC4 vertices, offset 36, length 24.
    for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        for (std::size_t slot = 0u; slot < 4u; ++slot) AppendU16(binary, 0u);
    // WEIGHTS_0: three FLOAT VEC4 vertices, offset 60, length 48.
    for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        for (const float weight : { 1.0f, 0.0f, 0.0f, 0.0f }) AppendF32(binary, weight);
    // Triangle indices, offset 108, length 6, then two bytes of alignment padding.
    AppendU16(binary, 0u); AppendU16(binary, 1u); AppendU16(binary, 2u);
    AppendU16(binary, 0u);
    // One inverse bind matrix, offset 116, length 64.
    AppendIdentity(binary);
    // Animation input times, offset 180, length 8.
    AppendF32(binary, 0.0f); AppendF32(binary, 1.0f);
    // Translation output, offset 188, length 24.
    for (const float value : { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }) AppendF32(binary, value);
    REQUIRE(binary.size() == 212u);

    {
        std::ofstream output(binPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(binary.data()),
            static_cast<std::streamsize>(binary.size()));
    }

    const std::string source = R"json({
  "asset": { "version": "2.0" },
  "buffers": [{ "uri": "scene.bin", "byteLength": 212 }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 108, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 116, "byteLength": 64 },
    { "buffer": 0, "byteOffset": 180, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 188, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 4, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3" }
  ],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
      "indices": 3
    }]
  }],
  "nodes": [
    { "name": "Joint" },
    { "name": "Character", "mesh": 0, "skin": 0 }
  ],
  "skins": [{
    "name": "Rig",
    "inverseBindMatrices": 4,
    "skeleton": 0,
    "joints": [0]
  }],
  "animations": [{
    "name": "Move",
    "samplers": [{ "input": 5, "output": 6, "interpolation": "LINEAR" }],
    "channels": [{ "sampler": 0, "target": { "node": 1, "path": "translation" } }]
  }],
  "scenes": [{ "nodes": [0, 1] }],
  "scene": 0
})json";
    {
        std::ofstream output(gltfPath, std::ios::binary);
        output << source;
    }
    std::vector<std::byte> sourceBytes(source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
        sourceBytes[index] = static_cast<std::byte>(static_cast<unsigned char>(source[index]));

    GltfSceneImporter importer;
    CHECK(importer.Version() == "2");
    const auto artifact = importer.Import({ {}, AssetType::Scene, sourceBytes, gltfPath });
    REQUIRE(artifact.Format == "kairo.gltf-scene.v2");
    const auto scene = ParseGltfSceneDerivedArtifact(artifact);
    REQUIRE(scene.Primitives.size() == 1u);
    REQUIRE(scene.Primitives[0].Skinning.size() == 3u);
    CHECK(scene.Primitives[0].Skinning[0].Joints[0] == 0u);
    CHECK(scene.Primitives[0].Skinning[0].Weights[0] == 1.0f);
    REQUIRE(scene.Skins.size() == 1u);
    CHECK(scene.Skins[0].Name == "Rig");
    CHECK(scene.Skins[0].Joints == std::vector<std::uint32_t>{ 0u });
    REQUIRE(scene.Animations.size() == 1u);
    CHECK(scene.Animations[0].Name == "Move");
    REQUIRE(scene.Animations[0].Channels.size() == 1u);
    CHECK(scene.Animations[0].Channels[0].TargetNode == 1u);
    CHECK(scene.Animations[0].Channels[0].Path == GltfAnimationPath::Translation);
    REQUIRE(scene.Animations[0].Channels[0].Keyframes.size() == 2u);
    CHECK(scene.Animations[0].Channels[0].Keyframes[1].Value[0] == 1.0f);
    CHECK(scene.Animations[0].DurationSeconds() == 1.0f);

    std::filesystem::remove_all(root);
}
''')

cmake = Path("CMakeLists.txt")
cmake_text = cmake.read_text()
cmake_text = replace_once(
    cmake_text,
    '        tests/GltfSceneArtifactTests.cpp\n        tests/GLBImporterTests.cpp\n',
    '        tests/GltfSceneArtifactTests.cpp\n        tests/GltfAnimationImporterTests.cpp\n        tests/GLBImporterTests.cpp\n',
    "CMake animation importer tests",
)
cmake.write_text(cmake_text)

Path(".github/workflows/apply-gltf-animation-importer.yml").unlink(missing_ok=True)
Path(".github/scripts/apply_gltf_animation_importer.py").unlink(missing_ok=True)
