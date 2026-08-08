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

        [[nodiscard]] inline GltfPrimitiveData ConvertPrimitive(
            const cgltf_data& data, const cgltf_primitive& source)
        {
            if (source.type != cgltf_primitive_type_triangles)
                throw std::invalid_argument("Only glTF triangle-list primitives are supported.");

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
        [[nodiscard]] std::string Version() const override { return "1"; }

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
                parsed->nodes_count > std::numeric_limits<std::uint32_t>::max())
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
}
