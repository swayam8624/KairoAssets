module;

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Assets.GltfImporter;

import Kairo.Assets.DerivedArtifact;
import Kairo.Assets.GltfSceneArtifact;
import Kairo.Assets.Importer;
import Kairo.Assets.MeshArtifact;
import Kairo.Assets.Types;

export namespace kairo::assets
{
    namespace gltf_importer_detail
    {
        class DataOwner final
        {
        public:
            explicit DataOwner(cgltf_data* data) noexcept : m_Data(data) {}
            DataOwner(const DataOwner&) = delete;
            DataOwner& operator=(const DataOwner&) = delete;
            ~DataOwner() { if (m_Data != nullptr) cgltf_free(m_Data); }
        private:
            cgltf_data* m_Data;
        };

        [[nodiscard]] inline std::string ResultMessage(cgltf_result result)
        {
            switch (result)
            {
                case cgltf_result_success: return "success";
                case cgltf_result_data_too_short: return "data too short";
                case cgltf_result_unknown_format: return "unknown format";
                case cgltf_result_invalid_json: return "invalid JSON";
                case cgltf_result_invalid_gltf: return "invalid glTF";
                case cgltf_result_invalid_options: return "invalid options";
                case cgltf_result_file_not_found: return "dependency file not found";
                case cgltf_result_io_error: return "I/O error";
                case cgltf_result_out_of_memory: return "out of memory";
                case cgltf_result_legacy_gltf: return "legacy glTF is unsupported";
                default: return "unknown cgltf error";
            }
        }

        [[nodiscard]] inline const cgltf_accessor* FindAttribute(
            const cgltf_primitive& primitive, cgltf_attribute_type type, cgltf_int set = 0)
        {
            for (cgltf_size index = 0u; index < primitive.attributes_count; ++index)
            {
                const cgltf_attribute& attribute = primitive.attributes[index];
                if (attribute.type == type && attribute.index == set)
                    return attribute.data;
            }
            return nullptr;
        }

        [[nodiscard]] inline std::string TextureUri(const cgltf_texture_view& view)
        {
            if (view.texture == nullptr || view.texture->image == nullptr ||
                view.texture->image->uri == nullptr)
                return {};
            return view.texture->image->uri;
        }

        [[nodiscard]] inline GltfTextureBinding TextureBinding(
            const cgltf_texture_view& view, float scale = 1.0f)
        {
            GltfTextureBinding binding;
            binding.Uri = TextureUri(view);
            binding.TexCoord = view.texcoord < 0 ? 0u : static_cast<std::uint32_t>(view.texcoord);
            binding.Scale = scale;
            return binding;
        }

        [[nodiscard]] inline GltfMaterialData ConvertMaterial(const cgltf_material& source)
        {
            GltfMaterialData material;
            if (source.name != nullptr) material.Name = source.name;
            if (source.has_pbr_metallic_roughness)
            {
                for (std::size_t index = 0u; index < 4u; ++index)
                    material.BaseColorFactor[index] =
                        source.pbr_metallic_roughness.base_color_factor[index];
                material.MetallicFactor =
                    source.pbr_metallic_roughness.metallic_factor;
                material.RoughnessFactor =
                    source.pbr_metallic_roughness.roughness_factor;
                material.BaseColorTexture =
                    TextureBinding(source.pbr_metallic_roughness.base_color_texture);
                material.MetallicRoughnessTexture =
                    TextureBinding(source.pbr_metallic_roughness.metallic_roughness_texture);
            }
            for (std::size_t index = 0u; index < 3u; ++index)
                material.EmissiveFactor[index] = source.emissive_factor[index];
            material.NormalTexture = TextureBinding(
                source.normal_texture, source.normal_texture.scale);
            material.OcclusionTexture = TextureBinding(
                source.occlusion_texture, source.occlusion_texture.scale);
            material.EmissiveTexture = TextureBinding(source.emissive_texture);
            material.AlphaCutoff = source.alpha_cutoff;
            material.DoubleSided = source.double_sided != 0;
            switch (source.alpha_mode)
            {
                case cgltf_alpha_mode_opaque: material.AlphaMode = GltfAlphaMode::Opaque; break;
                case cgltf_alpha_mode_mask: material.AlphaMode = GltfAlphaMode::Mask; break;
                case cgltf_alpha_mode_blend: material.AlphaMode = GltfAlphaMode::Blend; break;
                default: throw std::invalid_argument("glTF material alpha mode is unsupported.");
            }
            return material;
        }

        inline void ReadVector(const cgltf_accessor& accessor, cgltf_size index,
            float* destination, cgltf_size components, const char* role)
        {
            if (cgltf_accessor_read_float(&accessor, index, destination, components) == 0)
                throw std::invalid_argument(std::string("Unable to read glTF ") + role + " accessor.");
            for (cgltf_size component = 0u; component < components; ++component)
                if (!std::isfinite(destination[component]))
                    throw std::invalid_argument(std::string("glTF ") + role + " values must be finite.");
        }

        inline void Normalize3(std::array<float, 3u>& value, const char* role)
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
            const cgltf_data& data, const cgltf_primitive& source)
        {
            if (source.type != cgltf_primitive_type_triangles)
                throw std::invalid_argument("Only glTF triangle-list primitives are supported.");
            if (source.targets_count != 0u)
                throw std::invalid_argument(
                    "glTF morph targets are not supported by scene artifact v2.");

            const cgltf_accessor* positions =
                FindAttribute(source, cgltf_attribute_type_position);
            if (positions == nullptr || positions->type != cgltf_type_vec3)
                throw std::invalid_argument("glTF primitive requires VEC3 POSITION data.");
            if (positions->count < 3u ||
                positions->count > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("glTF primitive vertex count is outside the supported range.");

            const cgltf_accessor* normals =
                FindAttribute(source, cgltf_attribute_type_normal);
            const cgltf_accessor* texCoords =
                FindAttribute(source, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* tangents =
                FindAttribute(source, cgltf_attribute_type_tangent);
            const cgltf_accessor* joints =
                FindAttribute(source, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* weights =
                FindAttribute(source, cgltf_attribute_type_weights, 0);
            if (FindAttribute(source, cgltf_attribute_type_joints, 1) != nullptr ||
                FindAttribute(source, cgltf_attribute_type_weights, 1) != nullptr)
                throw std::invalid_argument(
                    "glTF scene artifact v2 supports at most four skin influences per vertex.");
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
                throw std::invalid_argument("glTF NORMAL accessor must match POSITION.");
            if (texCoords != nullptr && (texCoords->type != cgltf_type_vec2 ||
                texCoords->count != positions->count))
                throw std::invalid_argument("glTF TEXCOORD_0 accessor must match POSITION.");
            if (tangents != nullptr && (tangents->type != cgltf_type_vec4 ||
                tangents->count != positions->count))
                throw std::invalid_argument("glTF TANGENT accessor must match POSITION.");

            GltfPrimitiveData primitive;
            primitive.Mesh.HasNormals = normals != nullptr;
            primitive.Mesh.HasTexCoords = texCoords != nullptr;
            primitive.Mesh.Vertices.resize(static_cast<std::size_t>(positions->count));
            if (tangents != nullptr)
                primitive.Tangents.resize(static_cast<std::size_t>(positions->count));
            if (joints != nullptr)
                primitive.Skinning.resize(static_cast<std::size_t>(positions->count));

            for (cgltf_size vertexIndex = 0u; vertexIndex < positions->count; ++vertexIndex)
            {
                MeshArtifactVertex& vertex =
                    primitive.Mesh.Vertices[static_cast<std::size_t>(vertexIndex)];
                ReadVector(*positions, vertexIndex, vertex.Position.data(), 3u, "position");
                if (normals != nullptr)
                {
                    ReadVector(*normals, vertexIndex, vertex.Normal.data(), 3u, "normal");
                    Normalize3(vertex.Normal, "normal");
                }
                if (texCoords != nullptr)
                    ReadVector(*texCoords, vertexIndex, vertex.TexCoord.data(), 2u, "texture coordinate");
                if (tangents != nullptr)
                {
                    auto& tangent = primitive.Tangents[static_cast<std::size_t>(vertexIndex)];
                    ReadVector(*tangents, vertexIndex, tangent.data(), 4u, "tangent");
                    std::array<float, 3u> direction{ tangent[0], tangent[1], tangent[2] };
                    Normalize3(direction, "tangent");
                    tangent[0] = direction[0];
                    tangent[1] = direction[1];
                    tangent[2] = direction[2];
                    tangent[3] = tangent[3] < 0.0f ? -1.0f : 1.0f;
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
            {
                if (source.indices->count == 0u || source.indices->count % 3u != 0u ||
                    source.indices->count > std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("glTF index accessor must contain complete triangles.");
                primitive.Mesh.Indices.resize(static_cast<std::size_t>(source.indices->count));
                for (cgltf_size index = 0u; index < source.indices->count; ++index)
                {
                    const cgltf_size value = cgltf_accessor_read_index(source.indices, index);
                    if (value >= positions->count)
                        throw std::out_of_range("glTF index exceeds its vertex count.");
                    primitive.Mesh.Indices[static_cast<std::size_t>(index)] =
                        static_cast<std::uint32_t>(value);
                }
            }
            else
            {
                if (positions->count % 3u != 0u)
                    throw std::invalid_argument("Non-indexed glTF primitive must contain complete triangles.");
                primitive.Mesh.Indices.resize(static_cast<std::size_t>(positions->count));
                for (std::size_t index = 0u; index < primitive.Mesh.Indices.size(); ++index)
                    primitive.Mesh.Indices[index] = static_cast<std::uint32_t>(index);
            }

            // Real-world glTF files may contain zero-area cleanup triangles.
            // They are not valid canonical Kairo geometry, so filter them at
            // the source boundary while preserving the order of every valid
            // triangle. A primitive containing no usable geometry is rejected.
            std::vector<std::uint32_t> filteredIndices;
            filteredIndices.reserve(primitive.Mesh.Indices.size());
            for (std::size_t triangle = 0u;
                triangle < primitive.Mesh.Indices.size(); triangle += 3u)
            {
                const std::uint32_t ia = primitive.Mesh.Indices[triangle];
                const std::uint32_t ib = primitive.Mesh.Indices[triangle + 1u];
                const std::uint32_t ic = primitive.Mesh.Indices[triangle + 2u];
                if (IsDegenerateMeshTriangle(primitive.Mesh.Vertices, ia, ib, ic)) continue;
                filteredIndices.insert(filteredIndices.end(), { ia, ib, ic });
            }
            if (filteredIndices.empty())
                throw std::invalid_argument(
                    "glTF primitive contains no non-degenerate triangles.");
            primitive.Mesh.Indices = std::move(filteredIndices);

            if (source.material != nullptr)
            {
                const std::ptrdiff_t materialIndex = source.material - data.materials;
                if (materialIndex < 0 ||
                    static_cast<cgltf_size>(materialIndex) >= data.materials_count)
                    throw std::out_of_range("glTF primitive material pointer is invalid.");
                primitive.MaterialIndex = static_cast<std::uint32_t>(materialIndex);
            }

            ValidateMeshArtifactData(primitive.Mesh);
            return primitive;
        }
    }

    class GltfSceneImporter final : public AssetImporter
    {
    public:
        [[nodiscard]] std::string Identifier() const override { return "kairo.gltf.scene"; }
        [[nodiscard]] std::string Version() const override { return "2"; }

        [[nodiscard]] DerivedArtifact Import(const ImportRequest& request) const override
        {
            using namespace gltf_importer_detail;
            if (request.ExpectedType != AssetType::Scene)
                throw std::invalid_argument("glTF importer requires a scene asset.");
            if (request.SourceBytes.empty())
                throw std::invalid_argument("glTF source cannot be empty.");

            cgltf_options options{};
            cgltf_data* parsed = nullptr;
            const cgltf_result parseResult = cgltf_parse(
                &options, request.SourceBytes.data(), request.SourceBytes.size(), &parsed);
            if (parseResult != cgltf_result_success)
                throw std::invalid_argument("Unable to parse glTF source: " +
                    ResultMessage(parseResult));
            DataOwner owner(parsed);

            const std::string sourcePath = request.SourcePath.string();
            const cgltf_result loadResult = cgltf_load_buffers(
                &options, parsed, sourcePath.empty() ? nullptr : sourcePath.c_str());
            if (loadResult != cgltf_result_success)
                throw std::invalid_argument("Unable to load glTF buffers: " +
                    ResultMessage(loadResult));
            const cgltf_result validationResult = cgltf_validate(parsed);
            if (validationResult != cgltf_result_success)
                throw std::invalid_argument("glTF validation failed: " +
                    ResultMessage(validationResult));

            if (parsed->meshes_count == 0u)
                throw std::invalid_argument("glTF scene does not contain meshes.");
            if (parsed->materials_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->nodes_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->skins_count > std::numeric_limits<std::uint32_t>::max() ||
                parsed->animations_count > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("glTF scene exceeds 32-bit addressing.");

            GltfSceneArtifactData scene;
            scene.Materials.reserve(parsed->materials_count);
            for (cgltf_size index = 0u; index < parsed->materials_count; ++index)
                scene.Materials.push_back(ConvertMaterial(parsed->materials[index]));

            std::vector<std::vector<std::uint32_t>> meshPrimitives(parsed->meshes_count);
            for (cgltf_size meshIndex = 0u; meshIndex < parsed->meshes_count; ++meshIndex)
            {
                const cgltf_mesh& mesh = parsed->meshes[meshIndex];
                meshPrimitives[meshIndex].reserve(mesh.primitives_count);
                for (cgltf_size primitiveIndex = 0u;
                    primitiveIndex < mesh.primitives_count; ++primitiveIndex)
                {
                    if (scene.Primitives.size() >=
                        std::numeric_limits<std::uint32_t>::max())
                        throw std::length_error("glTF scene has too many primitives.");
                    meshPrimitives[meshIndex].push_back(
                        static_cast<std::uint32_t>(scene.Primitives.size()));
                    scene.Primitives.push_back(
                        ConvertPrimitive(*parsed, mesh.primitives[primitiveIndex]));
                }
            }

            scene.Nodes.resize(parsed->nodes_count);
            for (cgltf_size nodeIndex = 0u; nodeIndex < parsed->nodes_count; ++nodeIndex)
            {
                const cgltf_node& source = parsed->nodes[nodeIndex];
                GltfNodeData& node = scene.Nodes[nodeIndex];
                if (source.name != nullptr) node.Name = source.name;
                if (source.parent != nullptr)
                {
                    const std::ptrdiff_t parentIndex = source.parent - parsed->nodes;
                    if (parentIndex < 0 ||
                        static_cast<cgltf_size>(parentIndex) >= parsed->nodes_count)
                        throw std::out_of_range("glTF node parent pointer is invalid.");
                    node.Parent = static_cast<std::int32_t>(parentIndex);
                }
                cgltf_node_transform_local(&source, node.LocalTransform.data());
                if (source.mesh != nullptr)
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
            if (activeScene == nullptr && parsed->scenes_count != 0u)
                activeScene = &parsed->scenes[0];
            if (activeScene != nullptr)
            {
                scene.RootNodes.reserve(activeScene->nodes_count);
                for (cgltf_size index = 0u; index < activeScene->nodes_count; ++index)
                {
                    const std::ptrdiff_t rootIndex = activeScene->nodes[index] - parsed->nodes;
                    if (rootIndex < 0 ||
                        static_cast<cgltf_size>(rootIndex) >= parsed->nodes_count)
                        throw std::out_of_range("glTF scene root pointer is invalid.");
                    scene.RootNodes.push_back(static_cast<std::uint32_t>(rootIndex));
                }
            }
            else
            {
                for (std::size_t index = 0u; index < scene.Nodes.size(); ++index)
                    if (scene.Nodes[index].Parent == -1)
                        scene.RootNodes.push_back(static_cast<std::uint32_t>(index));
            }

            ValidateGltfSceneArtifactData(scene);
            return MakeGltfSceneDerivedArtifact(scene);
        }
    };


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
}
